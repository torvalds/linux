/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 *
 * Bus Glue for pxa27x
 *
 * Written by Christopher Hoover <ch@hpl.hp.com>
 * Based on fragments of previous driver by Russell King et al.
 *
 * Modified for LH7A404 from ohci-sa1111.c
 *  by Durgesh Pattamatta <pattamattad@sharpsec.com>
 *
 * Modified for pxa27x from ohci-lh7a404.c
 *  by Nick Bane <nick@cecomputing.co.uk> 26-8-2004
 *
 * This file is licenced under the GPL.
 */

#include <linux/device.h>
#include <linux/signal.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/arch/pxa-regs.h>


#define PMM_NPS_MODE           1
#define PMM_GLOBAL_MODE        2
#define PMM_PERPORT_MODE       3

#define PXA_UHC_MAX_PORTNUM    3

#define UHCRHPS(x)              __REG2( 0x4C000050, (x)<<2 )

static int pxa27x_ohci_pmm_state;

/*
  PMM_NPS_MODE -- PMM Non-power switching mode
      Ports are powered continuously.

  PMM_GLOBAL_MODE -- PMM global switching mode
      All ports are powered at the same time.

  PMM_PERPORT_MODE -- PMM per port switching mode
      Ports are powered individually.
 */
static int pxa27x_ohci_select_pmm( int mode )
{
	pxa27x_ohci_pmm_state = mode;

	switch ( mode ) {
	case PMM_NPS_MODE:
		UHCRHDA |= RH_A_NPS;
		break; 
	case PMM_GLOBAL_MODE:
		UHCRHDA &= ~(RH_A_NPS & RH_A_PSM);
		break;
	case PMM_PERPORT_MODE:
		UHCRHDA &= ~(RH_A_NPS);
		UHCRHDA |= RH_A_PSM;

		/* Set port power control mask bits, only 3 ports. */
		UHCRHDB |= (0x7<<17);
		break;
	default:
		printk( KERN_ERR
			"Invalid mode %d, set to non-power switch mode.\n", 
			mode );

		pxa27x_ohci_pmm_state = PMM_NPS_MODE;
		UHCRHDA |= RH_A_NPS;
	}

	return 0;
}

extern int usb_disabled(void);

/*-------------------------------------------------------------------------*/

static void pxa27x_start_hc(struct platform_device *dev)
{
	pxa_set_cken(CKEN10_USBHOST, 1);

	UHCHR |= UHCHR_FHR;
	udelay(11);
	UHCHR &= ~UHCHR_FHR;

	UHCHR |= UHCHR_FSBIR;
	while (UHCHR & UHCHR_FSBIR)
		cpu_relax();

	/* This could be properly abstracted away through the
	   device data the day more machines are supported and
	   their differences can be figured out correctly. */
	if (machine_is_mainstone()) {
		/* setup Port1 GPIO pin. */
		pxa_gpio_mode( 88 | GPIO_ALT_FN_1_IN);	/* USBHPWR1 */
		pxa_gpio_mode( 89 | GPIO_ALT_FN_2_OUT);	/* USBHPEN1 */

		/* Set the Power Control Polarity Low and Power Sense
		   Polarity Low to active low. Supply power to USB ports. */
		UHCHR = (UHCHR | UHCHR_PCPL | UHCHR_PSPL) &
			~(UHCHR_SSEP1 | UHCHR_SSEP2 | UHCHR_SSEP3 | UHCHR_SSE);

		pxa27x_ohci_pmm_state = PMM_PERPORT_MODE;
	}

	UHCHR &= ~UHCHR_SSE;

	UHCHIE = (UHCHIE_UPRIE | UHCHIE_RWIE);

	/* Clear any OTG Pin Hold */
	if (PSSR & PSSR_OTGPH)
		PSSR |= PSSR_OTGPH;
}

static void pxa27x_stop_hc(struct platform_device *dev)
{
	UHCHR |= UHCHR_FHR;
	udelay(11);
	UHCHR &= ~UHCHR_FHR;

	UHCCOMS |= 1;
	udelay(10);

	pxa_set_cken(CKEN10_USBHOST, 0);
}


/*-------------------------------------------------------------------------*/

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */


/**
 * usb_hcd_pxa27x_probe - initialize pxa27x-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */
int usb_hcd_pxa27x_probe (const struct hc_driver *driver,
			  struct platform_device *dev)
{
	int retval;
	struct usb_hcd *hcd;

	if (dev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug ("resource[1] is not IORESOURCE_IRQ");
		return -ENOMEM;
	}

	hcd = usb_create_hcd (driver, &dev->dev, "pxa27x");
	if (!hcd)
		return -ENOMEM;
	hcd->rsrc_start = dev->resource[0].start;
	hcd->rsrc_len = dev->resource[0].end - dev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		pr_debug("request_mem_region failed");
		retval = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		pr_debug("ioremap failed");
		retval = -ENOMEM;
		goto err2;
	}

	pxa27x_start_hc(dev);

	/* Select Power Management Mode */
	pxa27x_ohci_select_pmm(pxa27x_ohci_pmm_state);

	ohci_hcd_init(hcd_to_ohci(hcd));

	retval = usb_add_hcd(hcd, dev->resource[1].start, SA_INTERRUPT);
	if (retval == 0)
		return retval;

	pxa27x_stop_hc(dev);
	iounmap(hcd->regs);
 err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
 err1:
	usb_put_hcd(hcd);
	return retval;
}


/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_pxa27x_remove - shutdown processing for pxa27x-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_pxa27x_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
void usb_hcd_pxa27x_remove (struct usb_hcd *hcd, struct platform_device *dev)
{
	usb_remove_hcd(hcd);
	pxa27x_stop_hc(dev);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

/*-------------------------------------------------------------------------*/

static int __devinit
ohci_pxa27x_start (struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ret;

	ohci_dbg (ohci, "ohci_pxa27x_start, ohci:%p", ohci);

	/* The value of NDP in roothub_a is incorrect on this hardware */
	ohci->num_ports = 3;

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run (ohci)) < 0) {
		err ("can't start %s", hcd->self.bus_name);
		ohci_stop (hcd);
		return ret;
	}

	return 0;
}

/*-------------------------------------------------------------------------*/

static const struct hc_driver ohci_pxa27x_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"PXA27x OHCI",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_pxa27x_start,
	.stop =			ohci_stop,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef  CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/

static int ohci_hcd_pxa27x_drv_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	pr_debug ("In ohci_hcd_pxa27x_drv_probe");

	if (usb_disabled())
		return -ENODEV;

	ret = usb_hcd_pxa27x_probe(&ohci_pxa27x_hc_driver, pdev);
	return ret;
}

static int ohci_hcd_pxa27x_drv_remove(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	usb_hcd_pxa27x_remove(hcd, pdev);
	return 0;
}

static int ohci_hcd_pxa27x_drv_suspend(struct device *dev, pm_message_t state)
{
//	struct platform_device *pdev = to_platform_device(dev);
//	struct usb_hcd *hcd = dev_get_drvdata(dev);
	printk("%s: not implemented yet\n", __FUNCTION__);

	return 0;
}

static int ohci_hcd_pxa27x_drv_resume(struct device *dev)
{
//	struct platform_device *pdev = to_platform_device(dev);
//	struct usb_hcd *hcd = dev_get_drvdata(dev);
	printk("%s: not implemented yet\n", __FUNCTION__);

	return 0;
}


static struct device_driver ohci_hcd_pxa27x_driver = {
	.name		= "pxa27x-ohci",
	.bus		= &platform_bus_type,
	.probe		= ohci_hcd_pxa27x_drv_probe,
	.remove		= ohci_hcd_pxa27x_drv_remove,
	.suspend	= ohci_hcd_pxa27x_drv_suspend, 
	.resume		= ohci_hcd_pxa27x_drv_resume, 
};

static int __init ohci_hcd_pxa27x_init (void)
{
	pr_debug (DRIVER_INFO " (pxa27x)");
	pr_debug ("block sizes: ed %d td %d\n",
		sizeof (struct ed), sizeof (struct td));

	return driver_register(&ohci_hcd_pxa27x_driver);
}

static void __exit ohci_hcd_pxa27x_cleanup (void)
{
	driver_unregister(&ohci_hcd_pxa27x_driver);
}

module_init (ohci_hcd_pxa27x_init);
module_exit (ohci_hcd_pxa27x_cleanup);
