/*
 * EHCI-compliant USB host controller driver for NVIDIA Tegra SoCs
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2009 - 2013 NVIDIA Corporation
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
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/usb/ehci_def.h>
#include <linux/usb/tegra_usb_phy.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/otg.h>

#include "ehci.h"

#define PORT_WAKE_BITS (PORT_WKOC_E|PORT_WKDISC_E|PORT_WKCONN_E)

#define TEGRA_USB_DMA_ALIGN 32

#define DRIVER_DESC "Tegra EHCI driver"
#define DRV_NAME "tegra-ehci"

static struct hc_driver __read_mostly tegra_ehci_hc_driver;

struct tegra_ehci_soc_config {
	bool has_hostpc;
};

struct tegra_ehci_hcd {
	struct tegra_usb_phy *phy;
	struct clk *clk;
	struct reset_control *rst;
	int port_resuming;
	bool needs_double_reset;
	enum tegra_usb_phy_port_speed port_speed;
};

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
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct tegra_ehci_hcd *tegra = (struct tegra_ehci_hcd *)ehci->priv;
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
			tegra_usb_phy_postresume(hcd->phy);
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
		if (ehci_handshake(ehci, status_reg, PORT_SUSPEND,
						PORT_SUSPEND, 5000))
			pr_err("%s: timeout waiting for SUSPEND\n", __func__);

		set_bit((wIndex & 0xff) - 1, &ehci->suspended_ports);
		goto done;
	}

	/* For USB1 port we need to issue Port Reset twice internally */
	if (tegra->needs_double_reset &&
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
		tegra_usb_phy_preresume(hcd->phy);

		ehci->reset_done[wIndex-1] = jiffies + msecs_to_jiffies(25);

		temp &= ~(PORT_RWC_BITS | PORT_WAKE_BITS);
		/* start resume signalling */
		ehci_writel(ehci, temp | PORT_RESUME, status_reg);
		set_bit(wIndex-1, &ehci->resuming_ports);

		spin_unlock_irqrestore(&ehci->lock, flags);
		msleep(20);
		spin_lock_irqsave(&ehci->lock, flags);

		/* Poll until the controller clears RESUME and SUSPEND */
		if (ehci_handshake(ehci, status_reg, PORT_RESUME, 0, 2000))
			pr_err("%s: timeout waiting for RESUME\n", __func__);
		if (ehci_handshake(ehci, status_reg, PORT_SUSPEND, 0, 2000))
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

static const struct tegra_ehci_soc_config tegra30_soc_config = {
	.has_hostpc = true,
};

static const struct tegra_ehci_soc_config tegra20_soc_config = {
	.has_hostpc = false,
};

static const struct of_device_id tegra_ehci_of_match[] = {
	{ .compatible = "nvidia,tegra30-ehci", .data = &tegra30_soc_config },
	{ .compatible = "nvidia,tegra20-ehci", .data = &tegra20_soc_config },
	{ },
};

static int tegra_ehci_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct tegra_ehci_soc_config *soc_config;
	struct resource *res;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct tegra_ehci_hcd *tegra;
	int err = 0;
	int irq;
	struct usb_phy *u_phy;

	match = of_match_device(tegra_ehci_of_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Error: No device match found\n");
		return -ENODEV;
	}
	soc_config = match->data;

	/* Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	err = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	hcd = usb_create_hcd(&tegra_ehci_hc_driver, &pdev->dev,
					dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, hcd);
	ehci = hcd_to_ehci(hcd);
	tegra = (struct tegra_ehci_hcd *)ehci->priv;

	hcd->has_tt = 1;

	tegra->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tegra->clk)) {
		dev_err(&pdev->dev, "Can't get ehci clock\n");
		err = PTR_ERR(tegra->clk);
		goto cleanup_hcd_create;
	}

	tegra->rst = devm_reset_control_get(&pdev->dev, "usb");
	if (IS_ERR(tegra->rst)) {
		dev_err(&pdev->dev, "Can't get ehci reset\n");
		err = PTR_ERR(tegra->rst);
		goto cleanup_hcd_create;
	}

	err = clk_prepare_enable(tegra->clk);
	if (err)
		goto cleanup_hcd_create;

	reset_control_assert(tegra->rst);
	udelay(1);
	reset_control_deassert(tegra->rst);

	u_phy = devm_usb_get_phy_by_phandle(&pdev->dev, "nvidia,phy", 0);
	if (IS_ERR(u_phy)) {
		err = PTR_ERR(u_phy);
		goto cleanup_clk_en;
	}
	hcd->phy = u_phy;

	tegra->needs_double_reset = of_property_read_bool(pdev->dev.of_node,
		"nvidia,needs-double-reset");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto cleanup_clk_en;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto cleanup_clk_en;
	}
	ehci->caps = hcd->regs + 0x100;
	ehci->has_hostpc = soc_config->has_hostpc;

	err = usb_phy_init(hcd->phy);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize phy\n");
		goto cleanup_clk_en;
	}

	u_phy->otg = devm_kzalloc(&pdev->dev, sizeof(struct usb_otg),
			     GFP_KERNEL);
	if (!u_phy->otg) {
		dev_err(&pdev->dev, "Failed to alloc memory for otg\n");
		err = -ENOMEM;
		goto cleanup_phy;
	}
	u_phy->otg->host = hcd_to_bus(hcd);

	err = usb_phy_set_suspend(hcd->phy, 0);
	if (err) {
		dev_err(&pdev->dev, "Failed to power on the phy\n");
		goto cleanup_phy;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto cleanup_phy;
	}

	otg_set_host(u_phy->otg, &hcd->self);

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto cleanup_otg_set_host;
	}
	device_wakeup_enable(hcd->self.controller);

	return err;

cleanup_otg_set_host:
	otg_set_host(u_phy->otg, NULL);
cleanup_phy:
	usb_phy_shutdown(hcd->phy);
cleanup_clk_en:
	clk_disable_unprepare(tegra->clk);
cleanup_hcd_create:
	usb_put_hcd(hcd);
	return err;
}

static int tegra_ehci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct tegra_ehci_hcd *tegra =
		(struct tegra_ehci_hcd *)hcd_to_ehci(hcd)->priv;

	otg_set_host(hcd->phy->otg, NULL);

	usb_phy_shutdown(hcd->phy);
	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	clk_disable_unprepare(tegra->clk);

	return 0;
}

static void tegra_ehci_hcd_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct platform_driver tegra_ehci_driver = {
	.probe		= tegra_ehci_probe,
	.remove		= tegra_ehci_remove,
	.shutdown	= tegra_ehci_hcd_shutdown,
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = tegra_ehci_of_match,
	}
};

static int tegra_ehci_reset(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;
	int txfifothresh;

	retval = ehci_setup(hcd);
	if (retval)
		return retval;

	/*
	 * We should really pull this value out of tegra_ehci_soc_config, but
	 * to avoid needing access to it, make use of the fact that Tegra20 is
	 * the only one so far that needs a value of 10, and Tegra20 is the
	 * only one which doesn't set has_hostpc.
	 */
	txfifothresh = ehci->has_hostpc ? 0x10 : 10;
	ehci_writel(ehci, txfifothresh << 16, &ehci->regs->txfill_tuning);

	return 0;
}

