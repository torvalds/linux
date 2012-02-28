/*
 * PCI Stub Driver - Grabs devices in backend to be exported later
 *
 * Ryan Wilson <hap9@epoch.ncsc.mil>
 * Chris Bookholt <hap10@epoch.ncsc.mil>
 */
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
#include <asm/xen/pci.h>
#include <asm/xen/hypervisor.h>
#include "pciback.h"
#include "conf_space.h"
#include "conf_space_quirks.h"

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

	psdev = kzalloc(sizeof(*psdev), GFP_ATOMIC);
	if (!psdev)
		return NULL;

	psdev->dev = pci_dev_get(dev);
	if (!psdev->dev) {
		kfree(psdev);
		return NULL;
	}

	kref_init(&psdev->kref);
	spin_lock_init(&psdev->lock);

	return psdev;
}

/* Don't call this directly as it's called by pcistub_device_put */
static void pcistub_device_release(struct kref *kref)
{
	struct pcistub_device *psdev;

	psdev = container_of(kref, struct pcistub_device, kref);

	dev_dbg(&psdev->dev->dev, "pcistub_device_release\n");

	xen_unregister_device_domain_owner(psdev->dev);

	/* Clean-up the device */
	xen_pcibk_reset_device(psdev->dev);
	xen_pcibk_config_free_dyn_fields(psdev->dev);
	xen_pcibk_config_free_dev(psdev->dev);
	kfree(pci_get_drvdata(psdev->dev));
	pci_set_drvdata(psdev->dev, NULL);

	psdev->dev->dev_flags &= ~PCI_DEV_FLAGS_ASSIGNED;
	pci_dev_put(psdev->dev);

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

static struct pcistub_device *pcistub_device_find(int domain, int bus,
						  int slot, int func)
{
	struct pcistub_device *psdev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (psdev->dev != NULL
		    && domain == pci_domain_nr(psdev->dev->bus)
		    && bus == psdev->dev->bus->number
		    && PCI_DEVFN(slot, func) == psdev->dev->devfn) {
			pcistub_device_get(psdev);
			goto out;
		}
	}

	/* didn't find it */
	psdev = NULL;

out:
	spin_unlock_irqrestore(&pcistub_devices_lock, flags);
	return psdev;
}

static struct pci_dev *pcistub_device_get_pci_dev(struct xen_pcibk_device *pdev,
						  struct pcistub_device *psdev)
{
	struct pci_dev *pci_dev = NULL;
	unsigned long flags;

	pcistub_device_get(psdev);

	spin_lock_irqsave(&psdev->lock, flags);
	if (!psdev->pdev) {
		psdev->pdev = pdev;
		pci_dev = psdev->dev;
	}
	spin_unlock_irqrestore(&psdev->lock, flags);

	if (!pci_dev)
		pcistub_device_put(psdev);

	return pci_dev;
}

