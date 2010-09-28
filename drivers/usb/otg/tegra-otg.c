/*
 * drivers/usb/otg/tegra-otg.c
 *
 * OTG transceiver driver for Tegra UTMI phy
 *
 * Copyright (C) 2010 NVIDIA Corp.
 * Copyright (C) 2010 Google, Inc.
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

#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/platform_device.h>
#include <linux/tegra_usb.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>

#define USB_PHY_WAKEUP		0x408
#define  USB_ID_INT_EN		(1 << 0)
#define  USB_ID_INT_STATUS	(1 << 1)
#define  USB_ID_STATUS		(1 << 2)
#define  USB_ID_PIN_WAKEUP_EN	(1 << 6)
#define  USB_VBUS_WAKEUP_EN	(1 << 30)
#define  USB_VBUS_INT_EN	(1 << 8)
#define  USB_VBUS_INT_STATUS	(1 << 9)
#define  USB_VBUS_STATUS	(1 << 10)
#define  USB_INTS		(USB_VBUS_INT_STATUS | USB_ID_INT_STATUS)

struct tegra_otg_data {
	struct otg_transceiver otg;
	unsigned long int_status;
	spinlock_t lock;
	void __iomem *regs;
	struct clk *clk;
	int irq;
	struct platform_device *host;
	struct platform_device *pdev;
};

static inline unsigned long otg_readl(struct tegra_otg_data *tegra,
				      unsigned int offset)
{
	return readl(tegra->regs + offset);
}

static inline void otg_writel(struct tegra_otg_data *tegra, unsigned long val,
			      unsigned int offset)
{
	writel(val, tegra->regs + offset);
}

static const char *tegra_state_name(enum usb_otg_state state)
{
	if (state == OTG_STATE_A_HOST)
		return "HOST";
	if (state == OTG_STATE_B_PERIPHERAL)
		return "PERIPHERAL";
	if (state == OTG_STATE_A_SUSPEND)
		return "SUSPEND";
	return "INVALID";
}

void tegra_start_host(struct tegra_otg_data *tegra)
{
	int retval;
	struct platform_device *pdev;
	struct platform_device *host = tegra->host;
	void *platform_data;

	pdev = platform_device_alloc(host->name, host->id);
	if (!pdev)
		return;

	if (host->resource) {
		retval = platform_device_add_resources(pdev, host->resource,
							host->num_resources);
		if (retval)
			goto error;
	}

	pdev->dev.dma_mask = host->dev.dma_mask;
	pdev->dev.coherent_dma_mask = host->dev.coherent_dma_mask;

	platform_data = kmalloc(sizeof(struct tegra_ehci_platform_data), GFP_KERNEL);
	if (!platform_data)
		goto error;

	memcpy(platform_data, host->dev.platform_data,
				sizeof(struct tegra_ehci_platform_data));
	pdev->dev.platform_data = platform_data;

	retval = platform_device_add(pdev);
	if (retval)
		goto error_add;

	tegra->pdev = pdev;
	return;

error_add:
	kfree(platform_data);
error:
	pr_err("%s: failed to add the host contoller device\n", __func__);
	platform_device_put(pdev);
}

void tegra_stop_host(struct tegra_otg_data *tegra)
{
	if (tegra->pdev) {
		platform_device_unregister(tegra->pdev);
		tegra->pdev = NULL;
	}
}

static irqreturn_t tegra_otg_irq_thread(int irq, void *data)
{
	struct tegra_otg_data *tegra = data;
	struct otg_transceiver *otg = &tegra->otg;
	enum usb_otg_state from = otg->state;
	enum usb_otg_state to = OTG_STATE_UNDEFINED;
	unsigned long flags;
	unsigned long status;

	clk_enable(tegra->clk);

	status = otg_readl(tegra, USB_PHY_WAKEUP);

	spin_lock_irqsave(&tegra->lock, flags);

	if (tegra->int_status & USB_ID_INT_STATUS) {
		if (status & USB_ID_STATUS)
			to = OTG_STATE_A_SUSPEND;
		else
			to = OTG_STATE_A_HOST;
	} else if (tegra->int_status & USB_VBUS_INT_STATUS) {
		if (status & USB_VBUS_STATUS)
			to = OTG_STATE_B_PERIPHERAL;
		else
			to = OTG_STATE_A_SUSPEND;
	}

	tegra->int_status = 0;

	spin_unlock_irqrestore(&tegra->lock, flags);

	otg->state = to;

	dev_info(tegra->otg.dev, "%s --> %s", tegra_state_name(from),
					      tegra_state_name(to));

	if (to == OTG_STATE_A_SUSPEND) {
		if (from == OTG_STATE_A_HOST && tegra->host)
			tegra_stop_host(tegra);
		else if (from == OTG_STATE_B_PERIPHERAL && otg->gadget)
			usb_gadget_vbus_disconnect(otg->gadget);
	} else if (to == OTG_STATE_B_PERIPHERAL && otg->gadget) {
		if (from == OTG_STATE_A_SUSPEND)
			usb_gadget_vbus_connect(otg->gadget);
	} else if (to == OTG_STATE_A_HOST && tegra->host) {
		if (from == OTG_STATE_A_SUSPEND)
			tegra_start_host(tegra);
	}

	clk_disable(tegra->clk);

	return IRQ_HANDLED;

}

static irqreturn_t tegra_otg_irq(int irq, void *data)
{
	struct tegra_otg_data *tegra = data;
	unsigned long val;

	clk_enable(tegra->clk);

	spin_lock(&tegra->lock);
	val = otg_readl(tegra, USB_PHY_WAKEUP);
	otg_writel(tegra, val, USB_PHY_WAKEUP);

	/* and the interrupt enables into the interrupt status bits */
	val = (val & (val << 1)) & USB_INTS;

	tegra->int_status |= val;

	spin_unlock(&tegra->lock);

	clk_disable(tegra->clk);

	return (val) ? IRQ_WAKE_THREAD : IRQ_NONE;
}

