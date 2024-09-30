/*
 * PCI Stub Driver - Grabs devices in backend to be exported later
 *
 * Ryan Wilson <hap9@epoch.ncsc.mil>
 * Chris Bookholt <hap10@epoch.ncsc.mil>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/rwsem.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <xen/events.h>
#include <xen/pci.h>
#include <xen/xen.h>
#ifdef CONFIG_XEN_ACPI
#include <xen/acpi.h>
#endif
#include <asm/xen/hypervisor.h>
#include <xen/interface/physdev.h>
#include "pciback.h"
#include "conf_space.h"
#include "conf_space_quirks.h"

#define PCISTUB_DRIVER_NAME "pciback"

static char *pci_devs_to_hide;
wait_queue_head_t xen_pcibk_aer_wait_queue;
/*Add sem for sync AER handling and xen_pcibk remove/reconfigue ops,
* We want to avoid in middle of AER ops, xen_pcibk devices is being removed
*/
static DECLARE_RWSEM(pcistub_sem);
module_param_named(hide, pci_devs_to_hide, charp, 0444);

struct pcistub_device_id {
	struct list_head slot_list;
	int domain;
	unsigned char bus;
	unsigned int devfn;
};
static LIST_HEAD(pcistub_device_ids);
static DEFINE_SPINLOCK(device_ids_lock);

struct pcistub_device {
	struct kref kref;
	struct list_head dev_list;
	spinlock_t lock;

	struct pci_dev *dev;
	struct xen_pcibk_device *pdev;/* non-NULL if struct pci_dev is in use */
#ifdef CONFIG_XEN_ACPI
	int gsi;
#endif
};

/* Access to pcistub_devices & seized_devices lists and the initialize_devices
 * flag must be locked with pcistub_devices_lock
 */
static DEFINE_SPINLOCK(pcistub_devices_lock);
static LIST_HEAD(pcistub_devices);

/* wait for device_initcall before initializing our devices
 * (see pcistub_init_devices_late)
 */
static int initialize_devices;
static LIST_HEAD(seized_devices);

static struct pcistub_device *pcistub_device_alloc(struct pci_dev *dev)
{
	struct pcistub_device *psdev;

	dev_dbg(&dev->dev, "pcistub_device_alloc\n");

	psdev = kzalloc(sizeof(*psdev), GFP_KERNEL);
	if (!psdev)
		return NULL;

	psdev->dev = pci_dev_get(dev);
	if (!psdev->dev) {
		kfree(psdev);
		return NULL;
	}

	kref_init(&psdev->kref);
	spin_lock_init(&psdev->lock);
#ifdef CONFIG_XEN_ACPI
	psdev->gsi = -1;
#endif

	return psdev;
}

static int pcistub_reset_device_state(struct pci_dev *dev)
{
	__pci_reset_function_locked(dev);

	if (!xen_pv_domain())
		return xen_reset_device(dev);
	else
		return 0;
}

/* Don't call this directly as it's called by pcistub_device_put */
static void pcistub_device_release(struct kref *kref)
{
	struct pcistub_device *psdev;
	struct pci_dev *dev;
	struct xen_pcibk_dev_data *dev_data;

	psdev = container_of(kref, struct pcistub_device, kref);
	dev = psdev->dev;
	dev_data = pci_get_drvdata(dev);

	dev_dbg(&dev->dev, "pcistub_device_release\n");

	xen_unregister_device_domain_owner(dev);

	/* Call the reset function which does not take lock as this
	 * is called from "unbind" which takes a device_lock mutex.
	 */
	pcistub_reset_device_state(dev);
	if (dev_data &&
	    pci_load_and_free_saved_state(dev, &dev_data->pci_saved_state))
		dev_info(&dev->dev, "Could not reload PCI state\n");
	else
		pci_restore_state(dev);

	if (dev->msix_cap) {
		struct physdev_pci_device ppdev = {
			.seg = pci_domain_nr(dev->bus),
			.bus = dev->bus->number,
			.devfn = dev->devfn
		};
		int err = HYPERVISOR_physdev_op(PHYSDEVOP_release_msix,
						&ppdev);

		if (err && err != -ENOSYS)
			dev_warn(&dev->dev, "MSI-X release failed (%d)\n",
				 err);
	}

	/* Disable the device */
	xen_pcibk_reset_device(dev);

	kfree(dev_data);
	pci_set_drvdata(dev, NULL);

	/* Clean-up the device */
	xen_pcibk_config_free_dyn_fields(dev);
	xen_pcibk_config_free_dev(dev);

	pci_clear_dev_assigned(dev);
	pci_dev_put(dev);

	kfree(psdev);
}

static inline void pcistub_device_get(struct pcistub_device *psdev)
{
	kref_get(&psdev->kref);
}

static inline void pcistub_device_put(struct pcistub_device *psdev)
{
	kref_put(&psdev->kref, pcistub_device_release);
}

static struct pcistub_device *pcistub_device_find_locked(int domain, int bus,
							 int slot, int func)
{
	struct pcistub_device *psdev;

	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (psdev->dev != NULL
		    && domain == pci_domain_nr(psdev->dev->bus)
		    && bus == psdev->dev->bus->number
		    && slot == PCI_SLOT(psdev->dev->devfn)
		    && func == PCI_FUNC(psdev->dev->devfn)) {
			return psdev;
		}
	}

	return NULL;
}

static struct pcistub_device *pcistub_device_find(int domain, int bus,
						  int slot, int func)
{
	struct pcistub_device *psdev;
	unsigned long flags;

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	psdev = pcistub_device_find_locked(domain, bus, slot, func);
	if (psdev)
		pcistub_device_get(psdev);

	spin_unlock_irqrestore(&pcistub_devices_lock, flags);
	return psdev;
}

static struct pci_dev *pcistub_device_get_pci_dev(struct xen_pcibk_device *pdev,
						  struct pcistub_device *psdev)
{
	struct pci_dev *pci_dev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&psdev->lock, flags);
	if (!psdev->pdev) {
		psdev->pdev = pdev;
		pci_dev = psdev->dev;
	}
	spin_unlock_irqrestore(&psdev->lock, flags);

	if (pci_dev)
		pcistub_device_get(psdev);

	return pci_dev;
}

#ifdef CONFIG_XEN_ACPI
int pcistub_get_gsi_from_sbdf(unsigned int sbdf)
{
	struct pcistub_device *psdev;
	int domain = (sbdf >> 16) & 0xffff;
	int bus = PCI_BUS_NUM(sbdf);
	int slot = PCI_SLOT(sbdf);
	int func = PCI_FUNC(sbdf);

	psdev = pcistub_device_find(domain, bus, slot, func);

	if (!psdev)
		return -ENODEV;

	return psdev->gsi;
}
EXPORT_SYMBOL_GPL(pcistub_get_gsi_from_sbdf);
#endif

