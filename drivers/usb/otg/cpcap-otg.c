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
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <mach/usb_phy.h>
#include <mach/legacy_irq.h>

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
	int irq;
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

static irqreturn_t cpcap_otg_irq(int irq, void *data)
{
	if (tegra_legacy_force_irq_status(irq))
		tegra_legacy_force_irq_clr(irq);
	return IRQ_HANDLED;
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

	dev_info(cpcap->otg.dev, "%s --> %s", cpcap_state_name(from),
					      cpcap_state_name(to));

	if ((to == OTG_STATE_A_HOST) && (from == OTG_STATE_A_SUSPEND)
			&& otg->host) {
		hcd = (struct usb_hcd *)otg->host;

		clk_enable(cpcap->clk);

		val = readl(cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		val &= ~TEGRA_ID_SW_VALUE;
		writel(val, cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);

		set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	} else if ((to == OTG_STATE_A_SUSPEND) && (from == OTG_STATE_A_HOST)
			&& otg->host) {
		val = readl(cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		val |= TEGRA_ID_SW_VALUE;
		writel(val, cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);

		clk_disable(cpcap->clk);

	} else if ((to == OTG_STATE_B_PERIPHERAL)
			&& (from == OTG_STATE_A_SUSPEND)
			&& otg->gadget) {
		clk_enable(cpcap->clk);

		val = readl(cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		val |= TEGRA_VBUS_WAKEUP_SW_VALUE;
		writel(val, cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);

	} else if ((to == OTG_STATE_A_SUSPEND)
			&& (from == OTG_STATE_B_PERIPHERAL)
			&& otg->gadget) {
		val = readl(cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		val &= ~TEGRA_VBUS_WAKEUP_SW_VALUE;
		writel(val, cpcap->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);

		clk_disable(cpcap->clk);
	} else
		return 0;

	tegra_legacy_force_irq_set(cpcap->irq);
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

static int __init cpcap_otg_probe(struct platform_device *pdev)
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

	cpcap->irq = platform_get_irq(pdev, 0);
	if (cpcap->irq < 0) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENXIO;
		goto err_irq;
	}

	err = request_irq(cpcap->irq, cpcap_otg_irq, IRQF_SHARED,
			"cpcap-otg", cpcap);
	if (err) {
		dev_err(&pdev->dev, "cannot request irq %d err %d\n",
				cpcap->irq, err);
		goto err_irq;
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
	free_irq(cpcap->irq, cpcap);
err_irq:
	iounmap(cpcap->regs);
err_io:
	clk_disable(cpcap->clk);
err_clken:
	clk_put(cpcap->clk);
err_clk:
	platform_set_drvdata(pdev, NULL);
	kfree(cpcap);
	return err;
}

static int __exit cpcap_otg_remove(struct platform_device *pdev)
{
	struct cpcap_otg_data *cpcap = platform_get_drvdata(pdev);

	otg_set_transceiver(NULL);
	free_irq(cpcap->irq, cpcap);
	iounmap(cpcap->regs);
	clk_disable(cpcap->clk);
	clk_put(cpcap->clk);
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
