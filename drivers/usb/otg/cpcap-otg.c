/*
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
#include <linux/wakelock.h>

#define TEGRA_USB_PHY_WAKEUP_REG_OFFSET		0x408
#define   TEGRA_VBUS_WAKEUP_SW_VALUE		(1 << 12)
#define   TEGRA_VBUS_WAKEUP_SW_ENABLE		(1 << 11)
#define   TEGRA_ID_SW_VALUE			(1 << 4)
#define   TEGRA_ID_SW_ENABLE			(1 << 3)

struct cpcap_otg_data {
	struct otg_transceiver otg;
	struct notifier_block nb;
	void __iomem *regs;
	struct clk *clk;
	struct platform_device *host;
	struct platform_device *pdev;
	struct wake_lock wake_lock;
};

static const char *cpcap_state_name(enum usb_otg_state state)
{
	if (state == OTG_STATE_A_HOST)
		return "HOST";
	if (state == OTG_STATE_B_PERIPHERAL)
		return "PERIPHERAL";
	if (state == OTG_STATE_A_SUSPEND)
		return "SUSPEND";
	return "INVALID";
}

void cpcap_start_host(struct cpcap_otg_data *cpcap)
{
	int retval;
	struct platform_device *pdev;
	struct platform_device *host = cpcap->host;
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

	cpcap->pdev = pdev;
	return;

error_add:
	kfree(platform_data);
error:
	pr_err("%s: failed to add the host contoller device\n", __func__);
	platform_device_put(pdev);
}

void cpcap_stop_host(struct cpcap_otg_data *cpcap)
{
	if (cpcap->pdev) {
		platform_device_unregister(cpcap->pdev);
		cpcap->pdev = NULL;
	}
}

static int cpcap_otg_notify(struct notifier_block *nb, unsigned long event,
			    void *ignore)
{
	struct cpcap_otg_data *cpcap;
	struct otg_transceiver *otg;
	enum usb_otg_state from;
	enum usb_otg_state to;
	unsigned long val;
	struct usb_hcd *hcd;

	cpcap = container_of(nb, struct cpcap_otg_data, nb);
	otg = &cpcap->otg;

	from = otg->state;
	if (event == USB_EVENT_VBUS)
		to = OTG_STATE_B_PERIPHERAL;
	else if (event == USB_EVENT_ID)
		to = OTG_STATE_A_HOST;
	else
		to = OTG_STATE_A_SUSPEND;

	if (from == to)
		return 0;
	otg->state = to;

	if (to == OTG_STATE_B_PERIPHERAL)
		wake_lock(&cpcap->wake_lock);

	dev_info(cpcap->otg.dev, "%s --> %s", cpcap_state_name(from),
					      cpcap_state_name(to));

	clk_enable(cpcap->clk);

	if ((to == OTG_STATE_A_HOST) && (from == OTG_STATE_A_SUSPEND)
			&& cpcap->host) {
		hcd = (struct usb_hcd *)otg->host;

		val = readl(cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		val &= ~TEGRA_ID_SW_VALUE;
		writel(val, cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);

		cpcap_start_host(cpcap);

	} else if ((to == OTG_STATE_A_SUSPEND) && (from == OTG_STATE_A_HOST)
			&& cpcap->host) {
		val = readl(cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		val |= TEGRA_ID_SW_VALUE;
		writel(val, cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);

		cpcap_stop_host(cpcap);

	} else if ((to == OTG_STATE_B_PERIPHERAL)
			&& (from == OTG_STATE_A_SUSPEND)
			&& otg->gadget) {
		val = readl(cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		val |= TEGRA_VBUS_WAKEUP_SW_VALUE;
		writel(val, cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);

		usb_gadget_vbus_connect(otg->gadget);

	} else if ((to == OTG_STATE_A_SUSPEND)
			&& (from == OTG_STATE_B_PERIPHERAL)
			&& otg->gadget) {
		val = readl(cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		val &= ~TEGRA_VBUS_WAKEUP_SW_VALUE;
		writel(val, cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);

		usb_gadget_vbus_disconnect(otg->gadget);
	}

	clk_disable(cpcap->clk);

	if (to == OTG_STATE_A_SUSPEND)
		wake_unlock(&cpcap->wake_lock);

	return 0;
}

static int cpcap_otg_set_peripheral(struct otg_transceiver *otg,
				struct usb_gadget *gadget)
{
	otg->gadget = gadget;
	return 0;
}

static int cpcap_otg_set_host(struct otg_transceiver *otg,
				struct usb_bus *host)
{
	otg->host = host;
	return 0;
}

static int cpcap_otg_set_power(struct otg_transceiver *otg, unsigned mA)
{
	return 0;
}

static int cpcap_otg_set_suspend(struct otg_transceiver *otg, int suspend)
{
	return 0;
}

static int cpcap_otg_probe(struct platform_device *pdev)
{
	struct cpcap_otg_data *cpcap;
	struct resource *res;
	unsigned long val;
	int err;

	cpcap = kzalloc(sizeof(struct cpcap_otg_data), GFP_KERNEL);
	if (!cpcap)
		return -ENOMEM;

	cpcap->otg.dev = &pdev->dev;
	cpcap->otg.label = "cpcap-otg";
	cpcap->otg.state = OTG_STATE_UNDEFINED;
	cpcap->otg.set_host = cpcap_otg_set_host;
	cpcap->otg.set_peripheral = cpcap_otg_set_peripheral;
	cpcap->otg.set_suspend = cpcap_otg_set_suspend;
	cpcap->otg.set_power = cpcap_otg_set_power;
	cpcap->host = pdev->dev.platform_data;
	wake_lock_init(&cpcap->wake_lock, WAKE_LOCK_SUSPEND, "cpcap_otg");

	platform_set_drvdata(pdev, cpcap);

	cpcap->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(cpcap->clk)) {
		dev_err(&pdev->dev, "Can't get otg clock\n");
		err = PTR_ERR(cpcap->clk);
		goto err_clk;
	}

	err = clk_enable(cpcap->clk);
	if (err)
		goto err_clken;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto err_io;
	}
	cpcap->regs = ioremap(res->start, resource_size(res));
	if (!cpcap->regs) {
		err = -ENOMEM;
		goto err_io;
	}

	val = readl(cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
	val |= TEGRA_VBUS_WAKEUP_SW_ENABLE | TEGRA_ID_SW_ENABLE;
	val |= TEGRA_ID_SW_VALUE;
	val &= ~(TEGRA_VBUS_WAKEUP_SW_VALUE);
	writel(val, cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);

	clk_disable(cpcap->clk);
	cpcap->otg.state = OTG_STATE_A_SUSPEND;

	BLOCKING_INIT_NOTIFIER_HEAD(&cpcap->otg.notifier);
	cpcap->nb.notifier_call = cpcap_otg_notify;
	otg_register_notifier(&cpcap->otg, &cpcap->nb);

	err = otg_set_transceiver(&cpcap->otg);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver (%d)\n", err);
		goto err_otg;
	}

	return 0;

err_otg:
	iounmap(cpcap->regs);
err_io:
	clk_disable(cpcap->clk);
err_clken:
	clk_put(cpcap->clk);
err_clk:
	wake_lock_destroy(&cpcap->wake_lock);
	platform_set_drvdata(pdev, NULL);
	kfree(cpcap);
	return err;
}

static int __exit cpcap_otg_remove(struct platform_device *pdev)
{
	struct cpcap_otg_data *cpcap = platform_get_drvdata(pdev);

	otg_set_transceiver(NULL);
	iounmap(cpcap->regs);
	clk_disable(cpcap->clk);
	clk_put(cpcap->clk);
	wake_lock_destroy(&cpcap->wake_lock);
	platform_set_drvdata(pdev, NULL);
	kfree(cpcap);

	return 0;
}

static struct platform_driver cpcap_otg_driver = {
	.driver = {
		.name  = "cpcap-otg",
	},
	.remove  = __exit_p(cpcap_otg_remove),
	.probe   = cpcap_otg_probe,
};

static int __init cpcap_otg_init(void)
{
	return platform_driver_register(&cpcap_otg_driver);
}
module_init(cpcap_otg_init);

static void __exit cpcap_otg_exit(void)
{
	platform_driver_unregister(&cpcap_otg_driver);
}
module_exit(cpcap_otg_exit);