struct pci_dev *pcistub_get_pci_dev_by_slot(struct xen_pcibk_device *pdev,
					    int domain, int bus,
					    int slot, int func)
{
	struct pcistub_device *psdev;
	struct pci_dev *found_dev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	psdev = pcistub_device_find_locked(domain, bus, slot, func);
	if (psdev)
		found_dev = pcistub_device_get_pci_dev(pdev, psdev);

	spin_unlock_irqrestore(&pcistub_devices_lock, flags);
	return found_dev;
}

struct pci_dev *pcistub_get_pci_dev(struct xen_pcibk_device *pdev,
				    struct pci_dev *dev)
{
	struct pcistub_device *psdev;
	struct pci_dev *found_dev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (psdev->dev == dev) {
			found_dev = pcistub_device_get_pci_dev(pdev, psdev);
			break;
		}
	}

	spin_unlock_irqrestore(&pcistub_devices_lock, flags);
	return found_dev;
}

/*
 * Called when:
 *  - XenBus state has been reconfigure (pci unplug). See xen_pcibk_remove_device
 *  - XenBus state has been disconnected (guest shutdown). See xen_pcibk_xenbus_remove
 *  - 'echo BDF > unbind' on pciback module with no guest attached. See pcistub_remove
 *  - 'echo BDF > unbind' with a guest still using it. See pcistub_remove
 *
 *  As such we have to be careful.
 *
 *  To make this easier, the caller has to hold the device lock.
 */
void pcistub_put_pci_dev(struct pci_dev *dev)
{
	struct pcistub_device *psdev, *found_psdev = NULL;
	unsigned long flags;
	struct xen_pcibk_dev_data *dev_data;
	int ret;

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (psdev->dev == dev) {
			found_psdev = psdev;
			break;
		}
	}

	spin_unlock_irqrestore(&pcistub_devices_lock, flags);
	if (WARN_ON(!found_psdev))
		return;

	/*hold this lock for avoiding breaking link between
	* pcistub and xen_pcibk when AER is in processing
	*/
	down_write(&pcistub_sem);
	/* Cleanup our device
	 * (so it's ready for the next domain)
	 */
	device_lock_assert(&dev->dev);
	pcistub_reset_device_state(dev);

	dev_data = pci_get_drvdata(dev);
	ret = pci_load_saved_state(dev, dev_data->pci_saved_state);
	if (!ret) {
		/*
		 * The usual sequence is pci_save_state & pci_restore_state
		 * but the guest might have messed the configuration space up.
		 * Use the initial version (when device was bound to us).
		 */
		pci_restore_state(dev);
	} else
		dev_info(&dev->dev, "Could not reload PCI state\n");
	/* This disables the device. */
	xen_pcibk_reset_device(dev);

	/* And cleanup up our emulated fields. */
	xen_pcibk_config_reset_dev(dev);
	xen_pcibk_config_free_dyn_fields(dev);

	dev_data->allow_interrupt_control = 0;

	xen_unregister_device_domain_owner(dev);

	spin_lock_irqsave(&found_psdev->lock, flags);
	found_psdev->pdev = NULL;
	spin_unlock_irqrestore(&found_psdev->lock, flags);

	pcistub_device_put(found_psdev);
	up_write(&pcistub_sem);
}

static int pcistub_match_one(struct pci_dev *dev,
			     struct pcistub_device_id *pdev_id)
{
	/* Match the specified device by domain, bus, slot, func and also if
	 * any of the device's parent bridges match.
	 */
	for (; dev != NULL; dev = dev->bus->self) {
		if (pci_domain_nr(dev->bus) == pdev_id->domain
		    && dev->bus->number == pdev_id->bus
		    && dev->devfn == pdev_id->devfn)
			return 1;

		/* Sometimes topmost bridge links to itself. */
		if (dev == dev->bus->self)
			break;
	}

	return 0;
}

static int pcistub_match(struct pci_dev *dev)
{
	struct pcistub_device_id *pdev_id;
	unsigned long flags;
	int found = 0;

	spin_lock_irqsave(&device_ids_lock, flags);
	list_for_each_entry(pdev_id, &pcistub_device_ids, slot_list) {
		if (pcistub_match_one(dev, pdev_id)) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&device_ids_lock, flags);

	return found;
}

static int pcistub_init_device(struct pcistub_device *psdev)
{
	struct xen_pcibk_dev_data *dev_data;
	struct pci_dev *dev;
#ifdef CONFIG_XEN_ACPI
	int gsi, trigger, polarity;
#endif
	int err = 0;

	if (!psdev)
		return -EINVAL;

	dev = psdev->dev;

	dev_dbg(&dev->dev, "initializing...\n");

	/* The PCI backend is not intended to be a module (or to work with
	 * removable PCI devices (yet). If it were, xen_pcibk_config_free()
	 * would need to be called somewhere to free the memory allocated
	 * here and then to call kfree(pci_get_drvdata(psdev->dev)).
	 */
	dev_data = kzalloc(sizeof(*dev_data) +  strlen(DRV_NAME "[]")
				+ strlen(pci_name(dev)) + 1, GFP_KERNEL);
	if (!dev_data) {
		err = -ENOMEM;
		goto out;
	}
	pci_set_drvdata(dev, dev_data);

	/*
	 * Setup name for fake IRQ handler. It will only be enabled
	 * once the device is turned on by the guest.
	 */
	sprintf(dev_data->irq_name, DRV_NAME "[%s]", pci_name(dev));

	dev_dbg(&dev->dev, "initializing config\n");

	init_waitqueue_head(&xen_pcibk_aer_wait_queue);
	err = xen_pcibk_config_init_dev(dev);
	if (err)
		goto out;

	/* HACK: Force device (& ACPI) to determine what IRQ it's on - we
	 * must do this here because pcibios_enable_device may specify
	 * the pci device's true irq (and possibly its other resources)
	 * if they differ from what's in the configuration space.
	 * This makes the assumption that the device's resources won't
	 * change after this point (otherwise this code may break!)
	 */
	dev_dbg(&dev->dev, "enabling device\n");
	err = pci_enable_device(dev);
	if (err)
		goto config_release;

	if (dev->msix_cap) {
		struct physdev_pci_device ppdev = {
			.seg = pci_domain_nr(dev->bus),
			.bus = dev->bus->number,
			.devfn = dev->devfn
		};

		err = HYPERVISOR_physdev_op(PHYSDEVOP_prepare_msix, &ppdev);
		if (err && err != -ENOSYS)
			dev_err(&dev->dev, "MSI-X preparation failed (%d)\n",
				err);
	}

	/* We need the device active to save the state. */
	dev_dbg(&dev->dev, "save state of device\n");
	pci_save_state(dev);
	dev_data->pci_saved_state = pci_store_saved_state(dev);
	if (!dev_data->pci_saved_state)
		dev_err(&dev->dev, "Could not store PCI conf saved state!\n");
	else {
		dev_dbg(&dev->dev, "resetting (FLR, D3, etc) the device\n");
		err = pcistub_reset_device_state(dev);
		if (err)
			goto config_release;
		pci_restore_state(dev);
	}

#ifdef CONFIG_XEN_ACPI
	if (xen_initial_domain() && xen_pvh_domain()) {
		err = xen_acpi_get_gsi_info(dev, &gsi, &trigger, &polarity);
		if (err) {
			dev_err(&dev->dev, "Fail to get gsi info!\n");
			goto config_release;
		}
		err = xen_pvh_setup_gsi(gsi, trigger, polarity);
		if (err)
			goto config_release;
		psdev->gsi = gsi;
	}
#endif

	/* Now disable the device (this also ensures some private device
	 * data is setup before we export)
	 */
	dev_dbg(&dev->dev, "reset device\n");
	xen_pcibk_reset_device(dev);

	pci_set_dev_assigned(dev);
	return 0;

config_release:
	xen_pcibk_config_free_dev(dev);

out:
	pci_set_drvdata(dev, NULL);
	kfree(dev_data);
	return err;
}

