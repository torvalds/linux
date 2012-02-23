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
#include <linux/usb/hcd.h>

#include <asm/io.h>
#include <asm/irq.h>

#ifdef CONFIG_PPC_PMAC
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/pci-bridge.h>
#include <asm/prom.h>
#endif

#include "usb.h"


/* PCI-based HCs are common, but plenty of non-PCI HCs are used too */

#ifdef CONFIG_PM_SLEEP

/* Coordinate handoffs between EHCI and companion controllers
 * during system resume
 */

static DEFINE_MUTEX(companions_mutex);

#define CL_UHCI		PCI_CLASS_SERIAL_USB_UHCI
#define CL_OHCI		PCI_CLASS_SERIAL_USB_OHCI
#define CL_EHCI		PCI_CLASS_SERIAL_USB_EHCI

enum companion_action {
	SET_HS_COMPANION, CLEAR_HS_COMPANION, WAIT_FOR_COMPANIONS
};

static void companion_common(struct pci_dev *pdev, struct usb_hcd *hcd,
		enum companion_action action)
{
	struct pci_dev		*companion;
	struct usb_hcd		*companion_hcd;
	unsigned int		slot = PCI_SLOT(pdev->devfn);

	/* Iterate through other PCI functions in the same slot.
	 * If pdev is OHCI or UHCI then we are looking for EHCI, and
	 * vice versa.
	 */
	companion = NULL;
	for_each_pci_dev(companion) {
		if (companion->bus != pdev->bus ||
				PCI_SLOT(companion->devfn) != slot)
			continue;

		companion_hcd = pci_get_drvdata(companion);
		if (!companion_hcd)
			continue;

		/* For SET_HS_COMPANION, store a pointer to the EHCI bus in
		 * the OHCI/UHCI companion bus structure.
		 * For CLEAR_HS_COMPANION, clear the pointer to the EHCI bus
		 * in the OHCI/UHCI companion bus structure.
		 * For WAIT_FOR_COMPANIONS, wait until the OHCI/UHCI
		 * companion controllers have fully resumed.
		 */

		if ((pdev->class == CL_OHCI || pdev->class == CL_UHCI) &&
				companion->class == CL_EHCI) {
			/* action must be SET_HS_COMPANION */
			dev_dbg(&companion->dev, "HS companion for %s\n",
					dev_name(&pdev->dev));
			hcd->self.hs_companion = &companion_hcd->self;

		} else if (pdev->class == CL_EHCI &&
				(companion->class == CL_OHCI ||
				companion->class == CL_UHCI)) {
			switch (action) {
			case SET_HS_COMPANION:
				dev_dbg(&pdev->dev, "HS companion for %s\n",
						dev_name(&companion->dev));
				companion_hcd->self.hs_companion = &hcd->self;
				break;
			case CLEAR_HS_COMPANION:
				companion_hcd->self.hs_companion = NULL;
				break;
			case WAIT_FOR_COMPANIONS:
				device_pm_wait_for_dev(&pdev->dev,
						&companion->dev);
				break;
			}
		}
	}
}

static void set_hs_companion(struct pci_dev *pdev, struct usb_hcd *hcd)
{
	mutex_lock(&companions_mutex);
	dev_set_drvdata(&pdev->dev, hcd);
	companion_common(pdev, hcd, SET_HS_COMPANION);
	mutex_unlock(&companions_mutex);
}

static void clear_hs_companion(struct pci_dev *pdev, struct usb_hcd *hcd)
{
	mutex_lock(&companions_mutex);
	dev_set_drvdata(&pdev->dev, NULL);

	/* If pdev is OHCI or UHCI, just clear its hs_companion pointer */
	if (pdev->class == CL_OHCI || pdev->class == CL_UHCI)
		hcd->self.hs_companion = NULL;

	/* Otherwise search for companion buses and clear their pointers */
	else
		companion_common(pdev, hcd, CLEAR_HS_COMPANION);
	mutex_unlock(&companions_mutex);
}

static void wait_for_companions(struct pci_dev *pdev, struct usb_hcd *hcd)
{
	/* Only EHCI controllers need to wait.
	 * No locking is needed because a controller cannot be resumed
	 * while one of its companions is getting unbound.
	 */
	if (pdev->class == CL_EHCI)
		companion_common(pdev, hcd, WAIT_FOR_COMPANIONS);
}

