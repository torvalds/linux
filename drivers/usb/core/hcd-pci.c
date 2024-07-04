// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright David Brownell 2000-2002
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
#endif

#include "usb.h"


/* PCI-based HCs are common, but plenty of non-PCI HCs are used too */

/*
 * Coordinate handoffs between EHCI and companion controllers
 * during EHCI probing and system resume.
 */

static DECLARE_RWSEM(companions_rwsem);

#define CL_UHCI		PCI_CLASS_SERIAL_USB_UHCI
#define CL_OHCI		PCI_CLASS_SERIAL_USB_OHCI
#define CL_EHCI		PCI_CLASS_SERIAL_USB_EHCI

static inline int is_ohci_or_uhci(struct pci_dev *pdev)
{
	return pdev->class == CL_OHCI || pdev->class == CL_UHCI;
}

typedef void (*companion_fn)(struct pci_dev *pdev, struct usb_hcd *hcd,
		struct pci_dev *companion, struct usb_hcd *companion_hcd);

/* Iterate over PCI devices in the same slot as pdev and call fn for each */
static void for_each_companion(struct pci_dev *pdev, struct usb_hcd *hcd,
		companion_fn fn)
{
	struct pci_dev		*companion;
	struct usb_hcd		*companion_hcd;
	unsigned int		slot = PCI_SLOT(pdev->devfn);

	/*
	 * Iterate through other PCI functions in the same slot.
	 * If the function's drvdata isn't set then it isn't bound to
	 * a USB host controller driver, so skip it.
	 */
	companion = NULL;
	for_each_pci_dev(companion) {
		if (companion->bus != pdev->bus ||
				PCI_SLOT(companion->devfn) != slot)
			continue;

		/*
		 * Companion device should be either UHCI,OHCI or EHCI host
		 * controller, otherwise skip.
		 */
		if (companion->class != CL_UHCI && companion->class != CL_OHCI &&
				companion->class != CL_EHCI)
			continue;

		companion_hcd = pci_get_drvdata(companion);
		if (!companion_hcd || !companion_hcd->self.root_hub)
			continue;
		fn(pdev, hcd, companion, companion_hcd);
	}
}

/*
 * We're about to add an EHCI controller, which will unceremoniously grab
 * all the port connections away from its companions.  To prevent annoying
 * error messages, lock the companion's root hub and gracefully unconfigure
 * it beforehand.  Leave it locked until the EHCI controller is all set.
 */
static void ehci_pre_add(struct pci_dev *pdev, struct usb_hcd *hcd,
		struct pci_dev *companion, struct usb_hcd *companion_hcd)
{
	struct usb_device *udev;

	if (is_ohci_or_uhci(companion)) {
		udev = companion_hcd->self.root_hub;
		usb_lock_device(udev);
		usb_set_configuration(udev, 0);
	}
}

/*
 * Adding the EHCI controller has either succeeded or failed.  Set the
 * companion pointer accordingly, and in either case, reconfigure and
 * unlock the root hub.
 */
static void ehci_post_add(struct pci_dev *pdev, struct usb_hcd *hcd,
		struct pci_dev *companion, struct usb_hcd *companion_hcd)
{
	struct usb_device *udev;

	if (is_ohci_or_uhci(companion)) {
		if (dev_get_drvdata(&pdev->dev)) {	/* Succeeded */
			dev_dbg(&pdev->dev, "HS companion for %s\n",
					dev_name(&companion->dev));
			companion_hcd->self.hs_companion = &hcd->self;
		}
		udev = companion_hcd->self.root_hub;
		usb_set_configuration(udev, 1);
		usb_unlock_device(udev);
	}
}

/*
 * We just added a non-EHCI controller.  Find the EHCI controller to
 * which it is a companion, and store a pointer to the bus structure.
 */
static void non_ehci_add(struct pci_dev *pdev, struct usb_hcd *hcd,
		struct pci_dev *companion, struct usb_hcd *companion_hcd)
{
	if (is_ohci_or_uhci(pdev) && companion->class == CL_EHCI) {
		dev_dbg(&pdev->dev, "FS/LS companion for %s\n",
				dev_name(&companion->dev));
		hcd->self.hs_companion = &companion_hcd->self;
	}
}