/*
 * Because some initialization still happens on
 * devices during fs_initcall, we need to defer
 * full initialization of our devices until
 * device_initcall.
 */
static int __init pcistub_init_devices_late(void)
{
	struct pcistub_device *psdev;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	while (!list_empty(&seized_devices)) {
		psdev = container_of(seized_devices.next,
				     struct pcistub_device, dev_list);
		list_del(&psdev->dev_list);

		spin_unlock_irqrestore(&pcistub_devices_lock, flags);

		err = pcistub_init_device(psdev);
		if (err) {
			dev_err(&psdev->dev->dev,
				"error %d initializing device\n", err);
			kfree(psdev);
			psdev = NULL;
		}

		spin_lock_irqsave(&pcistub_devices_lock, flags);

		if (psdev)
			list_add_tail(&psdev->dev_list, &pcistub_devices);
	}

	initialize_devices = 1;

	spin_unlock_irqrestore(&pcistub_devices_lock, flags);

	return 0;
}

static void pcistub_device_id_add_list(struct pcistub_device_id *new,
				       int domain, int bus, unsigned int devfn)
{
	struct pcistub_device_id *pci_dev_id;
	unsigned long flags;
	int found = 0;

	spin_lock_irqsave(&device_ids_lock, flags);

	list_for_each_entry(pci_dev_id, &pcistub_device_ids, slot_list) {
		if (pci_dev_id->domain == domain && pci_dev_id->bus == bus &&
		    pci_dev_id->devfn == devfn) {
			found = 1;
			break;
		}
	}

	if (!found) {
		new->domain = domain;
		new->bus = bus;
		new->devfn = devfn;
		list_add_tail(&new->slot_list, &pcistub_device_ids);
	}

	spin_unlock_irqrestore(&device_ids_lock, flags);

	if (found)
		kfree(new);
}

static int pcistub_seize(struct pci_dev *dev,
			 struct pcistub_device_id *pci_dev_id)
{
	struct pcistub_device *psdev;
	unsigned long flags;
	int err = 0;

	psdev = pcistub_device_alloc(dev);
	if (!psdev) {
		kfree(pci_dev_id);
		return -ENOMEM;
	}

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	if (initialize_devices) {
		spin_unlock_irqrestore(&pcistub_devices_lock, flags);

		/* don't want irqs disabled when calling pcistub_init_device */
		err = pcistub_init_device(psdev);

		spin_lock_irqsave(&pcistub_devices_lock, flags);

		if (!err)
			list_add(&psdev->dev_list, &pcistub_devices);
	} else {
		dev_dbg(&dev->dev, "deferring initialization\n");
		list_add(&psdev->dev_list, &seized_devices);
	}

	spin_unlock_irqrestore(&pcistub_devices_lock, flags);

	if (err) {
		kfree(pci_dev_id);
		pcistub_device_put(psdev);
	} else if (pci_dev_id)
		pcistub_device_id_add_list(pci_dev_id, pci_domain_nr(dev->bus),
					   dev->bus->number, dev->devfn);

	return err;
}

/* Called when 'bind'. This means we must _NOT_ call pci_reset_function or
 * other functions that take the sysfs lock. */
static int pcistub_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int err = 0, match;
	struct pcistub_device_id *pci_dev_id = NULL;

	dev_dbg(&dev->dev, "probing...\n");

	match = pcistub_match(dev);

	if ((dev->driver_override &&
	     !strcmp(dev->driver_override, PCISTUB_DRIVER_NAME)) ||
	    match) {

		if (dev->hdr_type != PCI_HEADER_TYPE_NORMAL
		    && dev->hdr_type != PCI_HEADER_TYPE_BRIDGE) {
			dev_err(&dev->dev, "can't export pci devices that "
				"don't have a normal (0) or bridge (1) "
				"header type!\n");
			err = -ENODEV;
			goto out;
		}

		if (!match) {
			pci_dev_id = kmalloc(sizeof(*pci_dev_id), GFP_KERNEL);
			if (!pci_dev_id) {
				err = -ENOMEM;
				goto out;
			}
		}

		dev_info(&dev->dev, "seizing device\n");
		err = pcistub_seize(dev, pci_dev_id);
	} else
		/* Didn't find the device */
		err = -ENODEV;

out:
	return err;
}

/* Called when 'unbind'. This means we must _NOT_ call pci_reset_function or
 * other functions that take the sysfs lock. */
static void pcistub_remove(struct pci_dev *dev)
{
	struct pcistub_device *psdev, *found_psdev = NULL;
	unsigned long flags;

	dev_dbg(&dev->dev, "removing\n");

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	xen_pcibk_config_quirk_release(dev);

	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (psdev->dev == dev) {
			found_psdev = psdev;
			break;
		}
	}

	spin_unlock_irqrestore(&pcistub_devices_lock, flags);

	if (found_psdev) {
		dev_dbg(&dev->dev, "found device to remove %s\n",
			found_psdev->pdev ? "- in-use" : "");

		if (found_psdev->pdev) {
			int domid = xen_find_device_domain_owner(dev);

			dev_warn(&dev->dev, "****** removing device %s while still in-use by domain %d! ******\n",
			       pci_name(found_psdev->dev), domid);
			dev_warn(&dev->dev, "****** driver domain may still access this device's i/o resources!\n");
			dev_warn(&dev->dev, "****** shutdown driver domain before binding device\n");
			dev_warn(&dev->dev, "****** to other drivers or domains\n");

			/* N.B. This ends up calling pcistub_put_pci_dev which ends up
			 * doing the FLR. */
			xen_pcibk_release_pci_dev(found_psdev->pdev,
						found_psdev->dev,
						false /* caller holds the lock. */);
		}

		spin_lock_irqsave(&pcistub_devices_lock, flags);
		list_del(&found_psdev->dev_list);
		spin_unlock_irqrestore(&pcistub_devices_lock, flags);

		/* the final put for releasing from the list */
		pcistub_device_put(found_psdev);
	}
}