#else /* !CONFIG_PM_SLEEP */

static inline void set_hs_companion(struct pci_dev *d, struct usb_hcd *h) {}
static inline void clear_hs_companion(struct pci_dev *d, struct usb_hcd *h) {}
static inline void wait_for_companions(struct pci_dev *d, struct usb_hcd *h) {}

#endif /* !CONFIG_PM_SLEEP */

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

	/* The xHCI driver supports MSI and MSI-X,
	 * so don't fail if the BIOS doesn't provide a legacy IRQ.
	 */
	if (!dev->irq && (driver->flags & HCD_MASK) != HCD_USB3) {
		dev_err(&dev->dev,
			"Found HC with no IRQ.  Check BIOS/PCI %s setup!\n",
			pci_name(dev));
		retval = -ENODEV;
		goto disable_pci;
	}

	hcd = usb_create_hcd(driver, &dev->dev, pci_name(dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto disable_pci;
	}

	if (driver->flags & HCD_MEMORY) {
		/* EHCI, OHCI */
		hcd->rsrc_start = pci_resource_start(dev, 0);
		hcd->rsrc_len = pci_resource_len(dev, 0);
		if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
			dev_dbg(&dev->dev, "controller already in use\n");
			retval = -EBUSY;
			goto clear_companion;
		}
		hcd->regs = ioremap_nocache(hcd->rsrc_start, hcd->rsrc_len);
		if (hcd->regs == NULL) {
			dev_dbg(&dev->dev, "error mapping memory\n");
			retval = -EFAULT;
			goto release_mem_region;
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
			goto clear_companion;
		}
	}

	pci_set_master(dev);

	retval = usb_add_hcd(hcd, dev->irq, IRQF_DISABLED | IRQF_SHARED);
	if (retval != 0)
		goto unmap_registers;
	set_hs_companion(dev, hcd);

	if (pci_dev_run_wake(dev))
		pm_runtime_put_noidle(&dev->dev);
	return retval;

unmap_registers:
	if (driver->flags & HCD_MEMORY) {
		iounmap(hcd->regs);
release_mem_region:
		release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	} else
		release_region(hcd->rsrc_start, hcd->rsrc_len);
clear_companion:
	clear_hs_companion(dev, hcd);
	usb_put_hcd(hcd);
disable_pci:
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

	if (pci_dev_run_wake(dev))
		pm_runtime_get_noresume(&dev->dev);

	/* Fake an interrupt request in order to give the driver a chance
	 * to test whether the controller hardware has been removed (e.g.,
	 * cardbus physical eject).
	 */
	local_irq_disable();
	usb_hcd_irq(0, hcd);
	local_irq_enable();

	usb_remove_hcd(hcd);
	if (hcd->driver->flags & HCD_MEMORY) {
		iounmap(hcd->regs);
		release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	} else {
		release_region(hcd->rsrc_start, hcd->rsrc_len);
	}
	clear_hs_companion(dev, hcd);
	usb_put_hcd(hcd);
	pci_disable_device(dev);
}
EXPORT_SYMBOL_GPL(usb_hcd_pci_remove);

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

	if (test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags) &&
			hcd->driver->shutdown) {
		hcd->driver->shutdown(hcd);
		pci_disable_device(dev);
	}
}
EXPORT_SYMBOL_GPL(usb_hcd_pci_shutdown);

#ifdef	CONFIG_PM

#ifdef	CONFIG_PPC_PMAC
static void powermac_set_asic(struct pci_dev *pci_dev, int enable)
{
	/* Enanble or disable ASIC clocks for USB */
	if (machine_is(powermac)) {
		struct device_node	*of_node;

		of_node = pci_device_to_OF_node(pci_dev);
		if (of_node)
			pmac_call_feature(PMAC_FTR_USB_ENABLE,
					of_node, 0, enable);
	}
}

#else

static inline void powermac_set_asic(struct pci_dev *pci_dev, int enable)
{}

#endif	/* CONFIG_PPC_PMAC */