struct pci_dev *pcistub_get_pci_dev_by_slot(struct xen_pcibk_device *pdev,
					    int domain, int bus,
					    int slot, int func)
{
	struct pcistub_device *psdev;
	struct pci_dev *found_dev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	list_for_each_entry(psdev, &pcistub_devices, dev_list) {
		if (psdev->dev != NULL
		    && domain == pci_domain_nr(psdev->dev->bus)
		    && bus == psdev->dev->bus->number
		    && PCI_DEVFN(slot, func) == psdev->dev->devfn) {
			found_dev = pcistub_device_get_pci_dev(pdev, psdev);
			break;
		}
	}

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

void pcistub_put_pci_dev(struct pci_dev *dev)
{
	struct pcistub_device *psdev, *found_psdev = NULL;
	unsigned long flags;

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
	xen_pcibk_reset_device(found_psdev->dev);
	xen_pcibk_config_free_dyn_fields(found_psdev->dev);
	xen_pcibk_config_reset_dev(found_psdev->dev);

	xen_unregister_device_domain_owner(found_psdev->dev);

	spin_lock_irqsave(&found_psdev->lock, flags);
	found_psdev->pdev = NULL;
	spin_unlock_irqrestore(&found_psdev->lock, flags);

	pcistub_device_put(found_psdev);
	up_write(&pcistub_sem);
}

static int __devinit pcistub_match_one(struct pci_dev *dev,
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

static int __devinit pcistub_match(struct pci_dev *dev)
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

static int __devinit pcistub_init_device(struct pci_dev *dev)
{
	struct xen_pcibk_dev_data *dev_data;
	int err = 0;

	dev_dbg(&dev->dev, "initializing...\n");

	/* The PCI backend is not intended to be a module (or to work with
	 * removable PCI devices (yet). If it were, xen_pcibk_config_free()
	 * would need to be called somewhere to free the memory allocated
	 * here and then to call kfree(pci_get_drvdata(psdev->dev)).
	 */
	dev_data = kzalloc(sizeof(*dev_data) +  strlen(DRV_NAME "[]")
				+ strlen(pci_name(dev)) + 1, GFP_ATOMIC);
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

	/* Now disable the device (this also ensures some private device
	 * data is setup before we export)
	 */
	dev_dbg(&dev->dev, "reset device\n");
	xen_pcibk_reset_device(dev);

	dev->dev_flags |= PCI_DEV_FLAGS_ASSIGNED;
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

	pr_debug(DRV_NAME ": pcistub_init_devices_late\n");

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	while (!list_empty(&seized_devices)) {
		psdev = container_of(seized_devices.next,
				     struct pcistub_device, dev_list);
		list_del(&psdev->dev_list);

		spin_unlock_irqrestore(&pcistub_devices_lock, flags);

		err = pcistub_init_device(psdev->dev);
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

static int __devinit pcistub_seize(struct pci_dev *dev)
{
	struct pcistub_device *psdev;
	unsigned long flags;
	int err = 0;

	psdev = pcistub_device_alloc(dev);
	if (!psdev)
		return -ENOMEM;

	spin_lock_irqsave(&pcistub_devices_lock, flags);

	if (initialize_devices) {
		spin_unlock_irqrestore(&pcistub_devices_lock, flags);

		/* don't want irqs disabled when calling pcistub_init_device */
		err = pcistub_init_device(psdev->dev);

		spin_lock_irqsave(&pcistub_devices_lock, flags);

		if (!err)
			list_add(&psdev->dev_list, &pcistub_devices);
	} else {
		dev_dbg(&dev->dev, "deferring initialization\n");
		list_add(&psdev->dev_list, &seized_devices);
	}

	spin_unlock_irqrestore(&pcistub_devices_lock, flags);

	if (err)
		pcistub_device_put(psdev);

	return err;
}

static int __devinit pcistub_probe(struct pci_dev *dev,
				   const struct pci_device_id *id)
{
	int err = 0;

	dev_dbg(&dev->dev, "probing...\n");

	if (pcistub_match(dev)) {

		if (dev->hdr_type != PCI_HEADER_TYPE_NORMAL
		    && dev->hdr_type != PCI_HEADER_TYPE_BRIDGE) {
			dev_err(&dev->dev, "can't export pci devices that "
				"don't have a normal (0) or bridge (1) "
				"header type!\n");
			err = -ENODEV;
			goto out;
		}

		dev_info(&dev->dev, "seizing device\n");
		err = pcistub_seize(dev);
	} else
		/* Didn't find the device */
		err = -ENODEV;

out:
	return err;
}

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
		dev_dbg(&dev->dev, "found device to remove - in use? %p\n",
			found_psdev->pdev);

		if (found_psdev->pdev) {
			printk(KERN_WARNING DRV_NAME ": ****** removing device "
			       "%s while still in-use! ******\n",
			       pci_name(found_psdev->dev));
			printk(KERN_WARNING DRV_NAME ": ****** driver domain may"
			       " still access this device's i/o resources!\n");
			printk(KERN_WARNING DRV_NAME ": ****** shutdown driver "
			       "domain before binding device\n");
			printk(KERN_WARNING DRV_NAME ": ****** to other drivers "
			       "or domains\n");

			xen_pcibk_release_pci_dev(found_psdev->pdev,
						found_psdev->dev);
		}

		spin_lock_irqsave(&pcistub_devices_lock, flags);
		list_del(&found_psdev->dev_list);
		spin_unlock_irqrestore(&pcistub_devices_lock, flags);

		/* the final put for releasing from the list */
		pcistub_device_put(found_psdev);
	}
}

static DEFINE_PCI_DEVICE_TABLE(pcistub_ids) = {
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
	int ret;

	/*with PV AER drivers*/
	aer_op = &(psdev->pdev->sh_info->aer_op);
	aer_op->cmd = aer_cmd ;
	/*useful for error_detected callback*/
	aer_op->err = state;
	/*pcifront_end BDF*/
	ret = xen_pcibk_get_pcifront_dev(psdev->dev, psdev->pdev,
		&aer_op->domain, &aer_op->bus, &aer_op->devfn);
	if (!ret) {
		dev_err(&psdev->dev->dev,
			DRV_NAME ": failed to get pcifront device\n");
		return PCI_ERS_RESULT_NONE;
	}
	wmb();

	dev_dbg(&psdev->dev->dev,
			DRV_NAME ": aer_op %x dom %x bus %x devfn %x\n",
			aer_cmd, aer_op->domain, aer_op->bus, aer_op->devfn);
	/*local flag to mark there's aer request, xen_pcibk callback will use
	* this flag to judge whether we need to check pci-front give aer
	* service ack signal
	*/
	set_bit(_PCIB_op_pending, (unsigned long *)&psdev->pdev->flags);

	/*It is possible that a pcifront conf_read_write ops request invokes
	* the callback which cause the spurious execution of wake_up.
	* Yet it is harmless and better than a spinlock here
	*/
	set_bit(_XEN_PCIB_active,
		(unsigned long *)&psdev->pdev->sh_info->flags);
	wmb();
	notify_remote_via_irq(psdev->pdev->evtchn_irq);

	ret = wait_event_timeout(xen_pcibk_aer_wait_queue,
				 !(test_bit(_XEN_PCIB_active, (unsigned long *)
				 &psdev->pdev->sh_info->flags)), 300*HZ);

	if (!ret) {
		if (test_bit(_XEN_PCIB_active,
			(unsigned long *)&psdev->pdev->sh_info->flags)) {
			dev_err(&psdev->dev->dev,
				"pcifront aer process not responding!\n");
			clear_bit(_XEN_PCIB_active,
			  (unsigned long *)&psdev->pdev->sh_info->flags);
			aer_op->err = PCI_ERS_RESULT_NONE;
			return res;
		}
	}
	clear_bit(_PCIB_op_pending, (unsigned long *)&psdev->pdev->flags);

	if (test_bit(_XEN_PCIF_active,
		(unsigned long *)&psdev->pdev->sh_info->flags)) {
		dev_dbg(&psdev->dev->dev,
			"schedule pci_conf service in " DRV_NAME "\n");
		xen_pcibk_test_and_schedule_op(psdev->pdev);
	}

	res = (pci_ers_result_t)aer_op->err;
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
		dev_err(&dev->dev,
			DRV_NAME " device is not found/assigned\n");
		goto end;
	}

	if (!psdev->pdev->sh_info) {
		dev_err(&dev->dev, DRV_NAME " device is not connected or owned"
			" by HVM, kill it\n");
		kill_domain_by_device(psdev);
		goto release;
	}

	if (!test_bit(_XEN_PCIB_AERHANDLER,
		(unsigned long *)&psdev->pdev->sh_info->flags)) {
		dev_err(&dev->dev,
			"guest with no AER driver should have been killed\n");
		goto release;
	}
	result = common_process(psdev, 1, XEN_PCI_OP_aer_slotreset, result);

	if (result == PCI_ERS_RESULT_NONE ||
		result == PCI_ERS_RESULT_DISCONNECT) {
		dev_dbg(&dev->dev,
			"No AER slot_reset service or disconnected!\n");
		kill_domain_by_device(psdev);
	}
release:
	pcistub_device_put(psdev);
end:
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
		dev_err(&dev->dev,
			DRV_NAME " device is not found/assigned\n");
		goto end;
	}

	if (!psdev->pdev->sh_info) {
		dev_err(&dev->dev, DRV_NAME " device is not connected or owned"
			" by HVM, kill it\n");
		kill_domain_by_device(psdev);
		goto release;
	}

	if (!test_bit(_XEN_PCIB_AERHANDLER,
		(unsigned long *)&psdev->pdev->sh_info->flags)) {
		dev_err(&dev->dev,
			"guest with no AER driver should have been killed\n");
		goto release;
	}
	result = common_process(psdev, 1, XEN_PCI_OP_aer_mmio, result);

	if (result == PCI_ERS_RESULT_NONE ||
		result == PCI_ERS_RESULT_DISCONNECT) {
		dev_dbg(&dev->dev,
			"No AER mmio_enabled service or disconnected!\n");
		kill_domain_by_device(psdev);
	}
release:
	pcistub_device_put(psdev);
end:
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
		dev_err(&dev->dev,
			DRV_NAME " device is not found/assigned\n");
		goto end;
	}

	if (!psdev->pdev->sh_info) {
		dev_err(&dev->dev, DRV_NAME " device is not connected or owned"
			" by HVM, kill it\n");
		kill_domain_by_device(psdev);
		goto release;
	}

	/*Guest owns the device yet no aer handler regiested, kill guest*/
	if (!test_bit(_XEN_PCIB_AERHANDLER,
		(unsigned long *)&psdev->pdev->sh_info->flags)) {
		dev_dbg(&dev->dev, "guest may have no aer driver, kill it\n");
		kill_domain_by_device(psdev);
		goto release;
	}
	result = common_process(psdev, error, XEN_PCI_OP_aer_detected, result);

	if (result == PCI_ERS_RESULT_NONE ||
		result == PCI_ERS_RESULT_DISCONNECT) {
		dev_dbg(&dev->dev,
			"No AER error_detected service or disconnected!\n");
		kill_domain_by_device(psdev);
	}