static const struct pci_device_id pcistub_ids[] = {
	{
	 .vendor = PCI_ANY_ID,
	 .device = PCI_ANY_ID,
	 .subvendor = PCI_ANY_ID,
	 .subdevice = PCI_ANY_ID,
	 },
	{0,},
};

#define PCI_NODENAME_MAX 40
static void kill_domain_by_device(struct pcistub_device *psdev)
{
	struct xenbus_transaction xbt;
	int err;
	char nodename[PCI_NODENAME_MAX];

	BUG_ON(!psdev);
	snprintf(nodename, PCI_NODENAME_MAX, "/local/domain/0/backend/pci/%d/0",
		psdev->pdev->xdev->otherend_id);

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		dev_err(&psdev->dev->dev,
			"error %d when start xenbus transaction\n", err);
		return;
	}
	/*PV AER handlers will set this flag*/
	xenbus_printf(xbt, nodename, "aerState" , "aerfail");
	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		dev_err(&psdev->dev->dev,
			"error %d when end xenbus transaction\n", err);
		return;
	}
}

/* For each aer recovery step error_detected, mmio_enabled, etc, front_end and
 * backend need to have cooperation. In xen_pcibk, those steps will do similar
 * jobs: send service request and waiting for front_end response.
*/
static pci_ers_result_t common_process(struct pcistub_device *psdev,
				       pci_channel_state_t state, int aer_cmd,
				       pci_ers_result_t result)
{
	pci_ers_result_t res = result;
	struct xen_pcie_aer_op *aer_op;
	struct xen_pcibk_device *pdev = psdev->pdev;
	struct xen_pci_sharedinfo *sh_info = pdev->sh_info;
	int ret;

	/*with PV AER drivers*/
	aer_op = &(sh_info->aer_op);
	aer_op->cmd = aer_cmd ;
	/*useful for error_detected callback*/
	aer_op->err = state;
	/*pcifront_end BDF*/
	ret = xen_pcibk_get_pcifront_dev(psdev->dev, psdev->pdev,
		&aer_op->domain, &aer_op->bus, &aer_op->devfn);
	if (!ret) {
		dev_err(&psdev->dev->dev, "failed to get pcifront device\n");
		return PCI_ERS_RESULT_NONE;
	}
	wmb();

	dev_dbg(&psdev->dev->dev, "aer_op %x dom %x bus %x devfn %x\n",
			aer_cmd, aer_op->domain, aer_op->bus, aer_op->devfn);
	/*local flag to mark there's aer request, xen_pcibk callback will use
	* this flag to judge whether we need to check pci-front give aer
	* service ack signal
	*/
	set_bit(_PCIB_op_pending, (unsigned long *)&pdev->flags);

	/*It is possible that a pcifront conf_read_write ops request invokes
	* the callback which cause the spurious execution of wake_up.
	* Yet it is harmless and better than a spinlock here
	*/
	set_bit(_XEN_PCIB_active,
		(unsigned long *)&sh_info->flags);
	wmb();
	notify_remote_via_irq(pdev->evtchn_irq);

	/* Enable IRQ to signal "request done". */
	xen_pcibk_lateeoi(pdev, 0);

	ret = wait_event_timeout(xen_pcibk_aer_wait_queue,
				 !(test_bit(_XEN_PCIB_active, (unsigned long *)
				 &sh_info->flags)), 300*HZ);

	/* Enable IRQ for pcifront request if not already active. */
	if (!test_bit(_PDEVF_op_active, &pdev->flags))
		xen_pcibk_lateeoi(pdev, 0);

	if (!ret) {
		if (test_bit(_XEN_PCIB_active,
			(unsigned long *)&sh_info->flags)) {
			dev_err(&psdev->dev->dev,
				"pcifront aer process not responding!\n");
			clear_bit(_XEN_PCIB_active,
			  (unsigned long *)&sh_info->flags);
			aer_op->err = PCI_ERS_RESULT_NONE;
			return res;
		}
	}
	clear_bit(_PCIB_op_pending, (unsigned long *)&pdev->flags);

	res = (__force pci_ers_result_t)aer_op->err;
	return res;
}

/*
* xen_pcibk_slot_reset: it will send the slot_reset request to  pcifront in case
* of the device driver could provide this service, and then wait for pcifront
* ack.
* @dev: pointer to PCI devices
* return value is used by aer_core do_recovery policy
*/
static pci_ers_result_t xen_pcibk_slot_reset(struct pci_dev *dev)
{
	struct pcistub_device *psdev;
	pci_ers_result_t result;

	result = PCI_ERS_RESULT_RECOVERED;
	dev_dbg(&dev->dev, "xen_pcibk_slot_reset(bus:%x,devfn:%x)\n",
		dev->bus->number, dev->devfn);

	down_write(&pcistub_sem);
	psdev = pcistub_device_find(pci_domain_nr(dev->bus),
				dev->bus->number,
				PCI_SLOT(dev->devfn),
				PCI_FUNC(dev->devfn));

	if (!psdev || !psdev->pdev) {
		dev_err(&dev->dev, "device is not found/assigned\n");
		goto end;
	}

	if (!psdev->pdev->sh_info) {
		dev_err(&dev->dev, "device is not connected or owned"
			" by HVM, kill it\n");
		kill_domain_by_device(psdev);
		goto end;
	}

	if (!test_bit(_XEN_PCIB_AERHANDLER,
		(unsigned long *)&psdev->pdev->sh_info->flags)) {
		dev_err(&dev->dev,
			"guest with no AER driver should have been killed\n");
		goto end;
	}
	result = common_process(psdev, pci_channel_io_normal, XEN_PCI_OP_aer_slotreset, result);

	if (result == PCI_ERS_RESULT_NONE ||
		result == PCI_ERS_RESULT_DISCONNECT) {
		dev_dbg(&dev->dev,
			"No AER slot_reset service or disconnected!\n");
		kill_domain_by_device(psdev);
	}
end:
	if (psdev)
		pcistub_device_put(psdev);
	up_write(&pcistub_sem);
	return result;

}


/*xen_pcibk_mmio_enabled: it will send the mmio_enabled request to  pcifront
* in case of the device driver could provide this service, and then wait
* for pcifront ack
* @dev: pointer to PCI devices
* return value is used by aer_core do_recovery policy
*/

