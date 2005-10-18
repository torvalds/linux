/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2005 David Brownell
 * (C) Copyright 2002 Hewlett-Packard Company
 * 
 * OMAP Bus Glue
 *
 * Modified for OMAP by Tony Lindgren <tony@atomide.com>
 * Based on the 2.4 OMAP OHCI driver originally done by MontaVista Software Inc.
 * and on ohci-sa1111.c by Christopher Hoover <ch@hpl.hp.com>
 *
 * This file is licenced under the GPL.
 */

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach-types.h>

#include <asm/arch/mux.h>
#include <asm/arch/irqs.h>
#include <asm/arch/gpio.h>
#include <asm/arch/fpga.h>
#include <asm/arch/usb.h>
#include <asm/hardware/clock.h>


/* OMAP-1510 OHCI has its own MMU for DMA */
#define OMAP1510_LB_MEMSIZE	32	/* Should be same as SDRAM size */
#define OMAP1510_LB_CLOCK_DIV	0xfffec10c
#define OMAP1510_LB_MMU_CTL	0xfffec208
#define OMAP1510_LB_MMU_LCK	0xfffec224
#define OMAP1510_LB_MMU_LD_TLB	0xfffec228
#define OMAP1510_LB_MMU_CAM_H	0xfffec22c
#define OMAP1510_LB_MMU_CAM_L	0xfffec230
#define OMAP1510_LB_MMU_RAM_H	0xfffec234
#define OMAP1510_LB_MMU_RAM_L	0xfffec238


#ifndef CONFIG_ARCH_OMAP
#error "This file is OMAP bus glue.  CONFIG_OMAP must be defined."
#endif

#ifdef CONFIG_TPS65010
#include <asm/arch/tps65010.h>
#else

#define LOW	0
#define HIGH	1

#define GPIO1	1

static inline int tps65010_set_gpio_out_value(unsigned gpio, unsigned value)
{
	return 0;
}

#endif

extern int usb_disabled(void);
extern int ocpi_enable(void);

static struct clk *usb_host_ck;

static void omap_ohci_clock_power(int on)
{
	if (on) {
		clk_enable(usb_host_ck);
		/* guesstimate for T5 == 1x 32K clock + APLL lock time */
		udelay(100);
	} else {
		clk_disable(usb_host_ck);
	}
}

/*
 * Board specific gang-switched transceiver power on/off.
 * NOTE:  OSK supplies power from DC, not battery.
 */
static int omap_ohci_transceiver_power(int on)
{
	if (on) {
		if (machine_is_omap_innovator() && cpu_is_omap1510())
			fpga_write(fpga_read(INNOVATOR_FPGA_CAM_USB_CONTROL)
				| ((1 << 5/*usb1*/) | (1 << 3/*usb2*/)), 
			       INNOVATOR_FPGA_CAM_USB_CONTROL);
		else if (machine_is_omap_osk())
			tps65010_set_gpio_out_value(GPIO1, LOW);
	} else {
		if (machine_is_omap_innovator() && cpu_is_omap1510())
			fpga_write(fpga_read(INNOVATOR_FPGA_CAM_USB_CONTROL)
				& ~((1 << 5/*usb1*/) | (1 << 3/*usb2*/)), 
			       INNOVATOR_FPGA_CAM_USB_CONTROL);
		else if (machine_is_omap_osk())
			tps65010_set_gpio_out_value(GPIO1, HIGH);
	}

	return 0;
}

/*
 * OMAP-1510 specific Local Bus clock on/off
 */
static int omap_1510_local_bus_power(int on)
{
	if (on) {
		omap_writel((1 << 1) | (1 << 0), OMAP1510_LB_MMU_CTL);
		udelay(200);
	} else {
		omap_writel(0, OMAP1510_LB_MMU_CTL);
	}

	return 0;
}

