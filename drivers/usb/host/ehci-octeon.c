/*
 * EHCI HCD glue for Cavium Octeon II SOCs.
 *
 * Loosely based on ehci-au1xxx.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2010 Cavium Networks
 *
 */

#include <linux/platform_device.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-uctlx-defs.h>

#define OCTEON_EHCI_HCD_NAME "octeon-ehci"

/* Common clock init code.  */
void octeon2_usb_clocks_start(void);
void octeon2_usb_clocks_stop(void);

static void ehci_octeon_start(void)
{
	union cvmx_uctlx_ehci_ctl ehci_ctl;

	octeon2_usb_clocks_start();

	ehci_ctl.u64 = cvmx_read_csr(CVMX_UCTLX_EHCI_CTL(0));
	/* Use 64-bit addressing. */
	ehci_ctl.s.ehci_64b_addr_en = 1;
	ehci_ctl.s.l2c_addr_msb = 0;
	ehci_ctl.s.l2c_buff_emod = 1; /* Byte swapped. */
	ehci_ctl.s.l2c_desc_emod = 1; /* Byte swapped. */
	cvmx_write_csr(CVMX_UCTLX_EHCI_CTL(0), ehci_ctl.u64);
}

static void ehci_octeon_stop(void)
{
	octeon2_usb_clocks_stop();
}

static const struct hc_driver ehci_octeon_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Octeon EHCI",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2 | HCD_BH,

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
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

static u64 ehci_octeon_dma_mask = DMA_BIT_MASK(64);

static int ehci_octeon_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res_mem;
	int irq;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "No irq assigned\n");
		return -ENODEV;
	}

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_mem == NULL) {
		dev_err(&pdev->dev, "No register space assigned\n");
		return -ENODEV;
	}

	/*
	 * We can DMA from anywhere. But the descriptors must be in
	 * the lower 4GB.
	 */
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	pdev->dev.dma_mask = &ehci_octeon_dma_mask;

	hcd = usb_create_hcd(&ehci_octeon_hc_driver, &pdev->dev, "octeon");
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res_mem->start;
	hcd->rsrc_len = resource_size(res_mem);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				OCTEON_EHCI_HCD_NAME)) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		ret = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err2;
	}

	ehci_octeon_start();

	ehci = hcd_to_ehci(hcd);

	/* Octeon EHCI matches CPU endianness. */
#ifdef __BIG_ENDIAN
	ehci->big_endian_mmio = 1;
#endif

	ehci->caps = hcd->regs;

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret) {
		dev_dbg(&pdev->dev, "failed to add hcd with err %d\n", ret);
		goto err3;
	}

	platform_set_drvdata(pdev, hcd);

	return 0;
err3:
	ehci_octeon_stop();

	iounmap(hcd->regs);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err1:
	usb_put_hcd(hcd);
	return ret;
}

static int ehci_octeon_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);

	ehci_octeon_stop();
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	return 0;
}

static struct platform_driver ehci_octeon_driver = {
	.probe		= ehci_octeon_drv_probe,
	.remove		= ehci_octeon_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver = {
		.name	= OCTEON_EHCI_HCD_NAME,
		.owner	= THIS_MODULE,
	}
};

MODULE_ALIAS("platform:" OCTEON_EHCI_HCD_NAME);
