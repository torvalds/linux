/*
 * EHCI HCD (Host Controller Driver) for USB.
 *
 * Bus Glue for AMD Alchemy Au1xxx
 *
 * Based on "ohci-au1xxx.c" by Matt Porter <mporter@kernel.crashing.org>
 *
 * Modified for AMD Alchemy Au1200 EHC
 *  by K.Boge <karsten.boge@amd.com>
 *
 * This file is licenced under the GPL.
 */

#include <linux/platform_device.h>
#include <asm/mach-au1x00/au1000.h>

#define USB_HOST_CONFIG   (USB_MSR_BASE + USB_MSR_MCFG)
#define USB_MCFG_PFEN     (1<<31)
#define USB_MCFG_RDCOMB   (1<<30)
#define USB_MCFG_SSDEN    (1<<23)
#define USB_MCFG_PHYPLLEN (1<<19)
#define USB_MCFG_UCECLKEN (1<<18)
#define USB_MCFG_EHCCLKEN (1<<17)
#ifdef CONFIG_DMA_COHERENT
#define USB_MCFG_UCAM     (1<<7)
#else
#define USB_MCFG_UCAM     (0)
#endif
#define USB_MCFG_EBMEN    (1<<3)
#define USB_MCFG_EMEMEN   (1<<2)

#define USBH_ENABLE_CE	(USB_MCFG_PHYPLLEN | USB_MCFG_EHCCLKEN)
#define USBH_ENABLE_INIT (USB_MCFG_PFEN  | USB_MCFG_RDCOMB |	\
			  USBH_ENABLE_CE | USB_MCFG_SSDEN  |	\
			  USB_MCFG_UCAM  | USB_MCFG_EBMEN  |	\
			  USB_MCFG_EMEMEN)

#define USBH_DISABLE      (USB_MCFG_EBMEN | USB_MCFG_EMEMEN)

extern int usb_disabled(void);

static void au1xxx_start_ehc(void)
{
	/* enable clock to EHCI block and HS PHY PLL*/
	au_writel(au_readl(USB_HOST_CONFIG) | USBH_ENABLE_CE, USB_HOST_CONFIG);
	au_sync();
	udelay(1000);

	/* enable EHCI mmio */
	au_writel(au_readl(USB_HOST_CONFIG) | USBH_ENABLE_INIT, USB_HOST_CONFIG);
	au_sync();
	udelay(1000);
}

static void au1xxx_stop_ehc(void)
{
	unsigned long c;

	/* Disable mem */
	au_writel(au_readl(USB_HOST_CONFIG) & ~USBH_DISABLE, USB_HOST_CONFIG);
	au_sync();
	udelay(1000);

	/* Disable EHC clock. If the HS PHY is unused disable it too. */
	c = au_readl(USB_HOST_CONFIG) & ~USB_MCFG_EHCCLKEN;
	if (!(c & USB_MCFG_UCECLKEN))		/* UDC disabled? */
		c &= ~USB_MCFG_PHYPLLEN;	/* yes: disable HS PHY PLL */
	au_writel(c, USB_HOST_CONFIG);
	au_sync();
}

static const struct hc_driver ehci_au1xxx_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Au1xxx EHCI",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 *
	 * FIXME -- ehci_init() doesn't do enough here.
	 * See ehci-ppc-soc for a complete implementation.
	 */
	.reset			= ehci_init,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number	= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,
};

static int ehci_hcd_au1xxx_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	int ret;

	if (usb_disabled())
		return -ENODEV;

#if defined(CONFIG_SOC_AU1200) && defined(CONFIG_DMA_COHERENT)
	/* Au1200 AB USB does not support coherent memory */
	if (!(read_c0_prid() & 0xff)) {
		printk(KERN_INFO "%s: this is chip revision AB!\n", pdev->name);
		printk(KERN_INFO "%s: update your board or re-configure"
				 " the kernel\n", pdev->name);
		return -ENODEV;
	}
#endif

	if (pdev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug("resource[1] is not IORESOURCE_IRQ");
		return -ENOMEM;
	}
	hcd = usb_create_hcd(&ehci_au1xxx_hc_driver, &pdev->dev, "Au1xxx");
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		pr_debug("request_mem_region failed");
		ret = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		pr_debug("ioremap failed");
		ret = -ENOMEM;
		goto err2;
	}

	au1xxx_start_ehc();

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs + HC_LENGTH(readl(&ehci->caps->hc_capbase));
	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = readl(&ehci->caps->hcs_params);

	ret = usb_add_hcd(hcd, pdev->resource[1].start,
			  IRQF_DISABLED | IRQF_SHARED);
	if (ret == 0) {
		platform_set_drvdata(pdev, hcd);
		return ret;
	}

	au1xxx_stop_ehc();
	iounmap(hcd->regs);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err1:
	usb_put_hcd(hcd);
	return ret;
}

static int ehci_hcd_au1xxx_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	au1xxx_stop_ehc();
	platform_set_drvdata(pdev, NULL);

	return 0;
}

 /*TBD*/
/*static int ehci_hcd_au1xxx_drv_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	return 0;
}
static int ehci_hcd_au1xxx_drv_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	return 0;
}
*/

static struct platform_driver ehci_hcd_au1xxx_driver = {
	.probe		= ehci_hcd_au1xxx_drv_probe,
	.remove		= ehci_hcd_au1xxx_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	/*.suspend      = ehci_hcd_au1xxx_drv_suspend, */
	/*.resume       = ehci_hcd_au1xxx_drv_resume, */
	.driver = {
		.name	= "au1xxx-ehci",
		.owner	= THIS_MODULE,
	}
};

MODULE_ALIAS("platform:au1xxx-ehci");
