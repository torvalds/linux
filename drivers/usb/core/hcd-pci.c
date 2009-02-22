/*
 * (C) Copyright David Brownell 2000-2002
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/usb.h>

#include <asm/io.h>
#include <asm/irq.h>

#ifdef CONFIG_PPC_PMAC
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/pci-bridge.h>
#include <asm/prom.h>
#endif

#include "usb.h"
#include "hcd.h"


/* PCI-based HCs are common, but plenty of non-PCI HCs are used too */


/*-------------------------------------------------------------------------*/

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_hcd_pci_probe - initialize PCI-based HCDs
 * @dev: USB Host Controller being probed
 * @id: pci hotplug id connecting controller to HCD framework
 * Context: !in_interrupt()
 *
 * Allocates basic PCI resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 * Store this function in the HCD's struct pci_driver as probe().
 */
int usb_hcd_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct hc_driver	*driver;
	struct usb_hcd		*hcd;
	int			retval;

	if (usb_disabled())
		return -ENODEV;

	if (!id)
		return -EINVAL;
	driver = (struct hc_driver *)id->driver_data;
	if (!driver)
		return -EINVAL;

	if (pci_enable_device(dev) < 0)
		return -ENODEV;
	dev->current_state = PCI_D0;

	if (!dev->irq) {
		dev_err(&dev->dev,
			"Found HC with no IRQ.  Check BIOS/PCI %s setup!\n",
			pci_name(dev));
		retval = -ENODEV;
		goto err1;
	}

	hcd = usb_create_hcd(driver, &dev->dev, pci_name(dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}

	if (driver->flags & HCD_MEMORY) {
		/* EHCI, OHCI */
		hcd->rsrc_start = pci_resource_start(dev, 0);
		hcd->rsrc_len = pci_resource_len(dev, 0);
		if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
			dev_dbg(&dev->dev, "controller already in use\n");
			retval = -EBUSY;
			goto err2;
		}
		hcd->regs = ioremap_nocache(hcd->rsrc_start, hcd->rsrc_len);
		if (hcd->regs == NULL) {
			dev_dbg(&dev->dev, "error mapping memory\n");
			retval = -EFAULT;
			goto err3;
		}

	} else {
		/* UHCI */
		int	region;

		for (region = 0; region < PCI_ROM_RESOURCE; region++) {
			if (!(pci_resource_flags(dev, region) &
					IORESOURCE_IO))
				continue;

			hcd->rsrc_start = pci_resource_start(dev, region);
			hcd->rsrc_len = pci_resource_len(dev, region);
			if (request_region(hcd->rsrc_start, hcd->rsrc_len,
					driver->description))
				break;
		}
		if (region == PCI_ROM_RESOURCE) {
			dev_dbg(&dev->dev, "no i/o regions available\n");
			retval = -EBUSY;
			goto err1;
		}
	}

	pci_set_master(dev);

	retval = usb_add_hcd(hcd, dev->irq, IRQF_DISABLED | IRQF_SHARED);
	if (retval != 0)
		goto err4;
	return retval;

 err4:
	if (driver->flags & HCD_MEMORY) {
		iounmap(hcd->regs);
 err3:
		release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	} else
		release_region(hcd->rsrc_start, hcd->rsrc_len);
 err2:
	usb_put_hcd(hcd);
 err1:
	pci_disable_device(dev);
	dev_err(&dev->dev, "init %s fail, %d\n", pci_name(dev), retval);
	return retval;
}
EXPORT_SYMBOL_GPL(usb_hcd_pci_probe);


/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_pci_remove - shutdown processing for PCI-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_pci_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 * Store this function in the HCD's struct pci_driver as remove().
 */
void usb_hcd_pci_remove(struct pci_dev *dev)
{
	struct usb_hcd		*hcd;

	hcd = pci_get_drvdata(dev);
	if (!hcd)
		return;

	usb_remove_hcd(hcd);
	if (hcd->driver->flags & HCD_MEMORY) {
		iounmap(hcd->regs);
		release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	} else {
		release_region(hcd->rsrc_start, hcd->rsrc_len);
	}
	usb_put_hcd(hcd);
	pci_disable_device(dev);
}
EXPORT_SYMBOL_GPL(usb_hcd_pci_remove);


#ifdef	CONFIG_PM

/**
 * usb_hcd_pci_suspend - power management suspend of a PCI-based HCD
 * @dev: USB Host Controller being suspended
 * @message: Power Management message describing this state transition
 *
 * Store this function in the HCD's struct pci_driver as .suspend.
 */
