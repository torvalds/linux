/*
 * drivers/usb/host/ehci-pxa168.c
 *
 * Tanmay Upadhyay <tanmay.upadhyay@einfochips.com>
 *
 * Based on drivers/usb/host/ehci-orion.c
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <mach/pxa168.h>

#define USB_PHY_CTRL_REG 0x4
#define USB_PHY_PLL_REG  0x8
#define USB_PHY_TX_REG   0xc

#define FBDIV_SHIFT      4

#define ICP_SHIFT        12
#define ICP_15            2
#define ICP_20            3
#define ICP_25            4

#define KVCO_SHIFT       15

#define PLLCALI12_SHIFT  25
#define CALI12_VDD        0
#define CALI12_09         1
#define CALI12_10         2
#define CALI12_11         3

#define PLLVDD12_SHIFT   27
#define VDD12_VDD         0
#define VDD12_10          1
#define VDD12_11          2
#define VDD12_12          3

#define PLLVDD18_SHIFT   29
#define VDD18_19          0
#define VDD18_20          1
#define VDD18_21          2
#define VDD18_22          3


#define PLL_READY        (1 << 23)
#define VCOCAL_START     (1 << 21)
#define REG_RCAL_START   (1 << 12)

struct pxa168_usb_drv_data {
	struct ehci_hcd ehci;
	struct clk	*pxa168_usb_clk;
	struct resource	*usb_phy_res;
	void __iomem	*usb_phy_reg_base;
};

static int ehci_pxa168_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	ehci_reset(ehci);
	retval = ehci_halt(ehci);
	if (retval)
		return retval;

	/*
	 * data structure init
	 */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	hcd->has_tt = 1;

	ehci_port_power(ehci, 0);

	return retval;
}

static const struct hc_driver ehci_pxa168_hc_driver = {
	.description = hcd_name,
	.product_desc = "Marvell PXA168 EHCI",
	.hcd_priv_size = sizeof(struct pxa168_usb_drv_data),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_pxa168_setup,
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
	.relinquish_port = ehci_relinquish_port,
	.port_handed_over = ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

static int pxa168_usb_phy_init(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *usb_phy_reg_base;
	struct pxa168_usb_pdata *pdata;
	struct pxa168_usb_drv_data *drv_data;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	unsigned long reg_val;
	int pll_retry_cont = 10000, err = 0;

	drv_data = (struct pxa168_usb_drv_data *)hcd->hcd_priv;
	pdata = (struct pxa168_usb_pdata *)pdev->dev.platform_data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no PHY register addr. Check %s setup!\n",
			dev_name(&pdev->dev));
		return -ENODEV;
	}

	if (!request_mem_region(res->start, resource_size(res),
				ehci_pxa168_hc_driver.description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		return -EBUSY;
	}

	usb_phy_reg_base = ioremap(res->start, resource_size(res));
	if (usb_phy_reg_base == NULL) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		err = -EFAULT;
		goto err1;
	}
	drv_data->usb_phy_reg_base = usb_phy_reg_base;
	drv_data->usb_phy_res = res;

	/* If someone wants to init USB phy in board specific way */
	if (pdata && pdata->phy_init)
		return pdata->phy_init(usb_phy_reg_base);

	/* Power up the PHY and PLL */
	writel(readl(usb_phy_reg_base + USB_PHY_CTRL_REG) | 0x3,
		usb_phy_reg_base + USB_PHY_CTRL_REG);

	/* Configure PHY PLL */
	reg_val = readl(usb_phy_reg_base + USB_PHY_PLL_REG) & ~(0x7e03ffff);
	reg_val |= (VDD18_22 << PLLVDD18_SHIFT | VDD12_12 << PLLVDD12_SHIFT |
		CALI12_11 << PLLCALI12_SHIFT | 3 << KVCO_SHIFT |
		ICP_15 << ICP_SHIFT | 0xee << FBDIV_SHIFT | 0xb);
	writel(reg_val, usb_phy_reg_base + USB_PHY_PLL_REG);

	/* Make sure PHY PLL is ready */
	while (!(readl(usb_phy_reg_base + USB_PHY_PLL_REG) & PLL_READY)) {
		if (!(pll_retry_cont--)) {
			dev_dbg(&pdev->dev, "USB PHY PLL not ready\n");
			err = -EIO;
			goto err2;
		}
	}

	/* Toggle VCOCAL_START bit of U2PLL for PLL calibration */
	udelay(200);
	writel(readl(usb_phy_reg_base + USB_PHY_PLL_REG) | VCOCAL_START,
		usb_phy_reg_base + USB_PHY_PLL_REG);
	udelay(40);
	writel(readl(usb_phy_reg_base + USB_PHY_PLL_REG) & ~VCOCAL_START,
		usb_phy_reg_base + USB_PHY_PLL_REG);

	/* Toggle REG_RCAL_START bit of U2PTX for impedance calibration */
	udelay(400);
	writel(readl(usb_phy_reg_base + USB_PHY_TX_REG) | REG_RCAL_START,
		usb_phy_reg_base + USB_PHY_TX_REG);
	udelay(40);
	writel(readl(usb_phy_reg_base + USB_PHY_TX_REG) & ~REG_RCAL_START,
		usb_phy_reg_base + USB_PHY_TX_REG);

	/* Make sure PHY PLL is ready again */
	pll_retry_cont = 0;
	while (!(readl(usb_phy_reg_base + USB_PHY_PLL_REG) & PLL_READY)) {
		if (!(pll_retry_cont--)) {
			dev_dbg(&pdev->dev, "USB PHY PLL not ready\n");
			err = -EIO;
			goto err2;
		}
	}

	return 0;
