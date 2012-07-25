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
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/irq.h>
#include <linux/usb/otg.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>

#include <mach/usb_phy.h>
#include <mach/iomap.h>

#define TEGRA_USB_DMA_ALIGN 32

struct tegra_ehci_hcd {
	struct ehci_hcd *ehci;
	struct tegra_usb_phy *phy;
	struct clk *clk;
	struct clk *emc_clk;
	struct usb_phy *transceiver;
	int host_resumed;
	int port_resuming;
	enum tegra_usb_phy_port_speed port_speed;
};

static void tegra_ehci_power_up(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);

	clk_enable(tegra->emc_clk);
	clk_enable(tegra->clk);
	tegra_usb_phy_power_on(tegra->phy);
	tegra->host_resumed = 1;
}

static void tegra_ehci_power_down(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);

	tegra->host_resumed = 0;
	tegra_usb_phy_power_off(tegra->phy);
	clk_disable(tegra->clk);
	clk_disable(tegra->emc_clk);
}

static int tegra_ehci_internal_port_reset(
	struct ehci_hcd	*ehci,
	u32 __iomem	*portsc_reg
)
{
	u32		temp;
	unsigned long	flags;
	int		retval = 0;
	int		i, tries;
	u32		saved_usbintr;

	spin_lock_irqsave(&ehci->lock, flags);
	saved_usbintr = ehci_readl(ehci, &ehci->regs->intr_enable);
	/* disable USB interrupt */
	ehci_writel(ehci, 0, &ehci->regs->intr_enable);
	spin_unlock_irqrestore(&ehci->lock, flags);

	/*
	 * Here we have to do Port Reset at most twice for
	 * Port Enable bit to be set.
	 */
	for (i = 0; i < 2; i++) {
		temp = ehci_readl(ehci, portsc_reg);
		temp |= PORT_RESET;
		ehci_writel(ehci, temp, portsc_reg);
		mdelay(10);
		temp &= ~PORT_RESET;
		ehci_writel(ehci, temp, portsc_reg);
		mdelay(1);
		tries = 100;
		do {
			mdelay(1);
			/*
			 * Up to this point, Port Enable bit is
			 * expected to be set after 2 ms waiting.
			 * USB1 usually takes extra 45 ms, for safety,
			 * we take 100 ms as timeout.
			 */
			temp = ehci_readl(ehci, portsc_reg);
		} while (!(temp & PORT_PE) && tries--);
		if (temp & PORT_PE)
			break;
	}
	if (i == 2)
		retval = -ETIMEDOUT;

	/*
	 * Clear Connect Status Change bit if it's set.
	 * We can't clear PORT_PEC. It will also cause PORT_PE to be cleared.
	 */
	if (temp & PORT_CSC)
		ehci_writel(ehci, PORT_CSC, portsc_reg);

	/*
	 * Write to clear any interrupt status bits that might be set
	 * during port reset.
	 */
	temp = ehci_readl(ehci, &ehci->regs->status);
	ehci_writel(ehci, temp, &ehci->regs->status);

	/* restore original interrupt enable bits */
	ehci_writel(ehci, saved_usbintr, &ehci->regs->intr_enable);
	return retval;
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
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	u32 __iomem	*status_reg;
	u32		temp;
	unsigned long	flags;
	int		retval = 0;

	status_reg = &ehci->regs->port_status[(wIndex & 0xff) - 1];

	spin_lock_irqsave(&ehci->lock, flags);

	if (typeReq == GetPortStatus) {
		temp = ehci_readl(ehci, status_reg);
		if (tegra->port_resuming && !(temp & PORT_SUSPEND)) {
			/* Resume completed, re-enable disconnect detection */
			tegra->port_resuming = 0;
			tegra_usb_phy_postresume(tegra->phy);
		}
	}

	else if (typeReq == SetPortFeature && wValue == USB_PORT_FEAT_SUSPEND) {
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
		if (handshake(ehci, status_reg, PORT_SUSPEND,
						PORT_SUSPEND, 5000))
			pr_err("%s: timeout waiting for SUSPEND\n", __func__);

		set_bit((wIndex & 0xff) - 1, &ehci->suspended_ports);
		goto done;
	}

	/* For USB1 port we need to issue Port Reset twice internally */
	if (tegra->phy->instance == 0 &&
	   (typeReq == SetPortFeature && wValue == USB_PORT_FEAT_RESET)) {
		spin_unlock_irqrestore(&ehci->lock, flags);
		return tegra_ehci_internal_port_reset(ehci, status_reg);
	}

	/*
	 * Tegra host controller will time the resume operation to clear the bit
	 * when the port control state switches to HS or FS Idle. This behavior
	 * is different from EHCI where the host controller driver is required
	 * to set this bit to a zero after the resume duration is timed in the
	 * driver.
	 */
	else if (typeReq == ClearPortFeature &&
					wValue == USB_PORT_FEAT_SUSPEND) {
		temp = ehci_readl(ehci, status_reg);
		if ((temp & PORT_RESET) || !(temp & PORT_PE)) {
			retval = -EPIPE;
			goto done;
		}

		if (!(temp & PORT_SUSPEND))
			goto done;

		/* Disable disconnect detection during port resume */
		tegra_usb_phy_preresume(tegra->phy);

		ehci->reset_done[wIndex-1] = jiffies + msecs_to_jiffies(25);

		temp &= ~(PORT_RWC_BITS | PORT_WAKE_BITS);
		/* start resume signalling */
		ehci_writel(ehci, temp | PORT_RESUME, status_reg);
		set_bit(wIndex-1, &ehci->resuming_ports);

		spin_unlock_irqrestore(&ehci->lock, flags);
		msleep(20);
		spin_lock_irqsave(&ehci->lock, flags);

		/* Poll until the controller clears RESUME and SUSPEND */
		if (handshake(ehci, status_reg, PORT_RESUME, 0, 2000))
			pr_err("%s: timeout waiting for RESUME\n", __func__);
		if (handshake(ehci, status_reg, PORT_SUSPEND, 0, 2000))
			pr_err("%s: timeout waiting for SUSPEND\n", __func__);

		ehci->reset_done[wIndex-1] = 0;
		clear_bit(wIndex-1, &ehci->resuming_ports);

		tegra->port_resuming = 1;
		goto done;
	}

	spin_unlock_irqrestore(&ehci->lock, flags);

	/* Handle the hub control events here */
	return ehci_hub_control(hcd, typeReq, wValue, wIndex, buf, wLength);
done:
	spin_unlock_irqrestore(&ehci->lock, flags);
	return retval;
}

