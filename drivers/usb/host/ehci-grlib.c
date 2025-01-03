// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Aeroflex Gaisler GRLIB GRUSBHC EHCI host controller
 *
 * GRUSBHC is typically found on LEON/GRLIB SoCs
 *
 * (c) Jan Andersson <jan@gaisler.com>
 *
 * Based on ehci-ppc-of.c which is:
 * (c) Valentine Barshak <vbarshak@ru.mvista.com>
 * and in turn based on "ehci-ppc-soc.c" by Stefan Roese <sr@denx.de>
 * and "ohci-ppc-of.c" by Sylvain Munaut <tnt@246tNt.com>
 */

#include <linux/err.h>
#include <linux/signal.h>

#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#define GRUSBHC_HCIVERSION 0x0100 /* Known value of cap. reg. HCIVERSION */

static const struct hc_driver ehci_grlib_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "GRLIB GRUSBHC EHCI",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_DMA | HCD_USB2 | HCD_BH,

	/*
	 * basic lifecycle operations
	 */
	.reset			= ehci_setup,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number	= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
#endif
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};


static int ehci_hcd_grlib_probe(struct platform_device *op)
{
	struct device_node *dn = op->dev.of_node;
	struct usb_hcd *hcd;
	struct ehci_hcd	*ehci = NULL;
	struct resource res;
	u32 hc_capbase;
	int irq;
	int rv;

	if (usb_disabled())
		return -ENODEV;

	dev_dbg(&op->dev, "initializing GRUSBHC EHCI USB Controller\n");

	rv = of_address_to_resource(dn, 0, &res);
	if (rv)
		return rv;

	/* usb_create_hcd requires dma_mask != NULL */
	op->dev.dma_mask = &op->dev.coherent_dma_mask;
	hcd = usb_create_hcd(&ehci_grlib_hc_driver, &op->dev,
			"GRUSBHC EHCI USB");
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res.start;
	hcd->rsrc_len = resource_size(&res);

	irq = irq_of_parse_and_map(dn, 0);
	if (!irq) {
		dev_err(&op->dev, "%s: irq_of_parse_and_map failed\n",
			__FILE__);
		rv = -EBUSY;
		goto err_irq;
	}

	hcd->regs = devm_ioremap_resource(&op->dev, &res);
	if (IS_ERR(hcd->regs)) {
		rv = PTR_ERR(hcd->regs);
		goto err_ioremap;
	}

	ehci = hcd_to_ehci(hcd);

	ehci->caps = hcd->regs;

	/* determine endianness of this implementation */
	hc_capbase = ehci_readl(ehci, &ehci->caps->hc_capbase);
	if (HC_VERSION(ehci, hc_capbase) != GRUSBHC_HCIVERSION) {
		ehci->big_endian_mmio = 1;
		ehci->big_endian_desc = 1;
		ehci->big_endian_capbase = 1;
	}

	rv = usb_add_hcd(hcd, irq, 0);
	if (rv)
		goto err_ioremap;

	device_wakeup_enable(hcd->self.controller);
	return 0;

err_ioremap:
	irq_dispose_mapping(irq);
err_irq:
	usb_put_hcd(hcd);

	return rv;
}


static void ehci_hcd_grlib_remove(struct platform_device *op)
{
	struct usb_hcd *hcd = platform_get_drvdata(op);

	dev_dbg(&op->dev, "stopping GRLIB GRUSBHC EHCI USB Controller\n");

	usb_remove_hcd(hcd);

	irq_dispose_mapping(hcd->irq);

	usb_put_hcd(hcd);
}


static const struct of_device_id ehci_hcd_grlib_of_match[] = {
	{
		.name = "GAISLER_EHCI",
	 },
	{
		.name = "01_026",
	 },
	{},
};
MODULE_DEVICE_TABLE(of, ehci_hcd_grlib_of_match);


static struct platform_driver ehci_grlib_driver = {
	.probe		= ehci_hcd_grlib_probe,
	.remove		= ehci_hcd_grlib_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver = {
		.name = "grlib-ehci",
		.of_match_table = ehci_hcd_grlib_of_match,
	},
};