static pci_ers_result_t xen_pcibk_mmio_enabled(struct pci_dev *dev)
{
	struct pcistub_device *psdev;
	pci_ers_result_t result;

	result = PCI_ERS_RESULT_RECOVERED;
	dev_dbg(&dev->dev, "xen_pcibk_mmio_enabled(bus:%x,devfn:%x)\n",
		dev->bus->number, dev->devfn);

	down_write(&pcistub_sem);
	psdev = pcistub_device_find(pci_domain_nr(dev->bus),
				dev->bus->number,
				PCI_SLOT(dev->devfn),
				PCI_FUNC(dev->devfn));

	if (!psdev || !psdev->pdev) {
		dev_err(&dev->dev, "device is not found/assigned\n");
		goto end;
	}

	if (!psdev->pdev->sh_info) {
		dev_err(&dev->dev, "device is not connected or owned"
			" by HVM, kill it\n");
		kill_domain_by_device(psdev);
		goto end;
	}

	if (!test_bit(_XEN_PCIB_AERHANDLER,
		(unsigned long *)&psdev->pdev->sh_info->flags)) {
		dev_err(&dev->dev,
			"guest with no AER driver should have been killed\n");
		goto end;
	}
	result = common_process(psdev, pci_channel_io_normal, XEN_PCI_OP_aer_mmio, result);

	if (result == PCI_ERS_RESULT_NONE ||
		result == PCI_ERS_RESULT_DISCONNECT) {
		dev_dbg(&dev->dev,
			"No AER mmio_enabled service or disconnected!\n");
		kill_domain_by_device(psdev);
	}
end:
	if (psdev)
		pcistub_device_put(psdev);
	up_write(&pcistub_sem);
	return result;
}

/*xen_pcibk_error_detected: it will send the error_detected request to  pcifront
* in case of the device driver could provide this service, and then wait
* for pcifront ack.
* @dev: pointer to PCI devices
* @error: the current PCI connection state
* return value is used by aer_core do_recovery policy
*/

static pci_ers_result_t xen_pcibk_error_detected(struct pci_dev *dev,
	pci_channel_state_t error)
{
	struct pcistub_device *psdev;
	pci_ers_result_t result;

	result = PCI_ERS_RESULT_CAN_RECOVER;
	dev_dbg(&dev->dev, "xen_pcibk_error_detected(bus:%x,devfn:%x)\n",
		dev->bus->number, dev->devfn);

	down_write(&pcistub_sem);
	psdev = pcistub_device_find(pci_domain_nr(dev->bus),
				dev->bus->number,
				PCI_SLOT(dev->devfn),
				PCI_FUNC(dev->devfn));

	if (!psdev || !psdev->pdev) {
		dev_err(&dev->dev, "device is not found/assigned\n");
		goto end;
	}

	if (!psdev->pdev->sh_info) {
		dev_err(&dev->dev, "device is not connected or owned"
			" by HVM, kill it\n");
		kill_domain_by_device(psdev);
		goto end;
	}

	/*Guest owns the device yet no aer handler regiested, kill guest*/
	if (!test_bit(_XEN_PCIB_AERHANDLER,
		(unsigned long *)&psdev->pdev->sh_info->flags)) {
		dev_dbg(&dev->dev, "guest may have no aer driver, kill it\n");
		kill_domain_by_device(psdev);
		goto end;
	}
	result = common_process(psdev, error, XEN_PCI_OP_aer_detected, result);

	if (result == PCI_ERS_RESULT_NONE ||
		result == PCI_ERS_RESULT_DISCONNECT) {
		dev_dbg(&dev->dev,
			"No AER error_detected service or disconnected!\n");
		kill_domain_by_device(psdev);
	}
end:
	if (psdev)
		pcistub_device_put(psdev);
	up_write(&pcistub_sem);
	return result;
}

/*xen_pcibk_error_resume: it will send the error_resume request to  pcifront
* in case of the device driver could provide this service, and then wait
* for pcifront ack.
* @dev: pointer to PCI devices
*/

static void xen_pcibk_error_resume(struct pci_dev *dev)
{
	struct pcistub_device *psdev;

	dev_dbg(&dev->dev, "xen_pcibk_error_resume(bus:%x,devfn:%x)\n",
		dev->bus->number, dev->devfn);

	down_write(&pcistub_sem);
	psdev = pcistub_device_find(pci_domain_nr(dev->bus),
				dev->bus->number,
				PCI_SLOT(dev->devfn),
				PCI_FUNC(dev->devfn));

	if (!psdev || !psdev->pdev) {
		dev_err(&dev->dev, "device is not found/assigned\n");
		goto end;
	}

	if (!psdev->pdev->sh_info) {
		dev_err(&dev->dev, "device is not connected or owned"
			" by HVM, kill it\n");
		kill_domain_by_device(psdev);
		goto end;
	}

	if (!test_bit(_XEN_PCIB_AERHANDLER,
		(unsigned long *)&psdev->pdev->sh_info->flags)) {
		dev_err(&dev->dev,
			"guest with no AER driver should have been killed\n");
		kill_domain_by_device(psdev);
		goto end;
	}
	common_process(psdev, pci_channel_io_normal, XEN_PCI_OP_aer_resume,
		       PCI_ERS_RESULT_RECOVERED);
end:
	if (psdev)
		pcistub_device_put(psdev);
	up_write(&pcistub_sem);
	return;
}

/*add xen_pcibk AER handling*/
static const struct pci_error_handlers xen_pcibk_error_handler = {
	.error_detected = xen_pcibk_error_detected,
	.mmio_enabled = xen_pcibk_mmio_enabled,
	.slot_reset = xen_pcibk_slot_reset,
	.resume = xen_pcibk_error_resume,
};

/*
 * Note: There is no MODULE_DEVICE_TABLE entry here because this isn't
 * for a normal device. I don't want it to be loaded automatically.
 */

static struct pci_driver xen_pcibk_pci_driver = {
	/* The name should be xen_pciback, but until the tools are updated
	 * we will keep it as pciback. */
	.name = PCISTUB_DRIVER_NAME,
	.id_table = pcistub_ids,
	.probe = pcistub_probe,
	.remove = pcistub_remove,
	.err_handler = &xen_pcibk_error_handler,
};