/*
 * OMAP-1510 specific Local Bus initialization
 * NOTE: This assumes 32MB memory size in OMAP1510LB_MEMSIZE.
 *       See also arch/mach-omap/memory.h for __virt_to_dma() and 
 *       __dma_to_virt() which need to match with the physical 
 *       Local Bus address below.
 */
static int omap_1510_local_bus_init(void)
{
	unsigned int tlb;
	unsigned long lbaddr, physaddr;

	omap_writel((omap_readl(OMAP1510_LB_CLOCK_DIV) & 0xfffffff8) | 0x4, 
	       OMAP1510_LB_CLOCK_DIV);

	/* Configure the Local Bus MMU table */
	for (tlb = 0; tlb < OMAP1510_LB_MEMSIZE; tlb++) {
		lbaddr = tlb * 0x00100000 + OMAP1510_LB_OFFSET;
		physaddr = tlb * 0x00100000 + PHYS_OFFSET;
		omap_writel((lbaddr & 0x0fffffff) >> 22, OMAP1510_LB_MMU_CAM_H);
		omap_writel(((lbaddr & 0x003ffc00) >> 6) | 0xc, 
		       OMAP1510_LB_MMU_CAM_L);
		omap_writel(physaddr >> 16, OMAP1510_LB_MMU_RAM_H);
		omap_writel((physaddr & 0x0000fc00) | 0x300, OMAP1510_LB_MMU_RAM_L);
		omap_writel(tlb << 4, OMAP1510_LB_MMU_LCK);
		omap_writel(0x1, OMAP1510_LB_MMU_LD_TLB);
	}

	/* Enable the walking table */
	omap_writel(omap_readl(OMAP1510_LB_MMU_CTL) | (1 << 3), OMAP1510_LB_MMU_CTL);
	udelay(200);

	return 0;
}

#ifdef	CONFIG_USB_OTG

static void start_hnp(struct ohci_hcd *ohci)
{
	const unsigned	port = ohci_to_hcd(ohci)->self.otg_port - 1;
	unsigned long	flags;

	otg_start_hnp(ohci->transceiver);

	local_irq_save(flags);
	ohci->transceiver->state = OTG_STATE_A_SUSPEND;
	writel (RH_PS_PSS, &ohci->regs->roothub.portstatus [port]);
	OTG_CTRL_REG &= ~OTG_A_BUSREQ;
	local_irq_restore(flags);
}

#endif

/*-------------------------------------------------------------------------*/

static int omap_start_hc(struct ohci_hcd *ohci, struct platform_device *pdev)
{
	struct omap_usb_config	*config = pdev->dev.platform_data;
	int			need_transceiver = (config->otg != 0);
	int			ret;

	dev_dbg(&pdev->dev, "starting USB Controller\n");

	if (config->otg) {
		ohci_to_hcd(ohci)->self.otg_port = config->otg;
		/* default/minimum OTG power budget:  8 mA */
		ohci_to_hcd(ohci)->power_budget = 8;
	}

	/* boards can use OTG transceivers in non-OTG modes */
	need_transceiver = need_transceiver
			|| machine_is_omap_h2() || machine_is_omap_h3();

	if (cpu_is_omap16xx())
		ocpi_enable();

#ifdef	CONFIG_ARCH_OMAP_OTG
	if (need_transceiver) {
		ohci->transceiver = otg_get_transceiver();
		if (ohci->transceiver) {
			int	status = otg_set_host(ohci->transceiver,
						&ohci_to_hcd(ohci)->self);
			dev_dbg(&pdev->dev, "init %s transceiver, status %d\n",
					ohci->transceiver->label, status);
			if (status) {
				if (ohci->transceiver)
					put_device(ohci->transceiver->dev);
				return status;
			}
		} else {
			dev_err(&pdev->dev, "can't find transceiver\n");
			return -ENODEV;
		}
	}
#endif

	omap_ohci_clock_power(1);

	if (cpu_is_omap1510()) {
		omap_1510_local_bus_power(1);
		omap_1510_local_bus_init();
	}

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	/* board-specific power switching and overcurrent support */
	if (machine_is_omap_osk() || machine_is_omap_innovator()) {
		u32	rh = roothub_a (ohci);

		/* power switching (ganged by default) */
		rh &= ~RH_A_NPS;

		/* TPS2045 switch for internal transceiver (port 1) */
		if (machine_is_omap_osk()) {
			ohci_to_hcd(ohci)->power_budget = 250;

			rh &= ~RH_A_NOCP;

			/* gpio9 for overcurrent detction */
			omap_cfg_reg(W8_1610_GPIO9);
			omap_request_gpio(9);
			omap_set_gpio_direction(9, 1 /* IN */);

			/* for paranoia's sake:  disable USB.PUEN */
			omap_cfg_reg(W4_USB_HIGHZ);
		}
		ohci_writel(ohci, rh, &ohci->regs->roothub.a);
		distrust_firmware = 0;
	}

	/* FIXME khubd hub requests should manage power switching */
	omap_ohci_transceiver_power(1);

	/* board init will have already handled HMC and mux setup.
	 * any external transceiver should already be initialized
	 * too, so all configured ports use the right signaling now.
	 */

	return 0;
}