static int check_root_hub_suspended(struct device *dev)
{
	struct pci_dev		*pci_dev = to_pci_dev(dev);
	struct usb_hcd		*hcd = pci_get_drvdata(pci_dev);

	if (HCD_RH_RUNNING(hcd)) {
		dev_warn(dev, "Root hub is not suspended\n");
		return -EBUSY;
	}
	if (hcd->shared_hcd) {
		hcd = hcd->shared_hcd;
		if (HCD_RH_RUNNING(hcd)) {
			dev_warn(dev, "Secondary root hub is not suspended\n");
			return -EBUSY;
		}
	}
	return 0;
}

static int suspend_common(struct device *dev, bool do_wakeup)
{
	struct pci_dev		*pci_dev = to_pci_dev(dev);
	struct usb_hcd		*hcd = pci_get_drvdata(pci_dev);
	int			retval;

	/* Root hub suspend should have stopped all downstream traffic,
	 * and all bus master traffic.  And done so for both the interface
	 * and the stub usb_device (which we check here).  But maybe it
	 * didn't; writing sysfs power/state files ignores such rules...
	 */
	retval = check_root_hub_suspended(dev);
	if (retval)
		return retval;

	if (hcd->driver->pci_suspend && !HCD_DEAD(hcd)) {
		/* Optimization: Don't suspend if a root-hub wakeup is
		 * pending and it would cause the HCD to wake up anyway.
		 */
		if (do_wakeup && HCD_WAKEUP_PENDING(hcd))
			return -EBUSY;
		if (do_wakeup && hcd->shared_hcd &&
				HCD_WAKEUP_PENDING(hcd->shared_hcd))
			return -EBUSY;
		retval = hcd->driver->pci_suspend(hcd, do_wakeup);
		suspend_report_result(hcd->driver->pci_suspend, retval);

		/* Check again in case wakeup raced with pci_suspend */
		if ((retval == 0 && do_wakeup && HCD_WAKEUP_PENDING(hcd)) ||
				(retval == 0 && do_wakeup && hcd->shared_hcd &&
				 HCD_WAKEUP_PENDING(hcd->shared_hcd))) {
			if (hcd->driver->pci_resume)
				hcd->driver->pci_resume(hcd, false);
			retval = -EBUSY;
		}
		if (retval)
			return retval;
	}

	/* If MSI-X is enabled, the driver will have synchronized all vectors
	 * in pci_suspend(). If MSI or legacy PCI is enabled, that will be
	 * synchronized here.
	 */
	if (!hcd->msix_enabled)
		synchronize_irq(pci_dev->irq);

	/* Downstream ports from this root hub should already be quiesced, so
	 * there will be no DMA activity.  Now we can shut down the upstream
	 * link (except maybe for PME# resume signaling).  We'll enter a
	 * low power state during suspend_noirq, if the hardware allows.
	 */
	pci_disable_device(pci_dev);
	return retval;
}

static int resume_common(struct device *dev, int event)
{
	struct pci_dev		*pci_dev = to_pci_dev(dev);
	struct usb_hcd		*hcd = pci_get_drvdata(pci_dev);
	int			retval;

	if (HCD_RH_RUNNING(hcd) ||
			(hcd->shared_hcd &&
			 HCD_RH_RUNNING(hcd->shared_hcd))) {
		dev_dbg(dev, "can't resume, not suspended!\n");
		return 0;
	}

	retval = pci_enable_device(pci_dev);
	if (retval < 0) {
		dev_err(dev, "can't re-enable after resume, %d!\n", retval);
		return retval;
	}

	pci_set_master(pci_dev);

	clear_bit(HCD_FLAG_SAW_IRQ, &hcd->flags);
	if (hcd->shared_hcd)
		clear_bit(HCD_FLAG_SAW_IRQ, &hcd->shared_hcd->flags);

	if (hcd->driver->pci_resume && !HCD_DEAD(hcd)) {
		if (event != PM_EVENT_AUTO_RESUME)
			wait_for_companions(pci_dev, hcd);

		retval = hcd->driver->pci_resume(hcd,
				event == PM_EVENT_RESTORE);
		if (retval) {
			dev_err(dev, "PCI post-resume error %d!\n", retval);
			if (hcd->shared_hcd)
				usb_hc_died(hcd->shared_hcd);
			usb_hc_died(hcd);
		}
	}
	return retval;
}