static int tegra_otg_set_peripheral(struct otg_transceiver *otg,
				struct usb_gadget *gadget)
{
	struct tegra_otg_data *tegra;
	unsigned long val;

	tegra = container_of(otg, struct tegra_otg_data, otg);
	otg->gadget = gadget;

	clk_enable(tegra->clk);
	val = otg_readl(tegra, USB_PHY_WAKEUP);
	val &= ~(USB_VBUS_INT_STATUS | USB_ID_INT_STATUS);

	if (gadget)
		val |= (USB_VBUS_INT_EN | USB_VBUS_WAKEUP_EN);
	else
		val &= ~(USB_VBUS_INT_EN | USB_VBUS_WAKEUP_EN);

	otg_writel(tegra, val, USB_PHY_WAKEUP);
	clk_disable(tegra->clk);

	return 0;
}

static int tegra_otg_set_host(struct otg_transceiver *otg,
				struct usb_bus *host)
{
	struct tegra_otg_data *tegra;
	unsigned long val;

	tegra = container_of(otg, struct tegra_otg_data, otg);
	otg->host = host;

	clk_enable(tegra->clk);
	val = otg_readl(tegra, USB_PHY_WAKEUP);
	val &= ~(USB_VBUS_INT_STATUS | USB_ID_INT_STATUS);

	if (host)
		val |= USB_ID_INT_EN | USB_ID_PIN_WAKEUP_EN;
	else
		val &= ~(USB_ID_INT_EN | USB_ID_PIN_WAKEUP_EN);
	otg_writel(tegra, val, USB_PHY_WAKEUP);
	clk_disable(tegra->clk);

	return 0;
}

static int tegra_otg_set_power(struct otg_transceiver *otg, unsigned mA)
{
	return 0;
}

static int tegra_otg_set_suspend(struct otg_transceiver *otg, int suspend)
{
	return 0;
}

static int tegra_otg_probe(struct platform_device *pdev)
{
	struct tegra_otg_data *tegra;
	struct resource *res;
	unsigned long val;
	int err;

	tegra = kzalloc(sizeof(struct tegra_otg_data), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	tegra->otg.dev = &pdev->dev;
	tegra->otg.label = "tegra-otg";
	tegra->otg.state = OTG_STATE_UNDEFINED;
	tegra->otg.set_host = tegra_otg_set_host;
	tegra->otg.set_peripheral = tegra_otg_set_peripheral;
	tegra->otg.set_suspend = tegra_otg_set_suspend;
	tegra->otg.set_power = tegra_otg_set_power;
	tegra->host = pdev->dev.platform_data;
	spin_lock_init(&tegra->lock);

	platform_set_drvdata(pdev, tegra);

	tegra->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(tegra->clk)) {
		dev_err(&pdev->dev, "Can't get otg clock\n");
		err = PTR_ERR(tegra->clk);
		goto err_clk;
	}

	err = clk_enable(tegra->clk);
	if (err)
		goto err_clken;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto err_io;
	}
	tegra->regs = ioremap(res->start, resource_size(res));
	if (!tegra->regs) {
		err = -ENOMEM;
		goto err_io;
	}

	val = otg_readl(tegra, USB_PHY_WAKEUP);

	val &= ~(USB_VBUS_INT_STATUS | USB_VBUS_INT_EN |
		 USB_ID_INT_STATUS | USB_ID_INT_EN |
		 USB_VBUS_WAKEUP_EN | USB_ID_PIN_WAKEUP_EN);

	otg_writel(tegra, val, USB_PHY_WAKEUP);

	tegra->otg.state = OTG_STATE_A_SUSPEND;

	err = otg_set_transceiver(&tegra->otg);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver (%d)\n", err);
		goto err_otg;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENXIO;
		goto err_irq;
	}
	tegra->irq = res->start;
	err = request_threaded_irq(tegra->irq, tegra_otg_irq,
				   tegra_otg_irq_thread,
				   IRQF_SHARED, "tegra-otg", tegra);
	if (err) {
		dev_err(&pdev->dev, "Failed to register IRQ\n");
		goto err_irq;
	}

	dev_info(&pdev->dev, "otg transceiver registered\n");
	return 0;

err_irq:
	otg_set_transceiver(NULL);
err_otg:
	iounmap(tegra->regs);
err_io:
	clk_disable(tegra->clk);
err_clken:
	clk_put(tegra->clk);
err_clk:
	platform_set_drvdata(pdev, NULL);
	kfree(tegra);
	return err;
}

static int __exit tegra_otg_remove(struct platform_device *pdev)
{
	struct tegra_otg_data *tegra = platform_get_drvdata(pdev);

	free_irq(tegra->irq, tegra);
	otg_set_transceiver(NULL);
	iounmap(tegra->regs);
	clk_disable(tegra->clk);
	clk_put(tegra->clk);
	platform_set_drvdata(pdev, NULL);
	kfree(tegra);

	return 0;
}

static struct platform_driver tegra_otg_driver = {
	.driver = {
		.name  = "tegra-otg",
	},
	.remove  = __exit_p(tegra_otg_remove),
	.probe   = tegra_otg_probe,
};

static int __init tegra_otg_init(void)
{
	return platform_driver_register(&tegra_otg_driver);
}
subsys_initcall(tegra_otg_init);

static void __exit tegra_otg_exit(void)
{
	platform_driver_unregister(&tegra_otg_driver);
}
module_exit(tegra_otg_exit);