release:
	pcistub_device_put(psdev);
end:
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
		dev_err(&dev->dev,
			DRV_NAME " device is not found/assigned\n");
		goto end;
	}

	if (!psdev->pdev->sh_info) {
		dev_err(&dev->dev, DRV_NAME " device is not connected or owned"
			" by HVM, kill it\n");
		kill_domain_by_device(psdev);
		goto release;
	}

	if (!test_bit(_XEN_PCIB_AERHANDLER,
		(unsigned long *)&psdev->pdev->sh_info->flags)) {
		dev_err(&dev->dev,
			"guest with no AER driver should have been killed\n");
		kill_domain_by_device(psdev);
		goto release;
	}
	common_process(psdev, 1, XEN_PCI_OP_aer_resume,
		       PCI_ERS_RESULT_RECOVERED);
release:
	pcistub_device_put(psdev);
end:
	up_write(&pcistub_sem);
	return;
}

/*add xen_pcibk AER handling*/
static struct pci_error_handlers xen_pcibk_error_handler = {
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
	.name = "pciback",
	.id_table = pcistub_ids,
	.probe = pcistub_probe,
	.remove = pcistub_remove,
	.err_handler = &xen_pcibk_error_handler,
};

static inline int str_to_slot(const char *buf, int *domain, int *bus,
			      int *slot, int *func)
{
	int err;

	err = sscanf(buf, " %x:%x:%x.%x", domain, bus, slot, func);
	if (err == 4)
		return 0;
	else if (err < 0)
		return -EINVAL;

	/* try again without domain */
	*domain = 0;
	err = sscanf(buf, " %x:%x.%x", bus, slot, func);
	if (err == 3)
		return 0;

	return -EINVAL;
}

