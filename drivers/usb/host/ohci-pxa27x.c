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
#include <linux/clk.h>

#include <mach/hardware.h>
#include <mach/pxa-regs.h>
#include <mach/pxa2xx-regs.h> /* FIXME: for PSSR */
#include <mach/ohci.h>

#define PXA_UHC_MAX_PORTNUM    3

#define UHCRHPS(x)              __REG2( 0x4C000050, (x)<<2 )

static struct clk *usb_clk;

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

		UHCRHDA |= RH_A_NPS;
	}

	return 0;
}

extern int usb_disabled(void);

/*-------------------------------------------------------------------------*/

static inline void pxa27x_setup_hc(struct pxaohci_platform_data *inf)
{
	uint32_t uhchr = UHCHR;
	uint32_t uhcrhda = UHCRHDA;

	if (inf->flags & ENABLE_PORT1)
		uhchr &= ~UHCHR_SSEP1;

	if (inf->flags & ENABLE_PORT2)
		uhchr &= ~UHCHR_SSEP2;

	if (inf->flags & ENABLE_PORT3)
		uhchr &= ~UHCHR_SSEP3;

	if (inf->flags & POWER_CONTROL_LOW)
		uhchr |= UHCHR_PCPL;

	if (inf->flags & POWER_SENSE_LOW)
		uhchr |= UHCHR_PSPL;

	if (inf->flags & NO_OC_PROTECTION)
		uhcrhda |= UHCRHDA_NOCP;

	if (inf->flags & OC_MODE_PERPORT)
		uhcrhda |= UHCRHDA_OCPM;

	if (inf->power_on_delay) {
		uhcrhda &= ~UHCRHDA_POTPGT(0xff);
		uhcrhda |= UHCRHDA_POTPGT(inf->power_on_delay / 2);
	}

	UHCHR = uhchr;
	UHCRHDA = uhcrhda;
}

static int pxa27x_start_hc(struct device *dev)
{
	int retval = 0;
	struct pxaohci_platform_data *inf;

	inf = dev->platform_data;

	clk_enable(usb_clk);

	UHCHR |= UHCHR_FHR;
	udelay(11);
	UHCHR &= ~UHCHR_FHR;

	UHCHR |= UHCHR_FSBIR;
	while (UHCHR & UHCHR_FSBIR)
		cpu_relax();

	pxa27x_setup_hc(inf);

	if (inf->init)
		retval = inf->init(dev);

	if (retval < 0)
		return retval;

	UHCHR &= ~UHCHR_SSE;

	UHCHIE = (UHCHIE_UPRIE | UHCHIE_RWIE);

	/* Clear any OTG Pin Hold */
	if (cpu_is_pxa27x() && (PSSR & PSSR_OTGPH))
		PSSR |= PSSR_OTGPH;

	return 0;
}

static void pxa27x_stop_hc(struct device *dev)
{
	struct pxaohci_platform_data *inf;

	inf = dev->platform_data;

	if (inf->exit)
		inf->exit(dev);

	UHCHR |= UHCHR_FHR;
	udelay(11);
	UHCHR &= ~UHCHR_FHR;

	UHCCOMS |= 1;
	udelay(10);

	clk_disable(usb_clk);
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
int usb_hcd_pxa27x_probe (const struct hc_driver *driver, struct platform_device *pdev)
{
	int retval;
	struct usb_hcd *hcd;
	struct pxaohci_platform_data *inf;

	inf = pdev->dev.platform_data;

	if (!inf)
		return -ENODEV;

	if (pdev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug ("resource[1] is not IORESOURCE_IRQ");
		return -ENOMEM;
	}

	usb_clk = clk_get(&pdev->dev, "USBCLK");
	if (IS_ERR(usb_clk))
		return PTR_ERR(usb_clk);

	hcd = usb_create_hcd (driver, &pdev->dev, "pxa27x");
	if (!hcd)
		return -ENOMEM;
	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;

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

	if ((retval = pxa27x_start_hc(&pdev->dev)) < 0) {
		pr_debug("pxa27x_start_hc failed");
		goto err3;
	}

	/* Select Power Management Mode */
	pxa27x_ohci_select_pmm(inf->port_mode);

	if (inf->power_budget)
		hcd->power_budget = inf->power_budget;

	ohci_hcd_init(hcd_to_ohci(hcd));

	retval = usb_add_hcd(hcd, pdev->resource[1].start, IRQF_DISABLED);
	if (retval == 0)
		return retval;

	pxa27x_stop_hc(&pdev->dev);
 err3:
	iounmap(hcd->regs);
 err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
 err1:
	usb_put_hcd(hcd);
	clk_put(usb_clk);
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
void usb_hcd_pxa27x_remove (struct usb_hcd *hcd, struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
	pxa27x_stop_hc(&pdev->dev);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	clk_put(usb_clk);
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
	.shutdown =		ohci_shutdown,

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

static int ohci_hcd_pxa27x_drv_probe(struct platform_device *pdev)
{
	pr_debug ("In ohci_hcd_pxa27x_drv_probe");

	if (usb_disabled())
		return -ENODEV;

	return usb_hcd_pxa27x_probe(&ohci_pxa27x_hc_driver, pdev);
}

static int ohci_hcd_pxa27x_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_pxa27x_remove(hcd, pdev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef	CONFIG_PM
static int ohci_hcd_pxa27x_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	pxa27x_stop_hc(&pdev->dev);
	hcd->state = HC_STATE_SUSPENDED;

	return 0;
}

static int ohci_hcd_pxa27x_drv_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int status;

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	if ((status = pxa27x_start_hc(&pdev->dev)) < 0)
		return status;

	ohci_finish_controller_resume(hcd);
	return 0;
}
#endif

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:pxa27x-ohci");

static struct platform_driver ohci_hcd_pxa27x_driver = {
	.probe		= ohci_hcd_pxa27x_drv_probe,
	.remove		= ohci_hcd_pxa27x_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
#ifdef CONFIG_PM
	.suspend	= ohci_hcd_pxa27x_drv_suspend,
	.resume		= ohci_hcd_pxa27x_drv_resume,
#endif
	.driver		= {
		.name	= "pxa27x-ohci",
		.owner	= THIS_MODULE,
	},
};