/* We are removing an EHCI controller.  Clear the companions' pointers. */
static void ehci_remove(struct pci_dev *pdev, struct usb_hcd *hcd,
		struct pci_dev *companion, struct usb_hcd *companion_hcd)
{
	if (is_ohci_or_uhci(companion))
		companion_hcd->self.hs_companion = NULL;
}

#ifdef	CONFIG_PM

/* An EHCI controller must wait for its companions before resuming. */
static void ehci_wait_for_companions(struct pci_dev *pdev, struct usb_hcd *hcd,
		struct pci_dev *companion, struct usb_hcd *companion_hcd)
{
	if (is_ohci_or_uhci(companion))
		device_pm_wait_for_dev(&pdev->dev, &companion->dev);
}

#endif	/* CONFIG_PM */

/*-------------------------------------------------------------------------*/

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_hcd_pci_probe - initialize PCI-based HCDs
 * @dev: USB Host Controller being probed
 * @driver: USB HC driver handle
 *
 * Context: task context, might sleep
 *
 * Allocates basic PCI resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 * Store this function in the HCD's struct pci_driver as probe().
 *
 * Return: 0 if successful.
 */
int usb_hcd_pci_probe(struct pci_dev *dev, const struct hc_driver *driver)
{
	struct usb_hcd		*hcd;
	int			retval;
	int			hcd_irq = 0;

	if (usb_disabled())
		return -ENODEV;

	if (!driver)
		return -EINVAL;

	if (pci_enable_device(dev) < 0)
		return -ENODEV;

	/*
	 * The xHCI driver has its own irq management
	 * make sure irq setup is not touched for xhci in generic hcd code
	 */
	if ((driver->flags & HCD_MASK) < HCD_USB3) {
		retval = pci_alloc_irq_vectors(dev, 1, 1,
					       PCI_IRQ_INTX | PCI_IRQ_MSI);
		if (retval < 0) {
			dev_err(&dev->dev,
			"Found HC with no IRQ. Check BIOS/PCI %s setup!\n",
				pci_name(dev));
			retval = -ENODEV;
			goto disable_pci;
		}
		hcd_irq = pci_irq_vector(dev, 0);
	}

	hcd = usb_create_hcd(driver, &dev->dev, pci_name(dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto free_irq_vectors;
	}

	hcd->amd_resume_bug = usb_hcd_amd_resume_bug(dev, driver);

	if (driver->flags & HCD_MEMORY) {
		/* EHCI, OHCI */
		hcd->rsrc_start = pci_resource_start(dev, 0);
		hcd->rsrc_len = pci_resource_len(dev, 0);
		if (!devm_request_mem_region(&dev->dev, hcd->rsrc_start,
				hcd->rsrc_len, driver->description)) {
			dev_dbg(&dev->dev, "controller already in use\n");
			retval = -EBUSY;
			goto put_hcd;
		}
		hcd->regs = devm_ioremap(&dev->dev, hcd->rsrc_start,
				hcd->rsrc_len);
		if (hcd->regs == NULL) {
			dev_dbg(&dev->dev, "error mapping memory\n");
			retval = -EFAULT;
			goto put_hcd;
		}

	} else {
		/* UHCI */
		int	region;

		for (region = 0; region < PCI_STD_NUM_BARS; region++) {
			if (!(pci_resource_flags(dev, region) &
					IORESOURCE_IO))
				continue;

			hcd->rsrc_start = pci_resource_start(dev, region);
			hcd->rsrc_len = pci_resource_len(dev, region);
			if (devm_request_region(&dev->dev, hcd->rsrc_start,
					hcd->rsrc_len, driver->description))
				break;
		}
		if (region == PCI_STD_NUM_BARS) {
			dev_dbg(&dev->dev, "no i/o regions available\n");
			retval = -EBUSY;
			goto put_hcd;
		}
	}

	pci_set_master(dev);

	/* Note: dev_set_drvdata must be called while holding the rwsem */
	if (dev->class == CL_EHCI) {
		down_write(&companions_rwsem);
		dev_set_drvdata(&dev->dev, hcd);
		for_each_companion(dev, hcd, ehci_pre_add);
		retval = usb_add_hcd(hcd, hcd_irq, IRQF_SHARED);
		if (retval != 0)
			dev_set_drvdata(&dev->dev, NULL);
		for_each_companion(dev, hcd, ehci_post_add);
		up_write(&companions_rwsem);
	} else {
		down_read(&companions_rwsem);
		dev_set_drvdata(&dev->dev, hcd);
		retval = usb_add_hcd(hcd, hcd_irq, IRQF_SHARED);
		if (retval != 0)
			dev_set_drvdata(&dev->dev, NULL);
		else
			for_each_companion(dev, hcd, non_ehci_add);
		up_read(&companions_rwsem);
	}

	if (retval != 0)
		goto put_hcd;
	device_wakeup_enable(hcd->self.controller);

	if (pci_dev_run_wake(dev))
		pm_runtime_put_noidle(&dev->dev);
	return retval;

put_hcd:
	usb_put_hcd(hcd);
free_irq_vectors:
	if ((driver->flags & HCD_MASK) < HCD_USB3)
		pci_free_irq_vectors(dev);
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
 *
 * Context: task context, might sleep
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
	int			hcd_driver_flags;

	hcd = pci_get_drvdata(dev);
	if (!hcd)
		return;

	hcd_driver_flags = hcd->driver->flags;

	if (pci_dev_run_wake(dev))
		pm_runtime_get_noresume(&dev->dev);

	/* Fake an interrupt request in order to give the driver a chance
	 * to test whether the controller hardware has been removed (e.g.,
	 * cardbus physical eject).
	 */
	local_irq_disable();
	usb_hcd_irq(0, hcd);
	local_irq_enable();

	/* Note: dev_set_drvdata must be called while holding the rwsem */
	if (dev->class == CL_EHCI) {
		down_write(&companions_rwsem);
		for_each_companion(dev, hcd, ehci_remove);
		usb_remove_hcd(hcd);
		dev_set_drvdata(&dev->dev, NULL);
		up_write(&companions_rwsem);
	} else {
		/* Not EHCI; just clear the companion pointer */
		down_read(&companions_rwsem);
		hcd->self.hs_companion = NULL;
		usb_remove_hcd(hcd);
		dev_set_drvdata(&dev->dev, NULL);
		up_read(&companions_rwsem);
	}
	usb_put_hcd(hcd);
	if ((hcd_driver_flags & HCD_MASK) < HCD_USB3)
		pci_free_irq_vectors(dev);
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
		if (usb_hcd_is_primary_hcd(hcd) && hcd->irq > 0)
			free_irq(hcd->irq, hcd);
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
	struct usb_hcd		*hcd = dev_get_drvdata(dev);

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

static int suspend_common(struct device *dev, pm_message_t msg)
{
	struct pci_dev		*pci_dev = to_pci_dev(dev);
	struct usb_hcd		*hcd = pci_get_drvdata(pci_dev);
	bool			do_wakeup;
	int			retval;

	do_wakeup = PMSG_IS_AUTO(msg) ? true : device_may_wakeup(dev);

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
		suspend_report_result(dev, hcd->driver->pci_suspend, retval);

		/* Check again in case wakeup raced with pci_suspend */
		if ((retval == 0 && do_wakeup && HCD_WAKEUP_PENDING(hcd)) ||
				(retval == 0 && do_wakeup && hcd->shared_hcd &&
				 HCD_WAKEUP_PENDING(hcd->shared_hcd))) {
			if (hcd->driver->pci_resume)
				hcd->driver->pci_resume(hcd, msg);
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
		synchronize_irq(pci_irq_vector(pci_dev, 0));

	/* Downstream ports from this root hub should already be quiesced, so
	 * there will be no DMA activity.  Now we can shut down the upstream
	 * link (except maybe for PME# resume signaling).  We'll enter a
	 * low power state during suspend_noirq, if the hardware allows.
	 */
	pci_disable_device(pci_dev);
	return retval;
}

static int resume_common(struct device *dev, pm_message_t msg)
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

	if (hcd->driver->pci_resume && !HCD_DEAD(hcd)) {

		/*
		 * Only EHCI controllers have to wait for their companions.
		 * No locking is needed because PCI controller drivers do not
		 * get unbound during system resume.
		 */
		if (pci_dev->class == CL_EHCI && msg.event != PM_EVENT_AUTO_RESUME)
			for_each_companion(pci_dev, hcd,
					ehci_wait_for_companions);

		retval = hcd->driver->pci_resume(hcd, msg);
		if (retval) {
			dev_err(dev, "PCI post-resume error %d!\n", retval);
			usb_hc_died(hcd);
		}
	}
	return retval;
}

#ifdef	CONFIG_PM_SLEEP

static int hcd_pci_suspend(struct device *dev)
{
	return suspend_common(dev, PMSG_SUSPEND);
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
		suspend_report_result(dev, pci_prepare_to_sleep, retval);
		return retval;
	}

	powermac_set_asic(pci_dev, 0);
	return retval;
}

static int hcd_pci_poweroff_late(struct device *dev)
{
	struct pci_dev		*pci_dev = to_pci_dev(dev);
	struct usb_hcd		*hcd = pci_get_drvdata(pci_dev);

	if (hcd->driver->pci_poweroff_late && !HCD_DEAD(hcd))
		return hcd->driver->pci_poweroff_late(hcd, device_may_wakeup(dev));

	return 0;
}

static int hcd_pci_resume_noirq(struct device *dev)
{
	powermac_set_asic(to_pci_dev(dev), 1);
	return 0;
}

static int hcd_pci_resume(struct device *dev)
{
	return resume_common(dev, PMSG_RESUME);
}

static int hcd_pci_restore(struct device *dev)
{
	return resume_common(dev, PMSG_RESTORE);
}

#else

#define hcd_pci_suspend		NULL
#define hcd_pci_suspend_noirq	NULL
#define hcd_pci_poweroff_late	NULL
#define hcd_pci_resume_noirq	NULL
#define hcd_pci_resume		NULL
#define hcd_pci_restore		NULL

#endif	/* CONFIG_PM_SLEEP */

static int hcd_pci_runtime_suspend(struct device *dev)
{
	int	retval;

	retval = suspend_common(dev, PMSG_AUTO_SUSPEND);
	if (retval == 0)
		powermac_set_asic(to_pci_dev(dev), 0);
	dev_dbg(dev, "hcd_pci_runtime_suspend: %d\n", retval);
	return retval;
}

static int hcd_pci_runtime_resume(struct device *dev)
{
	int	retval;

	powermac_set_asic(to_pci_dev(dev), 1);
	retval = resume_common(dev, PMSG_AUTO_RESUME);
	dev_dbg(dev, "hcd_pci_runtime_resume: %d\n", retval);
	return retval;
}

const struct dev_pm_ops usb_hcd_pci_pm_ops = {
	.suspend	= hcd_pci_suspend,
	.suspend_noirq	= hcd_pci_suspend_noirq,
	.resume_noirq	= hcd_pci_resume_noirq,
	.resume		= hcd_pci_resume,
	.freeze		= hcd_pci_suspend,
	.freeze_noirq	= check_root_hub_suspended,
	.thaw_noirq	= NULL,
	.thaw		= hcd_pci_resume,
	.poweroff	= hcd_pci_suspend,
	.poweroff_late	= hcd_pci_poweroff_late,
	.poweroff_noirq	= hcd_pci_suspend_noirq,
	.restore_noirq	= hcd_pci_resume_noirq,
	.restore	= hcd_pci_restore,
	.runtime_suspend = hcd_pci_runtime_suspend,
	.runtime_resume	= hcd_pci_runtime_resume,
};
EXPORT_SYMBOL_GPL(usb_hcd_pci_pm_ops);

#endif	/* CONFIG_PM */