#ifdef	CONFIG_PM_SLEEP

static int hcd_pci_suspend(struct device *dev)
{
	return suspend_common(dev, device_may_wakeup(dev));
}

static int hcd_pci_suspend_noirq(struct device *dev)
{
	struct pci_dev		*pci_dev = to_pci_dev(dev);
	struct usb_hcd		*hcd = pci_get_drvdata(pci_dev);
	int			retval;

	retval = check_root_hub_suspended(dev);
	if (retval)
		return retval;

	pci_save_state(pci_dev);

	/* If the root hub is dead rather than suspended, disallow remote
	 * wakeup.  usb_hc_died() should ensure that both hosts are marked as
	 * dying, so we only need to check the primary roothub.
	 */
	if (HCD_DEAD(hcd))
		device_set_wakeup_enable(dev, 0);
	dev_dbg(dev, "wakeup: %d\n", device_may_wakeup(dev));

	/* Possibly enable remote wakeup,
	 * choose the appropriate low-power state, and go to that state.
	 */
	retval = pci_prepare_to_sleep(pci_dev);
	if (retval == -EIO) {		/* Low-power not supported */
		dev_dbg(dev, "--> PCI D0 legacy\n");
		retval = 0;
	} else if (retval == 0) {
		dev_dbg(dev, "--> PCI %s\n",
				pci_power_name(pci_dev->current_state));
	} else {
		suspend_report_result(pci_prepare_to_sleep, retval);
		return retval;
	}

	powermac_set_asic(pci_dev, 0);
	return retval;
}

static int hcd_pci_resume_noirq(struct device *dev)
{
	struct pci_dev		*pci_dev = to_pci_dev(dev);

	powermac_set_asic(pci_dev, 1);

	/* Go back to D0 and disable remote wakeup */
	pci_back_from_sleep(pci_dev);
	return 0;
}

static int hcd_pci_resume(struct device *dev)
{
	return resume_common(dev, PM_EVENT_RESUME);
}

static int hcd_pci_restore(struct device *dev)
{
	return resume_common(dev, PM_EVENT_RESTORE);
}

#else

#define hcd_pci_suspend		NULL
#define hcd_pci_suspend_noirq	NULL
#define hcd_pci_resume_noirq	NULL
#define hcd_pci_resume		NULL
#define hcd_pci_restore		NULL

#endif	/* CONFIG_PM_SLEEP */

#ifdef	CONFIG_PM_RUNTIME

static int hcd_pci_runtime_suspend(struct device *dev)
{
	int	retval;

	retval = suspend_common(dev, true);
	if (retval == 0)
		powermac_set_asic(to_pci_dev(dev), 0);
	dev_dbg(dev, "hcd_pci_runtime_suspend: %d\n", retval);
	return retval;
}

static int hcd_pci_runtime_resume(struct device *dev)
{
	int	retval;

	powermac_set_asic(to_pci_dev(dev), 1);
	retval = resume_common(dev, PM_EVENT_AUTO_RESUME);
	dev_dbg(dev, "hcd_pci_runtime_resume: %d\n", retval);
	return retval;
}

#else

#define hcd_pci_runtime_suspend	NULL
#define hcd_pci_runtime_resume	NULL

#endif	/* CONFIG_PM_RUNTIME */

const struct dev_pm_ops usb_hcd_pci_pm_ops = {
	.suspend	= hcd_pci_suspend,
	.suspend_noirq	= hcd_pci_suspend_noirq,
	.resume_noirq	= hcd_pci_resume_noirq,
	.resume		= hcd_pci_resume,
	.freeze		= check_root_hub_suspended,
	.freeze_noirq	= check_root_hub_suspended,
	.thaw_noirq	= NULL,
	.thaw		= NULL,
	.poweroff	= hcd_pci_suspend,
	.poweroff_noirq	= hcd_pci_suspend_noirq,
	.restore_noirq	= hcd_pci_resume_noirq,
	.restore	= hcd_pci_restore,
	.runtime_suspend = hcd_pci_runtime_suspend,
	.runtime_resume	= hcd_pci_runtime_resume,
};
EXPORT_SYMBOL_GPL(usb_hcd_pci_pm_ops);

#endif	/* CONFIG_PM */