static void tegra_ehci_restart(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	ehci_reset(ehci);

	/* setup the frame list and Async q heads */
	ehci_writel(ehci, ehci->periodic_dma, &ehci->regs->frame_list);
	ehci_writel(ehci, (u32)ehci->async->qh_dma, &ehci->regs->async_next);
	/* setup the command register and set the controller in RUN mode */
	ehci->command &= ~(CMD_LRESET|CMD_IAAD|CMD_PSE|CMD_ASE|CMD_RESET);
	ehci->command |= CMD_RUN;
	ehci_writel(ehci, ehci->command, &ehci->regs->command);

	down_write(&ehci_cf_port_reset_rwsem);
	ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
	/* flush posted writes */
	ehci_readl(ehci, &ehci->regs->command);
	up_write(&ehci_cf_port_reset_rwsem);
}

static void tegra_ehci_shutdown(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);

	/* ehci_shutdown touches the USB controller registers, make sure
	 * controller has clocks to it */
	if (!tegra->host_resumed)
		tegra_ehci_power_up(hcd);

	ehci_shutdown(hcd);
}

static int tegra_ehci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	/* EHCI registers start at offset 0x100 */
	ehci->caps = hcd->regs + 0x100;
	ehci->regs = hcd->regs + 0x100 +
		HC_LENGTH(ehci, readl(&ehci->caps->hc_capbase));

	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = readl(&ehci->caps->hcs_params);

	/* switch to host mode */
	hcd->has_tt = 1;
	ehci_reset(ehci);

	retval = ehci_halt(ehci);
	if (retval)
		return retval;

	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	ehci->sbrn = 0x20;

	ehci_port_power(ehci, 1);
	return retval;
}

struct dma_aligned_buffer {
	void *kmalloc_ptr;
	void *old_xfer_buffer;
	u8 data[0];
};

static void free_dma_aligned_buffer(struct urb *urb)
{
	struct dma_aligned_buffer *temp;

	if (!(urb->transfer_flags & URB_ALIGNED_TEMP_BUFFER))
		return;

	temp = container_of(urb->transfer_buffer,
		struct dma_aligned_buffer, data);

	if (usb_urb_dir_in(urb))
		memcpy(temp->old_xfer_buffer, temp->data,
		       urb->transfer_buffer_length);
	urb->transfer_buffer = temp->old_xfer_buffer;
	kfree(temp->kmalloc_ptr);

	urb->transfer_flags &= ~URB_ALIGNED_TEMP_BUFFER;
}