static inline int str_to_slot(const char *buf, int *domain, int *bus,
			      int *slot, int *func)
{
	int parsed = 0;

	switch (sscanf(buf, " %x:%x:%x.%x %n", domain, bus, slot, func,
		       &parsed)) {
	case 3:
		*func = -1;
		sscanf(buf, " %x:%x:%x.* %n", domain, bus, slot, &parsed);
		break;
	case 2:
		*slot = *func = -1;
		sscanf(buf, " %x:%x:*.* %n", domain, bus, &parsed);
		break;
	}
	if (parsed && !buf[parsed])
		return 0;

	/* try again without domain */
	*domain = 0;
	switch (sscanf(buf, " %x:%x.%x %n", bus, slot, func, &parsed)) {
	case 2:
		*func = -1;
		sscanf(buf, " %x:%x.* %n", bus, slot, &parsed);
		break;
	case 1:
		*slot = *func = -1;
		sscanf(buf, " %x:*.* %n", bus, &parsed);
		break;
	}
	if (parsed && !buf[parsed])
		return 0;

	return -EINVAL;
}

static inline int str_to_quirk(const char *buf, int *domain, int *bus, int
			       *slot, int *func, int *reg, int *size, int *mask)
{
	int parsed = 0;

	sscanf(buf, " %x:%x:%x.%x-%x:%x:%x %n", domain, bus, slot, func,
	       reg, size, mask, &parsed);
	if (parsed && !buf[parsed])
		return 0;

	/* try again without domain */
	*domain = 0;
	sscanf(buf, " %x:%x.%x-%x:%x:%x %n", bus, slot, func, reg, size,
	       mask, &parsed);
	if (parsed && !buf[parsed])
		return 0;

	return -EINVAL;
}

static int pcistub_device_id_add(int domain, int bus, int slot, int func)
{
	struct pcistub_device_id *pci_dev_id;
	int rc = 0, devfn = PCI_DEVFN(slot, func);

	if (slot < 0) {
		for (slot = 0; !rc && slot < 32; ++slot)
			rc = pcistub_device_id_add(domain, bus, slot, func);
		return rc;
	}

	if (func < 0) {
		for (func = 0; !rc && func < 8; ++func)
			rc = pcistub_device_id_add(domain, bus, slot, func);
		return rc;
	}

	if ((
#if !defined(MODULE) /* pci_domains_supported is not being exported */ \
    || !defined(CONFIG_PCI_DOMAINS)
	     !pci_domains_supported ? domain :
#endif
	     domain < 0 || domain > 0xffff)
	    || bus < 0 || bus > 0xff
	    || PCI_SLOT(devfn) != slot
	    || PCI_FUNC(devfn) != func)
		return -EINVAL;

	pci_dev_id = kmalloc(sizeof(*pci_dev_id), GFP_KERNEL);
	if (!pci_dev_id)
		return -ENOMEM;

	pr_debug("wants to seize %04x:%02x:%02x.%d\n",
		 domain, bus, slot, func);

	pcistub_device_id_add_list(pci_dev_id, domain, bus, devfn);

	return 0;
}

static int pcistub_device_id_remove(int domain, int bus, int slot, int func)
{
	struct pcistub_device_id *pci_dev_id, *t;
	int err = -ENOENT;
	unsigned long flags;

	spin_lock_irqsave(&device_ids_lock, flags);
	list_for_each_entry_safe(pci_dev_id, t, &pcistub_device_ids,
				 slot_list) {
		if (pci_dev_id->domain == domain && pci_dev_id->bus == bus
		    && (slot < 0 || PCI_SLOT(pci_dev_id->devfn) == slot)
		    && (func < 0 || PCI_FUNC(pci_dev_id->devfn) == func)) {
			/* Don't break; here because it's possible the same
			 * slot could be in the list more than once
			 */
			list_del(&pci_dev_id->slot_list);
			kfree(pci_dev_id);

			err = 0;

			pr_debug("removed %04x:%02x:%02x.%d from seize list\n",
				 domain, bus, slot, func);
		}
	}
	spin_unlock_irqrestore(&device_ids_lock, flags);

	return err;
}

static int pcistub_reg_add(int domain, int bus, int slot, int func,
			   unsigned int reg, unsigned int size,
			   unsigned int mask)
{
	int err = 0;
	struct pcistub_device *psdev;
	struct pci_dev *dev;
	struct config_field *field;

	if (reg > 0xfff || (size < 4 && (mask >> (size * 8))))
		return -EINVAL;

	psdev = pcistub_device_find(domain, bus, slot, func);
	if (!psdev) {
		err = -ENODEV;
		goto out;
	}
	dev = psdev->dev;

	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field) {
		err = -ENOMEM;
		goto out;
	}

	field->offset = reg;
	field->size = size;
	field->mask = mask;
	field->init = NULL;
	field->reset = NULL;
	field->release = NULL;
	field->clean = xen_pcibk_config_field_free;

	err = xen_pcibk_config_quirks_add_field(dev, field);
	if (err)
		kfree(field);
out:
	if (psdev)
		pcistub_device_put(psdev);
	return err;
}

static ssize_t new_slot_store(struct device_driver *drv, const char *buf,
			      size_t count)
{
	int domain, bus, slot, func;
	int err;

	err = str_to_slot(buf, &domain, &bus, &slot, &func);
	if (err)
		goto out;

	err = pcistub_device_id_add(domain, bus, slot, func);

out:
	if (!err)
		err = count;
	return err;
}
static DRIVER_ATTR_WO(new_slot);

static ssize_t remove_slot_store(struct device_driver *drv, const char *buf,
				 size_t count)
{
	int domain, bus, slot, func;
	int err;

	err = str_to_slot(buf, &domain, &bus, &slot, &func);
	if (err)
		goto out;

	err = pcistub_device_id_remove(domain, bus, slot, func);

out:
	if (!err)
		err = count;
	return err;
}
static DRIVER_ATTR_WO(remove_slot);

static ssize_t slots_show(struct device_driver *drv, char *buf)
{
	struct pcistub_device_id *pci_dev_id;
	size_t count = 0;
	unsigned long flags;

	spin_lock_irqsave(&device_ids_lock, flags);
	list_for_each_entry(pci_dev_id, &pcistub_device_ids, slot_list) {
		if (count >= PAGE_SIZE)
			break;

		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "%04x:%02x:%02x.%d\n",
				   pci_dev_id->domain, pci_dev_id->bus,
				   PCI_SLOT(pci_dev_id->devfn),
				   PCI_FUNC(pci_dev_id->devfn));
	}
	spin_unlock_irqrestore(&device_ids_lock, flags);

	return count;
}
static DRIVER_ATTR_RO(slots);