static void omap_stop_hc(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "stopping USB Controller\n");
	omap_ohci_clock_power(0);
}


/*-------------------------------------------------------------------------*/

void usb_hcd_omap_remove (struct usb_hcd *, struct platform_device *);

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */


/**
 * usb_hcd_omap_probe - initialize OMAP-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
int usb_hcd_omap_probe (const struct hc_driver *driver,
			  struct platform_device *pdev)
{
	int retval;
	struct usb_hcd *hcd = 0;
	struct ohci_hcd *ohci;

	if (pdev->num_resources != 2) {
		printk(KERN_ERR "hcd probe: invalid num_resources: %i\n", 
		       pdev->num_resources);
		return -ENODEV;
	}

	if (pdev->resource[0].flags != IORESOURCE_MEM 
			|| pdev->resource[1].flags != IORESOURCE_IRQ) {
		printk(KERN_ERR "hcd probe: invalid resource type\n");
		return -ENODEV;
	}

	usb_host_ck = clk_get(0, "usb_hhc_ck");
	if (IS_ERR(usb_host_ck))
		return PTR_ERR(usb_host_ck);

	hcd = usb_create_hcd (driver, &pdev->dev, pdev->dev.bus_id);
	if (!hcd) {
		retval = -ENOMEM;
		goto err0;
	}
	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_dbg(&pdev->dev, "request_mem_region failed\n");
		retval = -EBUSY;
		goto err1;
	}

	hcd->regs = (void __iomem *) (int) IO_ADDRESS(hcd->rsrc_start);

	ohci = hcd_to_ohci(hcd);
	ohci_hcd_init(ohci);

	retval = omap_start_hc(ohci, pdev);
	if (retval < 0)
		goto err2;

	retval = usb_add_hcd(hcd, platform_get_irq(pdev, 0), SA_INTERRUPT);
	if (retval == 0)
		return retval;

	omap_stop_hc(pdev);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err1:
	usb_put_hcd(hcd);
err0:
	clk_put(usb_host_ck);
	return retval;
}


/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_omap_remove - shutdown processing for OMAP-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_omap_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
void usb_hcd_omap_remove (struct usb_hcd *hcd, struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
	if (machine_is_omap_osk())
		omap_free_gpio(9);
	omap_stop_hc(pdev);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	clk_put(usb_host_ck);
}

/*-------------------------------------------------------------------------*/