static int alloc_dma_aligned_buffer(struct urb *urb, gfp_t mem_flags)
{
	struct dma_aligned_buffer *temp, *kmalloc_ptr;
	size_t kmalloc_size;

	if (urb->num_sgs || urb->sg ||
	    urb->transfer_buffer_length == 0 ||
	    !((uintptr_t)urb->transfer_buffer & (TEGRA_USB_DMA_ALIGN - 1)))
		return 0;

	/* Allocate a buffer with enough padding for alignment */
	kmalloc_size = urb->transfer_buffer_length +
		sizeof(struct dma_aligned_buffer) + TEGRA_USB_DMA_ALIGN - 1;

	kmalloc_ptr = kmalloc(kmalloc_size, mem_flags);
	if (!kmalloc_ptr)
		return -ENOMEM;

	/* Position our struct dma_aligned_buffer such that data is aligned */
	temp = PTR_ALIGN(kmalloc_ptr + 1, TEGRA_USB_DMA_ALIGN) - 1;
	temp->kmalloc_ptr = kmalloc_ptr;
	temp->old_xfer_buffer = urb->transfer_buffer;
	if (usb_urb_dir_out(urb))
		memcpy(temp->data, urb->transfer_buffer,
		       urb->transfer_buffer_length);
	urb->transfer_buffer = temp->data;

	urb->transfer_flags |= URB_ALIGNED_TEMP_BUFFER;

	return 0;
}

static int tegra_ehci_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
				      gfp_t mem_flags)
{
	int ret;

	ret = alloc_dma_aligned_buffer(urb, mem_flags);
	if (ret)
		return ret;

	ret = usb_hcd_map_urb_for_dma(hcd, urb, mem_flags);
	if (ret)
		free_dma_aligned_buffer(urb);

	return ret;
}

static void tegra_ehci_unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
	usb_hcd_unmap_urb_for_dma(hcd, urb);
	free_dma_aligned_buffer(urb);
}

static const struct hc_driver tegra_ehci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Tegra EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),
	.flags			= HCD_USB2 | HCD_MEMORY,

	/* standard ehci functions */
	.irq			= ehci_irq,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,
	.get_frame_number	= ehci_get_frame,
	.hub_status_data	= ehci_hub_status_data,
	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	/* modified ehci functions for tegra */
	.reset			= tegra_ehci_setup,
	.shutdown		= tegra_ehci_shutdown,
	.map_urb_for_dma	= tegra_ehci_map_urb_for_dma,
	.unmap_urb_for_dma	= tegra_ehci_unmap_urb_for_dma,
	.hub_control		= tegra_ehci_hub_control,
#ifdef CONFIG_PM
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
#endif
};

static int setup_vbus_gpio(struct platform_device *pdev,
			   struct tegra_ehci_platform_data *pdata)
{
	int err = 0;
	int gpio;

	gpio = pdata->vbus_gpio;
	if (!gpio_is_valid(gpio))
		gpio = of_get_named_gpio(pdev->dev.of_node,
					 "nvidia,vbus-gpio", 0);
	if (!gpio_is_valid(gpio))
		return 0;

	err = gpio_request(gpio, "vbus_gpio");
	if (err) {
		dev_err(&pdev->dev, "can't request vbus gpio %d", gpio);
		return err;
	}
	err = gpio_direction_output(gpio, 1);
	if (err) {
		dev_err(&pdev->dev, "can't enable vbus\n");
		return err;
	}

	return err;
}

#ifdef CONFIG_PM

static int controller_suspend(struct device *dev)
{
	struct tegra_ehci_hcd *tegra =
			platform_get_drvdata(to_platform_device(dev));
	struct ehci_hcd	*ehci = tegra->ehci;
	struct usb_hcd *hcd = ehci_to_hcd(ehci);
	struct ehci_regs __iomem *hw = ehci->regs;
	unsigned long flags;

	if (time_before(jiffies, ehci->next_statechange))
		msleep(10);

	spin_lock_irqsave(&ehci->lock, flags);

	tegra->port_speed = (readl(&hw->port_status[0]) >> 26) & 0x3;
	ehci_halt(ehci);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	spin_unlock_irqrestore(&ehci->lock, flags);

	tegra_ehci_power_down(hcd);
	return 0;
}

