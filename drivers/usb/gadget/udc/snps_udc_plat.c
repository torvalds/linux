// SPDX-License-Identifier: GPL-2.0
/*
 * snps_udc_plat.c - Synopsys UDC Platform Driver
 *
 * Copyright (C) 2016 Broadcom
 */

#include <linux/extcon.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/module.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include "amd5536udc.h"

/* description */
#define UDC_MOD_DESCRIPTION     "Synopsys UDC platform driver"

static void start_udc(struct udc *udc)
{
	if (udc->driver) {
		dev_info(udc->dev, "Connecting...\n");
		udc_enable_dev_setup_interrupts(udc);
		udc_basic_init(udc);
		udc->connected = 1;
	}
}

static void stop_udc(struct udc *udc)
{
	int tmp;
	u32 reg;

	spin_lock(&udc->lock);

	/* Flush the receieve fifo */
	reg = readl(&udc->regs->ctl);
	reg |= AMD_BIT(UDC_DEVCTL_SRX_FLUSH);
	writel(reg, &udc->regs->ctl);

	reg = readl(&udc->regs->ctl);
	reg &= ~(AMD_BIT(UDC_DEVCTL_SRX_FLUSH));
	writel(reg, &udc->regs->ctl);
	dev_dbg(udc->dev, "ep rx queue flushed\n");

	/* Mask interrupts. Required more so when the
	 * UDC is connected to a DRD phy.
	 */
	udc_mask_unused_interrupts(udc);

	/* Disconnect gadget driver */
	if (udc->driver) {
		spin_unlock(&udc->lock);
		udc->driver->disconnect(&udc->gadget);
		spin_lock(&udc->lock);

		/* empty queues */
		for (tmp = 0; tmp < UDC_EP_NUM; tmp++)
			empty_req_queue(&udc->ep[tmp]);
	}
	udc->connected = 0;

	spin_unlock(&udc->lock);
	dev_info(udc->dev, "Device disconnected\n");
}

static void udc_drd_work(struct work_struct *work)
{
	struct udc *udc;

	udc = container_of(to_delayed_work(work),
			   struct udc, drd_work);

	if (udc->conn_type) {
		dev_dbg(udc->dev, "idle -> device\n");
		start_udc(udc);
	} else {
		dev_dbg(udc->dev, "device -> idle\n");
		stop_udc(udc);
	}
}

static int usbd_connect_notify(struct notifier_block *self,
			       unsigned long event, void *ptr)
{
	struct udc *udc = container_of(self, struct udc, nb);

	dev_dbg(udc->dev, "%s: event: %lu\n", __func__, event);

	udc->conn_type = event;

	schedule_delayed_work(&udc->drd_work, 0);

	return NOTIFY_OK;
}

static int udc_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct udc *udc;
	int ret;

	udc = devm_kzalloc(dev, sizeof(*udc), GFP_KERNEL);
	if (!udc)
		return -ENOMEM;

	spin_lock_init(&udc->lock);
	udc->dev = dev;

	udc->virt_addr = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(udc->virt_addr))
		return PTR_ERR(udc->virt_addr);

	/* udc csr registers base */
	udc->csr = udc->virt_addr + UDC_CSR_ADDR;

	/* dev registers base */
	udc->regs = udc->virt_addr + UDC_DEVCFG_ADDR;

	/* ep registers base */
	udc->ep_regs = udc->virt_addr + UDC_EPREGS_ADDR;

	/* fifo's base */
	udc->rxfifo = (u32 __iomem *)(udc->virt_addr + UDC_RXFIFO_ADDR);
	udc->txfifo = (u32 __iomem *)(udc->virt_addr + UDC_TXFIFO_ADDR);

	udc->phys_addr = (unsigned long)res->start;

	udc->irq = irq_of_parse_and_map(dev->of_node, 0);
	if (udc->irq <= 0) {
		dev_err(dev, "Can't parse and map interrupt\n");
		return -EINVAL;
	}

	udc->udc_phy = devm_of_phy_get_by_index(dev, dev->of_node, 0);
	if (IS_ERR(udc->udc_phy)) {
		dev_err(dev, "Failed to obtain phy from device tree\n");
		return PTR_ERR(udc->udc_phy);
	}

	ret = phy_init(udc->udc_phy);
	if (ret) {
		dev_err(dev, "UDC phy init failed");
		return ret;
	}

	ret = phy_power_on(udc->udc_phy);
	if (ret) {
		dev_err(dev, "UDC phy power on failed");
		phy_exit(udc->udc_phy);
		return ret;
	}

	/* Register for extcon if supported */
	if (of_property_present(dev->of_node, "extcon")) {
		udc->edev = extcon_get_edev_by_phandle(dev, 0);
		if (IS_ERR(udc->edev)) {
			if (PTR_ERR(udc->edev) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			dev_err(dev, "Invalid or missing extcon\n");
			ret = PTR_ERR(udc->edev);
			goto exit_phy;
		}

		udc->nb.notifier_call = usbd_connect_notify;
		ret = extcon_register_notifier(udc->edev, EXTCON_USB,
					       &udc->nb);
		if (ret < 0) {
			dev_err(dev, "Can't register extcon device\n");
			goto exit_phy;
		}

		ret = extcon_get_state(udc->edev, EXTCON_USB);
		if (ret < 0) {
			dev_err(dev, "Can't get cable state\n");
			goto exit_extcon;
		} else if (ret) {
			udc->conn_type = ret;
		}
		INIT_DELAYED_WORK(&udc->drd_work, udc_drd_work);
	}

	/* init dma pools */
	if (use_dma) {
		ret = init_dma_pools(udc);
		if (ret != 0)
			goto exit_extcon;
	}

	ret = devm_request_irq(dev, udc->irq, udc_irq, IRQF_SHARED,
			       "snps-udc", udc);
	if (ret < 0) {
		dev_err(dev, "Request irq %d failed for UDC\n", udc->irq);
		goto exit_dma;
	}

	platform_set_drvdata(pdev, udc);
	udc->chiprev = UDC_BCM_REV;

	if (udc_probe(udc)) {
		ret = -ENODEV;
		goto exit_dma;
	}
	dev_info(dev, "Synopsys UDC platform driver probe successful\n");

	return 0;

exit_dma:
	if (use_dma)
		free_dma_pools(udc);
exit_extcon:
	if (udc->edev)
		extcon_unregister_notifier(udc->edev, EXTCON_USB, &udc->nb);
exit_phy:
	if (udc->udc_phy) {
		phy_power_off(udc->udc_phy);
		phy_exit(udc->udc_phy);
	}
	return ret;
}