static ssize_t irq_handlers_show(struct device_driver *drv, char *buf)
{
	struct pcistub_device *psdev;
	struct xen_pcibk_dev_data *dev_data;
	size_t count = 0;
	unsigned long flags;

	spin_lock_irqsave(&pcistub_devices_lock, flags);
	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (count >= PAGE_SIZE)
			break;
		if (!psdev->dev)
			continue;
		dev_data = pci_get_drvdata(psdev->dev);
		if (!dev_data)
			continue;
		count +=
		    scnprintf(buf + count, PAGE_SIZE - count,
			      "%s:%s:%sing:%ld\n",
			      pci_name(psdev->dev),
			      dev_data->isr_on ? "on" : "off",
			      dev_data->ack_intr ? "ack" : "not ack",
			      dev_data->handled);
	}
	spin_unlock_irqrestore(&pcistub_devices_lock, flags);
	return count;
}
static DRIVER_ATTR_RO(irq_handlers);

static ssize_t irq_handler_state_store(struct device_driver *drv,
				       const char *buf, size_t count)
{
	struct pcistub_device *psdev;
	struct xen_pcibk_dev_data *dev_data;
	int domain, bus, slot, func;
	int err;

	err = str_to_slot(buf, &domain, &bus, &slot, &func);
	if (err)
		return err;

	psdev = pcistub_device_find(domain, bus, slot, func);
	if (!psdev) {
		err = -ENOENT;
		goto out;
	}

	dev_data = pci_get_drvdata(psdev->dev);
	if (!dev_data) {
		err = -ENOENT;
		goto out;
	}

	dev_dbg(&psdev->dev->dev, "%s fake irq handler: %d->%d\n",
		dev_data->irq_name, dev_data->isr_on,
		!dev_data->isr_on);

	dev_data->isr_on = !(dev_data->isr_on);
	if (dev_data->isr_on)
		dev_data->ack_intr = 1;
out:
	if (psdev)
		pcistub_device_put(psdev);
	if (!err)
		err = count;
	return err;
}
static DRIVER_ATTR_WO(irq_handler_state);

static ssize_t quirks_store(struct device_driver *drv, const char *buf,
			    size_t count)
{
	int domain, bus, slot, func, reg, size, mask;
	int err;

	err = str_to_quirk(buf, &domain, &bus, &slot, &func, &reg, &size,
			   &mask);
	if (err)
		goto out;

	err = pcistub_reg_add(domain, bus, slot, func, reg, size, mask);

out:
	if (!err)
		err = count;
	return err;
}

static ssize_t quirks_show(struct device_driver *drv, char *buf)
{
	int count = 0;
	unsigned long flags;
	struct xen_pcibk_config_quirk *quirk;
	struct xen_pcibk_dev_data *dev_data;
	const struct config_field *field;
	const struct config_field_entry *cfg_entry;

	spin_lock_irqsave(&device_ids_lock, flags);
	list_for_each_entry(quirk, &xen_pcibk_quirks, quirks_list) {
		if (count >= PAGE_SIZE)
			goto out;

		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "%02x:%02x.%01x\n\t%04x:%04x:%04x:%04x\n",
				   quirk->pdev->bus->number,
				   PCI_SLOT(quirk->pdev->devfn),
				   PCI_FUNC(quirk->pdev->devfn),
				   quirk->devid.vendor, quirk->devid.device,
				   quirk->devid.subvendor,
				   quirk->devid.subdevice);

		dev_data = pci_get_drvdata(quirk->pdev);

		list_for_each_entry(cfg_entry, &dev_data->config_fields, list) {
			field = cfg_entry->field;
			if (count >= PAGE_SIZE)
				goto out;

			count += scnprintf(buf + count, PAGE_SIZE - count,
					   "\t\t%08x:%01x:%08x\n",
					   cfg_entry->base_offset +
					   field->offset, field->size,
					   field->mask);
		}
	}

out:
	spin_unlock_irqrestore(&device_ids_lock, flags);

	return count;
}
static DRIVER_ATTR_RW(quirks);

static ssize_t permissive_store(struct device_driver *drv, const char *buf,
				size_t count)
{
	int domain, bus, slot, func;
	int err;
	struct pcistub_device *psdev;
	struct xen_pcibk_dev_data *dev_data;

	err = str_to_slot(buf, &domain, &bus, &slot, &func);
	if (err)
		goto out;

	psdev = pcistub_device_find(domain, bus, slot, func);
	if (!psdev) {
		err = -ENODEV;
		goto out;
	}

	dev_data = pci_get_drvdata(psdev->dev);
	/* the driver data for a device should never be null at this point */
	if (!dev_data) {
		err = -ENXIO;
		goto release;
	}
	if (!dev_data->permissive) {
		dev_data->permissive = 1;
		/* Let user know that what they're doing could be unsafe */
		dev_warn(&psdev->dev->dev, "enabling permissive mode "
			 "configuration space accesses!\n");
		dev_warn(&psdev->dev->dev,
			 "permissive mode is potentially unsafe!\n");
	}
release:
	pcistub_device_put(psdev);
out:
	if (!err)
		err = count;
	return err;
}

static ssize_t permissive_show(struct device_driver *drv, char *buf)
{
	struct pcistub_device *psdev;
	struct xen_pcibk_dev_data *dev_data;
	size_t count = 0;
	unsigned long flags;
	spin_lock_irqsave(&pcistub_devices_lock, flags);
	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (count >= PAGE_SIZE)
			break;
		if (!psdev->dev)
			continue;
		dev_data = pci_get_drvdata(psdev->dev);
		if (!dev_data || !dev_data->permissive)
			continue;
		count +=
		    scnprintf(buf + count, PAGE_SIZE - count, "%s\n",
			      pci_name(psdev->dev));
	}
	spin_unlock_irqrestore(&pcistub_devices_lock, flags);
	return count;
}
static DRIVER_ATTR_RW(permissive);

static ssize_t allow_interrupt_control_store(struct device_driver *drv,
					     const char *buf, size_t count)
{
	int domain, bus, slot, func;
	int err;
	struct pcistub_device *psdev;
	struct xen_pcibk_dev_data *dev_data;

	err = str_to_slot(buf, &domain, &bus, &slot, &func);
	if (err)
		goto out;

	psdev = pcistub_device_find(domain, bus, slot, func);
	if (!psdev) {
		err = -ENODEV;
		goto out;
	}

	dev_data = pci_get_drvdata(psdev->dev);
	/* the driver data for a device should never be null at this point */
	if (!dev_data) {
		err = -ENXIO;
		goto release;
	}
	dev_data->allow_interrupt_control = 1;
release:
	pcistub_device_put(psdev);
out:
	if (!err)
		err = count;
	return err;
}

static ssize_t allow_interrupt_control_show(struct device_driver *drv,
					    char *buf)
{
	struct pcistub_device *psdev;
	struct xen_pcibk_dev_data *dev_data;
	size_t count = 0;
	unsigned long flags;

	spin_lock_irqsave(&pcistub_devices_lock, flags);
	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (count >= PAGE_SIZE)
			break;
		if (!psdev->dev)
			continue;
		dev_data = pci_get_drvdata(psdev->dev);
		if (!dev_data || !dev_data->allow_interrupt_control)
			continue;
		count +=
		    scnprintf(buf + count, PAGE_SIZE - count, "%s\n",
			      pci_name(psdev->dev));
	}
	spin_unlock_irqrestore(&pcistub_devices_lock, flags);
	return count;
}
static DRIVER_ATTR_RW(allow_interrupt_control);