static int controller_resume(struct device *dev)
{
	struct tegra_ehci_hcd *tegra =
			platform_get_drvdata(to_platform_device(dev));
	struct ehci_hcd	*ehci = tegra->ehci;
	struct usb_hcd *hcd = ehci_to_hcd(ehci);
	struct ehci_regs __iomem *hw = ehci->regs;
	unsigned long val;

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	tegra_ehci_power_up(hcd);

	if (tegra->port_speed > TEGRA_USB_PHY_PORT_SPEED_HIGH) {
		/* Wait for the phy to detect new devices
		 * before we restart the controller */
		msleep(10);
		goto restart;
	}

	/* Force the phy to keep data lines in suspend state */
	tegra_ehci_phy_restore_start(tegra->phy, tegra->port_speed);

	/* Enable host mode */
	tdi_reset(ehci);

	/* Enable Port Power */
	val = readl(&hw->port_status[0]);
	val |= PORT_POWER;
	writel(val, &hw->port_status[0]);
	udelay(10);

	/* Check if the phy resume from LP0. When the phy resume from LP0
	 * USB register will be reset. */
	if (!readl(&hw->async_next)) {
		/* Program the field PTC based on the saved speed mode */
		val = readl(&hw->port_status[0]);
		val &= ~PORT_TEST(~0);
		if (tegra->port_speed == TEGRA_USB_PHY_PORT_SPEED_HIGH)
			val |= PORT_TEST_FORCE;
		else if (tegra->port_speed == TEGRA_USB_PHY_PORT_SPEED_FULL)
			val |= PORT_TEST(6);
		else if (tegra->port_speed == TEGRA_USB_PHY_PORT_SPEED_LOW)
			val |= PORT_TEST(7);
		writel(val, &hw->port_status[0]);
		udelay(10);

		/* Disable test mode by setting PTC field to NORMAL_OP */
		val = readl(&hw->port_status[0]);
		val &= ~PORT_TEST(~0);
		writel(val, &hw->port_status[0]);
		udelay(10);
	}

	/* Poll until CCS is enabled */
	if (handshake(ehci, &hw->port_status[0], PORT_CONNECT,
						 PORT_CONNECT, 2000)) {
		pr_err("%s: timeout waiting for PORT_CONNECT\n", __func__);
		goto restart;
	}

	/* Poll until PE is enabled */
	if (handshake(ehci, &hw->port_status[0], PORT_PE,
						 PORT_PE, 2000)) {
		pr_err("%s: timeout waiting for USB_PORTSC1_PE\n", __func__);
		goto restart;
	}

	/* Clear the PCI status, to avoid an interrupt taken upon resume */
	val = readl(&hw->status);
	val |= STS_PCD;
	writel(val, &hw->status);

	/* Put controller in suspend mode by writing 1 to SUSP bit of PORTSC */
	val = readl(&hw->port_status[0]);
	if ((val & PORT_POWER) && (val & PORT_PE)) {
		val |= PORT_SUSPEND;
		writel(val, &hw->port_status[0]);

		/* Wait until port suspend completes */
		if (handshake(ehci, &hw->port_status[0], PORT_SUSPEND,
							 PORT_SUSPEND, 1000)) {
			pr_err("%s: timeout waiting for PORT_SUSPEND\n",
								__func__);
			goto restart;
		}
	}

	tegra_ehci_phy_restore_end(tegra->phy);
	goto done;

 restart:
	if (tegra->port_speed <= TEGRA_USB_PHY_PORT_SPEED_HIGH)
		tegra_ehci_phy_restore_end(tegra->phy);

	tegra_ehci_restart(hcd);

 done:
	tegra_usb_phy_preresume(tegra->phy);
	tegra->port_resuming = 1;
	return 0;
}

static int tegra_ehci_suspend(struct device *dev)
{
	struct tegra_ehci_hcd *tegra =
			platform_get_drvdata(to_platform_device(dev));
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);
	int rc = 0;

	/*
	 * When system sleep is supported and USB controller wakeup is
	 * implemented: If the controller is runtime-suspended and the
	 * wakeup setting needs to be changed, call pm_runtime_resume().
	 */
	if (HCD_HW_ACCESSIBLE(hcd))
		rc = controller_suspend(dev);
	return rc;
}

static int tegra_ehci_resume(struct device *dev)
{
	int rc;

	rc = controller_resume(dev);
	if (rc == 0) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}
	return rc;
}

static int tegra_ehci_runtime_suspend(struct device *dev)
{
	return controller_suspend(dev);
}

static int tegra_ehci_runtime_resume(struct device *dev)
{
	return controller_resume(dev);
}

static const struct dev_pm_ops tegra_ehci_pm_ops = {
	.suspend	= tegra_ehci_suspend,
	.resume		= tegra_ehci_resume,
	.runtime_suspend = tegra_ehci_runtime_suspend,
	.runtime_resume	= tegra_ehci_runtime_resume,
};

#endif

static u64 tegra_ehci_dma_mask = DMA_BIT_MASK(32);