static inline int str_to_quirk(const char *buf, int *domain, int *bus, int
			       *slot, int *func, int *reg, int *size, int *mask)
{
	int err;

	err =
	    sscanf(buf, " %04x:%02x:%02x.%d-%08x:%1x:%08x", domain, bus, slot,
		   func, reg, size, mask);
	if (err == 7)
		return 0;
	return -EINVAL;
}

static int pcistub_device_id_add(int domain, int bus, int slot, int func)
{
	struct pcistub_device_id *pci_dev_id;
	unsigned long flags;

	pci_dev_id = kmalloc(sizeof(*pci_dev_id), GFP_KERNEL);
	if (!pci_dev_id)
		return -ENOMEM;

	pci_dev_id->domain = domain;
	pci_dev_id->bus = bus;
	pci_dev_id->devfn = PCI_DEVFN(slot, func);

	pr_debug(DRV_NAME ": wants to seize %04x:%02x:%02x.%d\n",
		 domain, bus, slot, func);

	spin_lock_irqsave(&device_ids_lock, flags);
	list_add_tail(&pci_dev_id->slot_list, &pcistub_device_ids);
	spin_unlock_irqrestore(&device_ids_lock, flags);

	return 0;
}

static int pcistub_device_id_remove(int domain, int bus, int slot, int func)
{
	struct pcistub_device_id *pci_dev_id, *t;
	int devfn = PCI_DEVFN(slot, func);
	int err = -ENOENT;
	unsigned long flags;

	spin_lock_irqsave(&device_ids_lock, flags);
	list_for_each_entry_safe(pci_dev_id, t, &pcistub_device_ids,
				 slot_list) {
		if (pci_dev_id->domain == domain
		    && pci_dev_id->bus == bus && pci_dev_id->devfn == devfn) {
			/* Don't break; here because it's possible the same
			 * slot could be in the list more than once
			 */
			list_del(&pci_dev_id->slot_list);
			kfree(pci_dev_id);

			err = 0;

			pr_debug(DRV_NAME ": removed %04x:%02x:%02x.%d from "
				 "seize list\n", domain, bus, slot, func);
		}
	}
	spin_unlock_irqrestore(&device_ids_lock, flags);

	return err;
}

