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
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/irq.h>
#include <linux/usb/otg.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/usb/ehci_def.h>
#include <linux/usb/tegra_usb_phy.h>
#include <linux/clk/tegra.h>

#define TEGRA_USB_BASE			0xC5000000
#define TEGRA_USB2_BASE			0xC5004000
#define TEGRA_USB3_BASE			0xC5008000

/* PORTSC registers */
#define TEGRA_USB_PORTSC1			0x184
#define TEGRA_USB_PORTSC1_PTS(x)	(((x) & 0x3) << 30)
#define TEGRA_USB_PORTSC1_PHCD	(1 << 23)

#define TEGRA_USB_DMA_ALIGN 32

struct tegra_ehci_hcd {
	struct ehci_hcd *ehci;
	struct tegra_usb_phy *phy;
	struct clk *clk;
	struct usb_phy *transceiver;
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

static int tegra_ehci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	/* EHCI registers start at offset 0x100 */
	ehci->caps = hcd->regs + 0x100;

	/* switch to host mode */
	hcd->has_tt = 1;

	return ehci_setup(hcd);
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
	.shutdown		= ehci_shutdown,
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

/* Bits of PORTSC1, which will get cleared by writing 1 into them */
#define TEGRA_PORTSC1_RWC_BITS (PORT_CSC | PORT_PEC | PORT_OCC)

void tegra_ehci_set_pts(struct usb_phy *x, u8 pts_val)
{
	unsigned long val;
	struct usb_hcd *hcd = bus_to_hcd(x->otg->host);
	void __iomem *base = hcd->regs;

	val = readl(base + TEGRA_USB_PORTSC1) & ~TEGRA_PORTSC1_RWC_BITS;
	val &= ~TEGRA_USB_PORTSC1_PTS(3);
	val |= TEGRA_USB_PORTSC1_PTS(pts_val & 3);
	writel(val, base + TEGRA_USB_PORTSC1);
}
EXPORT_SYMBOL_GPL(tegra_ehci_set_pts);

void tegra_ehci_set_phcd(struct usb_phy *x, bool enable)
{
	unsigned long val;
	struct usb_hcd *hcd = bus_to_hcd(x->otg->host);
	void __iomem *base = hcd->regs;

	val = readl(base + TEGRA_USB_PORTSC1) & ~TEGRA_PORTSC1_RWC_BITS;
	if (enable)
		val |= TEGRA_USB_PORTSC1_PHCD;
	else
		val &= ~TEGRA_USB_PORTSC1_PHCD;
	writel(val, base + TEGRA_USB_PORTSC1);
}
EXPORT_SYMBOL_GPL(tegra_ehci_set_phcd);

static int tegra_ehci_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct usb_hcd *hcd;
	struct tegra_ehci_hcd *tegra;
	struct tegra_ehci_platform_data *pdata;
	int err = 0;
	int irq;
	struct device_node *np_phy;
	struct usb_phy *u_phy;

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
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	setup_vbus_gpio(pdev, pdata);

	tegra = devm_kzalloc(&pdev->dev, sizeof(struct tegra_ehci_hcd),
			     GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	tegra->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tegra->clk)) {
		dev_err(&pdev->dev, "Can't get ehci clock\n");
		return PTR_ERR(tegra->clk);
	}

	err = clk_prepare_enable(tegra->clk);
	if (err)
		return err;

	tegra_periph_reset_assert(tegra->clk);
	udelay(1);
	tegra_periph_reset_deassert(tegra->clk);

	np_phy = of_parse_phandle(pdev->dev.of_node, "nvidia,phy", 0);
	if (!np_phy) {
		err = -ENODEV;
		goto cleanup_clk;
	}

	u_phy = tegra_usb_get_phy(np_phy);
	if (IS_ERR(u_phy)) {
		err = PTR_ERR(u_phy);
		goto cleanup_clk;
	}

	tegra->needs_double_reset = of_property_read_bool(pdev->dev.of_node,
		"nvidia,needs-double-reset");

	hcd = usb_create_hcd(&tegra_ehci_hc_driver, &pdev->dev,
					dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		err = -ENOMEM;
		goto cleanup_clk;
	}
	hcd->phy = u_phy;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto cleanup_hcd_create;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto cleanup_hcd_create;
	}

	err = usb_phy_init(hcd->phy);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize phy\n");
		goto cleanup_hcd_create;
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

	tegra->ehci = hcd_to_ehci(hcd);

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto cleanup_phy;
	}

	if (pdata->operating_mode == TEGRA_USB_OTG) {
		tegra->transceiver =
			devm_usb_get_phy(&pdev->dev, USB_PHY_TYPE_USB2);
		if (!IS_ERR(tegra->transceiver))
			otg_set_host(tegra->transceiver->otg, &hcd->self);
	} else {
		tegra->transceiver = ERR_PTR(-ENODEV);
	}

	platform_set_drvdata(pdev, tegra);

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto cleanup_phy;
	}

	return err;

cleanup_phy:
	if (!IS_ERR(tegra->transceiver))
		otg_set_host(tegra->transceiver->otg, NULL);

	usb_phy_shutdown(hcd->phy);
cleanup_hcd_create:
	usb_put_hcd(hcd);
cleanup_clk:
	clk_disable_unprepare(tegra->clk);
	return err;
}

static int tegra_ehci_remove(struct platform_device *pdev)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (!IS_ERR(tegra->transceiver))
		otg_set_host(tegra->transceiver->otg, NULL);

	usb_phy_shutdown(hcd->phy);
	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	clk_disable_unprepare(tegra->clk);

	return 0;
}

static void tegra_ehci_hcd_shutdown(struct platform_device *pdev)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct of_device_id tegra_ehci_of_match[] = {
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
	}
};