static int tegra_ehci_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct usb_hcd *hcd;
	struct tegra_ehci_hcd *tegra;
	struct tegra_ehci_platform_data *pdata;
	int err = 0;
	int irq;
	int instance = pdev->id;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -EINVAL;
	}

	/* Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &tegra_ehci_dma_mask;

	setup_vbus_gpio(pdev, pdata);

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

	tegra->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(tegra->clk)) {
		dev_err(&pdev->dev, "Can't get ehci clock\n");
		err = PTR_ERR(tegra->clk);
		goto fail_clk;
	}

	err = clk_enable(tegra->clk);
	if (err)
		goto fail_clken;

	tegra->emc_clk = clk_get(&pdev->dev, "emc");
	if (IS_ERR(tegra->emc_clk)) {
		dev_err(&pdev->dev, "Can't get emc clock\n");
		err = PTR_ERR(tegra->emc_clk);
		goto fail_emc_clk;
	}

	clk_enable(tegra->emc_clk);
	clk_set_rate(tegra->emc_clk, 400000000);

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

	/* This is pretty ugly and needs to be fixed when we do only
	 * device-tree probing. Old code relies on the platform_device
	 * numbering that we lack for device-tree-instantiated devices.
	 */
	if (instance < 0) {
		switch (res->start) {
		case TEGRA_USB_BASE:
			instance = 0;
			break;
		case TEGRA_USB2_BASE:
			instance = 1;
			break;
		case TEGRA_USB3_BASE:
			instance = 2;
			break;
		default:
			err = -ENODEV;
			dev_err(&pdev->dev, "unknown usb instance\n");
			goto fail_phy;
		}
	}

	tegra->phy = tegra_usb_phy_open(&pdev->dev, instance, hcd->regs,
					pdata->phy_config,
					TEGRA_USB_PHY_MODE_HOST);
	if (IS_ERR(tegra->phy)) {
		dev_err(&pdev->dev, "Failed to open USB phy\n");
		err = -ENXIO;
		goto fail_phy;
	}

	err = tegra_usb_phy_power_on(tegra->phy);
	if (err) {
		dev_err(&pdev->dev, "Failed to power on the phy\n");
		goto fail;
	}

	tegra->host_resumed = 1;
	tegra->ehci = hcd_to_ehci(hcd);

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail;
	}

#ifdef CONFIG_USB_OTG_UTILS
	if (pdata->operating_mode == TEGRA_USB_OTG) {
		tegra->transceiver = usb_get_transceiver();
		if (tegra->transceiver)
			otg_set_host(tegra->transceiver->otg, &hcd->self);
	}
#endif

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail;
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);

	/* Don't skip the pm_runtime_forbid call if wakeup isn't working */
	/* if (!pdata->power_down_on_bus_suspend) */
		pm_runtime_forbid(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	return err;

fail:
#ifdef CONFIG_USB_OTG_UTILS
	if (tegra->transceiver) {
		otg_set_host(tegra->transceiver->otg, NULL);
		usb_put_transceiver(tegra->transceiver);
	}
#endif
	tegra_usb_phy_close(tegra->phy);
fail_phy:
	iounmap(hcd->regs);
fail_io:
	clk_disable(tegra->emc_clk);
	clk_put(tegra->emc_clk);
fail_emc_clk:
	clk_disable(tegra->clk);
fail_clken:
	clk_put(tegra->clk);
fail_clk:
	usb_put_hcd(hcd);
fail_hcd:
	kfree(tegra);
	return err;
}

static int tegra_ehci_remove(struct platform_device *pdev)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (tegra == NULL || hcd == NULL)
		return -EINVAL;

	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

#ifdef CONFIG_USB_OTG_UTILS
	if (tegra->transceiver) {
		otg_set_host(tegra->transceiver->otg, NULL);
		usb_put_transceiver(tegra->transceiver);
	}
#endif

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	tegra_usb_phy_close(tegra->phy);
	iounmap(hcd->regs);

	clk_disable(tegra->clk);
	clk_put(tegra->clk);

	clk_disable(tegra->emc_clk);
	clk_put(tegra->emc_clk);

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

static struct of_device_id tegra_ehci_of_match[] __devinitdata = {
	{ .compatible = "nvidia,tegra20-ehci", },
	{ },
};

static struct platform_driver tegra_ehci_driver = {
	.probe		= tegra_ehci_probe,
	.remove		= tegra_ehci_remove,
	.shutdown	= tegra_ehci_hcd_shutdown,
	.driver		= {
		.name	= "tegra-ehci",
		.of_match_table = tegra_ehci_of_match,
#ifdef CONFIG_PM
		.pm	= &tegra_ehci_pm_ops,
#endif
	}
};