static int pcistub_reg_add(int domain, int bus, int slot, int func, int reg,
			   int size, int mask)
{
	int err = 0;
	struct pcistub_device *psdev;
	struct pci_dev *dev;
	struct config_field *field;

	psdev = pcistub_device_find(domain, bus, slot, func);
	if (!psdev || !psdev->dev) {
		err = -ENODEV;
		goto out;
	}
	dev = psdev->dev;

	field = kzalloc(sizeof(*field), GFP_ATOMIC);
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
	return err;
}

static ssize_t pcistub_slot_add(struct device_driver *drv, const char *buf,
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
static DRIVER_ATTR(new_slot, S_IWUSR, NULL, pcistub_slot_add);

static ssize_t pcistub_slot_remove(struct device_driver *drv, const char *buf,
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
static DRIVER_ATTR(remove_slot, S_IWUSR, NULL, pcistub_slot_remove);

static ssize_t pcistub_slot_show(struct device_driver *drv, char *buf)
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
static DRIVER_ATTR(slots, S_IRUSR, pcistub_slot_show, NULL);

static ssize_t pcistub_irq_handler_show(struct device_driver *drv, char *buf)
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
static DRIVER_ATTR(irq_handlers, S_IRUSR, pcistub_irq_handler_show, NULL);

static ssize_t pcistub_irq_handler_switch(struct device_driver *drv,
					  const char *buf,
					  size_t count)
{
	struct pcistub_device *psdev;
	struct xen_pcibk_dev_data *dev_data;
	int domain, bus, slot, func;
	int err = -ENOENT;

	err = str_to_slot(buf, &domain, &bus, &slot, &func);
	if (err)
		goto out;

	psdev = pcistub_device_find(domain, bus, slot, func);

	if (!psdev)
		goto out;

	dev_data = pci_get_drvdata(psdev->dev);
	if (!dev_data)
		goto out;

	dev_dbg(&psdev->dev->dev, "%s fake irq handler: %d->%d\n",
		dev_data->irq_name, dev_data->isr_on,
		!dev_data->isr_on);

	dev_data->isr_on = !(dev_data->isr_on);
	if (dev_data->isr_on)
		dev_data->ack_intr = 1;
out:
	if (!err)
		err = count;
	return err;
}
static DRIVER_ATTR(irq_handler_state, S_IWUSR, NULL,
		   pcistub_irq_handler_switch);

static ssize_t pcistub_quirk_add(struct device_driver *drv, const char *buf,
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

static ssize_t pcistub_quirk_show(struct device_driver *drv, char *buf)
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
static DRIVER_ATTR(quirks, S_IRUSR | S_IWUSR, pcistub_quirk_show,
		   pcistub_quirk_add);

static ssize_t permissive_add(struct device_driver *drv, const char *buf,
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
	if (!psdev->dev) {
		err = -ENODEV;
		goto release;
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
static DRIVER_ATTR(permissive, S_IRUSR | S_IWUSR, permissive_show,
		   permissive_add);

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
			if (err != 4) {
				domain = 0;
				err = sscanf(pci_devs_to_hide + pos,
					     " (%x:%x.%x) %n",
					     &bus, &slot, &func, &parsed);
				if (err != 3)
					goto parse_error;
			}

			err = pcistub_device_id_add(domain, bus, slot, func);
			if (err)
				goto out;

			/* if parsed<=0, we've reached the end of the string */
			pos += parsed;
		} while (parsed > 0 && pci_devs_to_hide[pos]);
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
					 &driver_attr_irq_handlers);
	if (!err)
		err = driver_create_file(&xen_pcibk_pci_driver.driver,
					&driver_attr_irq_handler_state);
	if (err)
		pcistub_exit();

out:
	return err;

parse_error:
	printk(KERN_ERR DRV_NAME ": Error parsing pci_devs_to_hide at \"%s\"\n",
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

	return err;
}

static void __exit xen_pcibk_cleanup(void)
{
	xen_pcibk_xenbus_unregister();
	pcistub_exit();
}

module_init(xen_pcibk_init);
module_exit(xen_pcibk_cleanup);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:pci");