int usb_hcd_pci_suspend(struct pci_dev *dev, pm_message_t message)
{
	struct usb_hcd		*hcd = pci_get_drvdata(dev);
	int			retval = 0;
	int			wake, w;
	int			has_pci_pm;

	/* Root hub suspend should have stopped all downstream traffic,
	 * and all bus master traffic.  And done so for both the interface
	 * and the stub usb_device (which we check here).  But maybe it
	 * didn't; writing sysfs power/state files ignores such rules...
	 *
	 * We must ignore the FREEZE vs SUSPEND distinction here, because
	 * otherwise the swsusp will save (and restore) garbage state.
	 */
	if (!(hcd->state == HC_STATE_SUSPENDED ||
			hcd->state == HC_STATE_HALT)) {
		dev_warn(&dev->dev, "Root hub is not suspended\n");
		retval = -EBUSY;
		goto done;
	}

	/* We might already be suspended (runtime PM -- not yet written) */
	if (dev->current_state != PCI_D0)
		goto done;

	if (hcd->driver->pci_suspend) {
		retval = hcd->driver->pci_suspend(hcd, message);
		suspend_report_result(hcd->driver->pci_suspend, retval);
		if (retval)
			goto done;
	}

	synchronize_irq(dev->irq);

	/* Downstream ports from this root hub should already be quiesced, so
	 * there will be no DMA activity.  Now we can shut down the upstream
	 * link (except maybe for PME# resume signaling) and enter some PCI
	 * low power state, if the hardware allows.
	 */
	pci_disable_device(dev);

	pci_save_state(dev);

	/* Don't fail on error to enable wakeup.  We rely on pci code
	 * to reject requests the hardware can't implement, rather
	 * than coding the same thing.
	 */
	wake = (hcd->state == HC_STATE_SUSPENDED &&
			device_may_wakeup(&dev->dev));
	w = pci_wake_from_d3(dev, wake);
	if (w < 0)
		wake = w;
	dev_dbg(&dev->dev, "wakeup: %d\n", wake);

	/* Don't change state if we don't need to */
	if (message.event == PM_EVENT_FREEZE ||
			message.event == PM_EVENT_PRETHAW) {
		dev_dbg(&dev->dev, "--> no state change\n");
		goto done;
	}

	has_pci_pm = pci_find_capability(dev, PCI_CAP_ID_PM);
	if (!has_pci_pm) {
		dev_dbg(&dev->dev, "--> PCI D0 legacy\n");
	} else {

		/* NOTE:  dev->current_state becomes nonzero only here, and
		 * only for devices that support PCI PM.  Also, exiting
		 * PCI_D3 (but not PCI_D1 or PCI_D2) is allowed to reset
		 * some device state (e.g. as part of clock reinit).
		 */
		retval = pci_set_power_state(dev, PCI_D3hot);
		suspend_report_result(pci_set_power_state, retval);
		if (retval == 0) {
			dev_dbg(&dev->dev, "--> PCI D3\n");
		} else {
			dev_dbg(&dev->dev, "PCI D3 suspend fail, %d\n",
					retval);
			pci_restore_state(dev);
		}
	}

#ifdef CONFIG_PPC_PMAC
	if (retval == 0) {
		/* Disable ASIC clocks for USB */
		if (machine_is(powermac)) {
			struct device_node	*of_node;

			of_node = pci_device_to_OF_node(dev);
			if (of_node)
				pmac_call_feature(PMAC_FTR_USB_ENABLE,
							of_node, 0, 0);
		}
	}
#endif

 done:
	return retval;
}
EXPORT_SYMBOL_GPL(usb_hcd_pci_suspend);

/**
 * usb_hcd_pci_resume - power management resume of a PCI-based HCD
 * @dev: USB Host Controller being resumed
 *
 * Store this function in the HCD's struct pci_driver as .resume.
 */
int usb_hcd_pci_resume(struct pci_dev *dev)
{
	struct usb_hcd		*hcd;
	int			retval;

#ifdef CONFIG_PPC_PMAC
	/* Reenable ASIC clocks for USB */
	if (machine_is(powermac)) {
		struct device_node *of_node;

		of_node = pci_device_to_OF_node(dev);
		if (of_node)
			pmac_call_feature(PMAC_FTR_USB_ENABLE,
						of_node, 0, 1);
	}
#endif

	pci_restore_state(dev);

	hcd = pci_get_drvdata(dev);
	if (hcd->state != HC_STATE_SUSPENDED) {
		dev_dbg(hcd->self.controller,
				"can't resume, not suspended!\n");
		return 0;
	}

	pci_enable_wake(dev, PCI_D0, false);

	retval = pci_enable_device(dev);
	if (retval < 0) {
		dev_err(&dev->dev, "can't re-enable after resume, %d!\n",
				retval);
		return retval;
	}

	pci_set_master(dev);

	/* yes, ignore this result too... */
	(void) pci_wake_from_d3(dev, 0);

	clear_bit(HCD_FLAG_SAW_IRQ, &hcd->flags);

	if (hcd->driver->pci_resume) {
		retval = hcd->driver->pci_resume(hcd);
		if (retval) {
			dev_err(hcd->self.controller,
				"PCI post-resume error %d!\n", retval);
			usb_hc_died(hcd);
		}
	}
	return retval;
}
EXPORT_SYMBOL_GPL(usb_hcd_pci_resume);

#endif	/* CONFIG_PM */

/**
 * usb_hcd_pci_shutdown - shutdown host controller
 * @dev: USB Host Controller being shutdown
 */
void usb_hcd_pci_shutdown(struct pci_dev *dev)
{
	struct usb_hcd		*hcd;

	hcd = pci_get_drvdata(dev);
	if (!hcd)
		return;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}
EXPORT_SYMBOL_GPL(usb_hcd_pci_shutdown);