static void pcistub_exit(void)
{
	driver_remove_file(&xen_pcibk_pci_driver.driver, &driver_attr_new_slot);
	driver_remove_file(&xen_pcibk_pci_driver.driver,
			   &driver_attr_remove_slot);
	driver_remove_file(&xen_pcibk_pci_driver.driver, &driver_attr_slots);
	driver_remove_file(&xen_pcibk_pci_driver.driver, &driver_attr_quirks);
	driver_remove_file(&xen_pcibk_pci_driver.driver,
			   &driver_attr_permissive);
	driver_remove_file(&xen_pcibk_pci_driver.driver,
			   &driver_attr_allow_interrupt_control);
	driver_remove_file(&xen_pcibk_pci_driver.driver,
			   &driver_attr_irq_handlers);
	driver_remove_file(&xen_pcibk_pci_driver.driver,
			   &driver_attr_irq_handler_state);
	pci_unregister_driver(&xen_pcibk_pci_driver);
}

static int __init pcistub_init(void)
{
	int pos = 0;
	int err = 0;
	int domain, bus, slot, func;
	int parsed;

	if (pci_devs_to_hide && *pci_devs_to_hide) {
		do {
			parsed = 0;

			err = sscanf(pci_devs_to_hide + pos,
				     " (%x:%x:%x.%x) %n",
				     &domain, &bus, &slot, &func, &parsed);
			switch (err) {
			case 3:
				func = -1;
				sscanf(pci_devs_to_hide + pos,
				       " (%x:%x:%x.*) %n",
				       &domain, &bus, &slot, &parsed);
				break;
			case 2:
				slot = func = -1;
				sscanf(pci_devs_to_hide + pos,
				       " (%x:%x:*.*) %n",
				       &domain, &bus, &parsed);
				break;
			}

			if (!parsed) {
				domain = 0;
				err = sscanf(pci_devs_to_hide + pos,
					     " (%x:%x.%x) %n",
					     &bus, &slot, &func, &parsed);
				switch (err) {
				case 2:
					func = -1;
					sscanf(pci_devs_to_hide + pos,
					       " (%x:%x.*) %n",
					       &bus, &slot, &parsed);
					break;
				case 1:
					slot = func = -1;
					sscanf(pci_devs_to_hide + pos,
					       " (%x:*.*) %n",
					       &bus, &parsed);
					break;
				}
			}

			if (parsed <= 0)
				goto parse_error;

			err = pcistub_device_id_add(domain, bus, slot, func);
			if (err)
				goto out;

			pos += parsed;
		} while (pci_devs_to_hide[pos]);
	}

	/* If we're the first PCI Device Driver to register, we're the
	 * first one to get offered PCI devices as they become
	 * available (and thus we can be the first to grab them)
	 */
	err = pci_register_driver(&xen_pcibk_pci_driver);
	if (err < 0)
		goto out;

	err = driver_create_file(&xen_pcibk_pci_driver.driver,
				 &driver_attr_new_slot);
	if (!err)
		err = driver_create_file(&xen_pcibk_pci_driver.driver,
					 &driver_attr_remove_slot);
	if (!err)
		err = driver_create_file(&xen_pcibk_pci_driver.driver,
					 &driver_attr_slots);
	if (!err)
		err = driver_create_file(&xen_pcibk_pci_driver.driver,
					 &driver_attr_quirks);
	if (!err)
		err = driver_create_file(&xen_pcibk_pci_driver.driver,
					 &driver_attr_permissive);
	if (!err)
		err = driver_create_file(&xen_pcibk_pci_driver.driver,
					 &driver_attr_allow_interrupt_control);

	if (!err)
		err = driver_create_file(&xen_pcibk_pci_driver.driver,
					 &driver_attr_irq_handlers);
	if (!err)
		err = driver_create_file(&xen_pcibk_pci_driver.driver,
					&driver_attr_irq_handler_state);
	if (err)
		pcistub_exit();

out:
	return err;

parse_error:
	pr_err("Error parsing pci_devs_to_hide at \"%s\"\n",
	       pci_devs_to_hide + pos);
	return -EINVAL;
}

#ifndef MODULE
/*
 * fs_initcall happens before device_initcall
 * so xen_pcibk *should* get called first (b/c we
 * want to suck up any device before other drivers
 * get a chance by being the first pci device
 * driver to register)
 */
fs_initcall(pcistub_init);
#endif

#ifdef CONFIG_PCI_IOV
static struct pcistub_device *find_vfs(const struct pci_dev *pdev)
{
	struct pcistub_device *psdev = NULL;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&pcistub_devices_lock, flags);
	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (!psdev->pdev && psdev->dev != pdev
		    && pci_physfn(psdev->dev) == pdev) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&pcistub_devices_lock, flags);
	if (found)
		return psdev;
	return NULL;
}

static int pci_stub_notifier(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	struct device *dev = data;
	const struct pci_dev *pdev = to_pci_dev(dev);

	if (action != BUS_NOTIFY_UNBIND_DRIVER)
		return NOTIFY_DONE;

	if (!pdev->is_physfn)
		return NOTIFY_DONE;

	for (;;) {
		struct pcistub_device *psdev = find_vfs(pdev);
		if (!psdev)
			break;
		device_release_driver(&psdev->dev->dev);
	}
	return NOTIFY_DONE;
}

static struct notifier_block pci_stub_nb = {
	.notifier_call = pci_stub_notifier,
};
#endif

static int __init xen_pcibk_init(void)
{
	int err;

	if (!xen_initial_domain())
		return -ENODEV;

	err = xen_pcibk_config_init();
	if (err)
		return err;

#ifdef MODULE
	err = pcistub_init();
	if (err < 0)
		return err;
#endif

	pcistub_init_devices_late();
	err = xen_pcibk_xenbus_register();
	if (err)
		pcistub_exit();
#ifdef CONFIG_PCI_IOV
	else
		bus_register_notifier(&pci_bus_type, &pci_stub_nb);
#endif

	return err;
}

static void __exit xen_pcibk_cleanup(void)
{
#ifdef CONFIG_PCI_IOV
	bus_unregister_notifier(&pci_bus_type, &pci_stub_nb);
#endif
	xen_pcibk_xenbus_unregister();
	pcistub_exit();
}

module_init(xen_pcibk_init);
module_exit(xen_pcibk_cleanup);

MODULE_DESCRIPTION("Xen PCI-device stub driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:pci");