static void udc_plat_remove(struct platform_device *pdev)
{
	struct udc *dev;

	dev = platform_get_drvdata(pdev);

	usb_del_gadget_udc(&dev->gadget);
	/* gadget driver must not be registered */
	if (WARN_ON(dev->driver))
		return;

	/* dma pool cleanup */
	free_dma_pools(dev);

	udc_remove(dev);

	platform_set_drvdata(pdev, NULL);

	phy_power_off(dev->udc_phy);
	phy_exit(dev->udc_phy);
	extcon_unregister_notifier(dev->edev, EXTCON_USB, &dev->nb);

	dev_info(&pdev->dev, "Synopsys UDC platform driver removed\n");
}

#ifdef CONFIG_PM_SLEEP
static int udc_plat_suspend(struct device *dev)
{
	struct udc *udc;

	udc = dev_get_drvdata(dev);
	stop_udc(udc);

	if (extcon_get_state(udc->edev, EXTCON_USB) > 0) {
		dev_dbg(udc->dev, "device -> idle\n");
		stop_udc(udc);
	}
	phy_power_off(udc->udc_phy);
	phy_exit(udc->udc_phy);

	return 0;
}

static int udc_plat_resume(struct device *dev)
{
	struct udc *udc;
	int ret;

	udc = dev_get_drvdata(dev);

	ret = phy_init(udc->udc_phy);
	if (ret) {
		dev_err(udc->dev, "UDC phy init failure");
		return ret;
	}

	ret = phy_power_on(udc->udc_phy);
	if (ret) {
		dev_err(udc->dev, "UDC phy power on failure");
		phy_exit(udc->udc_phy);
		return ret;
	}

	if (extcon_get_state(udc->edev, EXTCON_USB) > 0) {
		dev_dbg(udc->dev, "idle -> device\n");
		start_udc(udc);
	}

	return 0;
}
static const struct dev_pm_ops udc_plat_pm_ops = {
	.suspend	= udc_plat_suspend,
	.resume		= udc_plat_resume,
};
#endif

#if defined(CONFIG_OF)
static const struct of_device_id of_udc_match[] = {
	{ .compatible = "brcm,ns2-udc", },
	{ .compatible = "brcm,cygnus-udc", },
	{ .compatible = "brcm,iproc-udc", },
	{ }
};
MODULE_DEVICE_TABLE(of, of_udc_match);
#endif

static struct platform_driver udc_plat_driver = {
	.probe		= udc_plat_probe,
	.remove_new	= udc_plat_remove,
	.driver		= {
		.name	= "snps-udc-plat",
		.of_match_table = of_match_ptr(of_udc_match),
#ifdef CONFIG_PM_SLEEP
		.pm	= &udc_plat_pm_ops,
#endif
	},
};
module_platform_driver(udc_plat_driver);

MODULE_DESCRIPTION(UDC_MOD_DESCRIPTION);
MODULE_AUTHOR("Broadcom");
MODULE_LICENSE("GPL v2");