err2:
	iounmap(usb_phy_reg_base);
err1:
	release_mem_region(res->start, resource_size(res));
	return err;
}

static int __devinit ehci_pxa168_drv_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct pxa168_usb_drv_data *drv_data;
	void __iomem *regs;
	int irq, err = 0;

	if (usb_disabled())
		return -ENODEV;

	pr_debug("Initializing pxa168-SoC USB Host Controller\n");

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev,
			"Found HC with no IRQ. Check %s setup!\n",
			dev_name(&pdev->dev));
		err = -ENODEV;
		goto err1;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
			dev_name(&pdev->dev));
		err = -ENODEV;
		goto err1;
	}

	if (!request_mem_region(res->start, resource_size(res),
				ehci_pxa168_hc_driver.description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		err = -EBUSY;
		goto err1;
	}

	regs = ioremap(res->start, resource_size(res));
	if (regs == NULL) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		err = -EFAULT;
		goto err2;
	}

	hcd = usb_create_hcd(&ehci_pxa168_hc_driver,
			&pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		err = -ENOMEM;
		goto err3;
	}

	drv_data = (struct pxa168_usb_drv_data *)hcd->hcd_priv;

	/* Enable USB clock */
	drv_data->pxa168_usb_clk = clk_get(&pdev->dev, "PXA168-USBCLK");
	if (IS_ERR(drv_data->pxa168_usb_clk)) {
		dev_err(&pdev->dev, "Couldn't get USB clock\n");
		err = PTR_ERR(drv_data->pxa168_usb_clk);
		goto err4;
	}
	clk_enable(drv_data->pxa168_usb_clk);

	err = pxa168_usb_phy_init(pdev);
	if (err) {
		dev_err(&pdev->dev, "USB PHY initialization failed\n");
		goto err5;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = regs;

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs + 0x100;
	ehci->regs = hcd->regs + 0x100 +
		HC_LENGTH(ehci, ehci_readl(ehci, &ehci->caps->hc_capbase));
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);
	hcd->has_tt = 1;
	ehci->sbrn = 0x20;

	err = usb_add_hcd(hcd, irq, IRQF_SHARED | IRQF_DISABLED);
	if (err)
		goto err5;

	return 0;

err5:
	clk_disable(drv_data->pxa168_usb_clk);
	clk_put(drv_data->pxa168_usb_clk);
err4:
	usb_put_hcd(hcd);
err3:
	iounmap(regs);
err2:
	release_mem_region(res->start, resource_size(res));
err1:
	dev_err(&pdev->dev, "init %s fail, %d\n",
		dev_name(&pdev->dev), err);

	return err;
}

static int __exit ehci_pxa168_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct pxa168_usb_drv_data *drv_data =
		(struct pxa168_usb_drv_data *)hcd->hcd_priv;

	usb_remove_hcd(hcd);

	/* Power down PHY & PLL */
	writel(readl(drv_data->usb_phy_reg_base + USB_PHY_CTRL_REG) & (~0x3),
		drv_data->usb_phy_reg_base + USB_PHY_CTRL_REG);

	clk_disable(drv_data->pxa168_usb_clk);
	clk_put(drv_data->pxa168_usb_clk);

	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);

	iounmap(drv_data->usb_phy_reg_base);
	release_mem_region(drv_data->usb_phy_res->start,
			resource_size(drv_data->usb_phy_res));

	usb_put_hcd(hcd);

	return 0;
}

MODULE_ALIAS("platform:pxa168-ehci");

static struct platform_driver ehci_pxa168_driver = {
	.probe		= ehci_pxa168_drv_probe,
	.remove		= __exit_p(ehci_pxa168_drv_remove),
	.shutdown	= usb_hcd_platform_shutdown,
	.driver.name	= "pxa168-ehci",
};