static const struct ehci_driver_overrides tegra_overrides __initconst = {
	.extra_priv_size	= sizeof(struct tegra_ehci_hcd),
	.reset			= tegra_ehci_reset,
};

static int __init ehci_tegra_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info(DRV_NAME ": " DRIVER_DESC "\n");

	ehci_init_driver(&tegra_ehci_hc_driver, &tegra_overrides);

	/*
	 * The Tegra HW has some unusual quirks, which require Tegra-specific
	 * workarounds. We override certain hc_driver functions here to
	 * achieve that. We explicitly do not enhance ehci_driver_overrides to
	 * allow this more easily, since this is an unusual case, and we don't
	 * want to encourage others to override these functions by making it
	 * too easy.
	 */

	tegra_ehci_hc_driver.map_urb_for_dma = tegra_ehci_map_urb_for_dma;
	tegra_ehci_hc_driver.unmap_urb_for_dma = tegra_ehci_unmap_urb_for_dma;
	tegra_ehci_hc_driver.hub_control = tegra_ehci_hub_control;

	return platform_driver_register(&tegra_ehci_driver);
}
module_init(ehci_tegra_init);

static void __exit ehci_tegra_cleanup(void)
{
	platform_driver_unregister(&tegra_ehci_driver);
}
module_exit(ehci_tegra_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra_ehci_of_match);