static int __devinit
ohci_omap_start (struct usb_hcd *hcd)
{
	struct omap_usb_config *config;
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ret;

	config = hcd->self.controller->platform_data;
	if (config->otg || config->rwc)
		writel(OHCI_CTRL_RWC, &ohci->regs->control);

	if ((ret = ohci_run (ohci)) < 0) {
		dev_err(hcd->self.controller, "can't start\n");
		ohci_stop (hcd);
		return ret;
	}
	return 0;
}

/*-------------------------------------------------------------------------*/

static const struct hc_driver ohci_omap_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"OMAP OHCI",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_omap_start,
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
#ifdef	CONFIG_USB_SUSPEND
	.hub_suspend =		ohci_hub_suspend,
	.hub_resume =		ohci_hub_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/

static int ohci_hcd_omap_drv_probe(struct device *dev)
{
	return usb_hcd_omap_probe(&ohci_omap_hc_driver,
				to_platform_device(dev));
}

static int ohci_hcd_omap_drv_remove(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct usb_hcd		*hcd = dev_get_drvdata(dev);
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);

	usb_hcd_omap_remove(hcd, pdev);
	if (ohci->transceiver) {
		(void) otg_set_host(ohci->transceiver, 0);
		put_device(ohci->transceiver->dev);
	}
	dev_set_drvdata(dev, NULL);

	return 0;
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

static int ohci_omap_suspend(struct device *dev, pm_message_t message, u32 level)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(dev_get_drvdata(dev));
	int		status = -EINVAL;

	if (level != SUSPEND_POWER_DOWN)
		return 0;

	down(&ohci_to_hcd(ohci)->self.root_hub->serialize);
	status = ohci_hub_suspend(ohci_to_hcd(ohci));
	if (status == 0) {
		omap_ohci_clock_power(0);
		ohci_to_hcd(ohci)->self.root_hub->state =
			USB_STATE_SUSPENDED;
		ohci_to_hcd(ohci)->state = HC_STATE_SUSPENDED;
		dev->power.power_state = PMSG_SUSPEND;
	}
	up(&ohci_to_hcd(ohci)->self.root_hub->serialize);
	return status;
}

static int ohci_omap_resume(struct device *dev, u32 level)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(dev_get_drvdata(dev));
	int		status = 0;

	if (level != RESUME_POWER_ON)
		return 0;

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;
	omap_ohci_clock_power(1);
#ifdef	CONFIG_USB_SUSPEND
	/* get extra cleanup even if remote wakeup isn't in use */
	status = usb_resume_device(ohci_to_hcd(ohci)->self.root_hub);
#else
	down(&ohci_to_hcd(ohci)->self.root_hub->serialize);
	status = ohci_hub_resume(ohci_to_hcd(ohci));
	up(&ohci_to_hcd(ohci)->self.root_hub->serialize);
#endif
	if (status == 0)
		dev->power.power_state = PMSG_ON;
	return status;
}

#endif

/*-------------------------------------------------------------------------*/

/*
 * Driver definition to register with the OMAP bus
 */
static struct device_driver ohci_hcd_omap_driver = {
	.name		= "ohci",
	.bus		= &platform_bus_type,
	.probe		= ohci_hcd_omap_drv_probe,
	.remove		= ohci_hcd_omap_drv_remove,
#ifdef	CONFIG_PM
	.suspend	= ohci_omap_suspend,
	.resume		= ohci_omap_resume,
#endif
};

static int __init ohci_hcd_omap_init (void)
{
	printk (KERN_DEBUG "%s: " DRIVER_INFO " (OMAP)\n", hcd_name);
	if (usb_disabled())
		return -ENODEV;

	pr_debug("%s: block sizes: ed %Zd td %Zd\n", hcd_name,
		sizeof (struct ed), sizeof (struct td));

	return driver_register(&ohci_hcd_omap_driver);
}

static void __exit ohci_hcd_omap_cleanup (void)
{
	driver_unregister(&ohci_hcd_omap_driver);
}

module_init (ohci_hcd_omap_init);
module_exit (ohci_hcd_omap_cleanup);
