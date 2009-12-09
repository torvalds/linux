/*
 * PCI Backend Xenbus Setup - handles setup with frontend and xend
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <asm/xen/pci.h>
#include <linux/workqueue.h>
#include "pciback.h"

#define INVALID_EVTCHN_IRQ  (-1)
struct workqueue_struct *pciback_wq;

static struct pciback_device *alloc_pdev(struct xenbus_device *xdev)
{
	struct pciback_device *pdev;

	pdev = kzalloc(sizeof(struct pciback_device), GFP_KERNEL);
	if (pdev == NULL)
		goto out;
	dev_dbg(&xdev->dev, "allocated pdev @ 0x%p\n", pdev);

	pdev->xdev = xdev;
	dev_set_drvdata(&xdev->dev, pdev);

	spin_lock_init(&pdev->dev_lock);

	pdev->sh_info = NULL;
	pdev->evtchn_irq = INVALID_EVTCHN_IRQ;
	pdev->be_watching = 0;

	INIT_WORK(&pdev->op_work, pciback_do_op);

	if (pciback_init_devices(pdev)) {
		kfree(pdev);
		pdev = NULL;
	}
out:
	return pdev;
}

static void pciback_disconnect(struct pciback_device *pdev)
{
	spin_lock(&pdev->dev_lock);

	/* Ensure the guest can't trigger our handler before removing devices */
	if (pdev->evtchn_irq != INVALID_EVTCHN_IRQ) {
		unbind_from_irqhandler(pdev->evtchn_irq, pdev);
		pdev->evtchn_irq = INVALID_EVTCHN_IRQ;
	}

	/* If the driver domain started an op, make sure we complete it
	 * before releasing the shared memory */
	flush_workqueue(pciback_wq);

	if (pdev->sh_info != NULL) {
		xenbus_unmap_ring_vfree(pdev->xdev, pdev->sh_info);
		pdev->sh_info = NULL;
	}

	spin_unlock(&pdev->dev_lock);
}

static void free_pdev(struct pciback_device *pdev)
{
	if (pdev->be_watching)
		unregister_xenbus_watch(&pdev->be_watch);

	pciback_disconnect(pdev);

	pciback_release_devices(pdev);

	dev_set_drvdata(&pdev->xdev->dev, NULL);
	pdev->xdev = NULL;

	kfree(pdev);
}

static int pciback_do_attach(struct pciback_device *pdev, int gnt_ref,
			     int remote_evtchn)
{
	int err = 0;
	void *vaddr;

	dev_dbg(&pdev->xdev->dev,
		"Attaching to frontend resources - gnt_ref=%d evtchn=%d\n",
		gnt_ref, remote_evtchn);

	err = xenbus_map_ring_valloc(pdev->xdev, gnt_ref, &vaddr);
	if (err < 0) {
		xenbus_dev_fatal(pdev->xdev, err,
				"Error mapping other domain page in ours.");
		goto out;
	}
	pdev->sh_info = vaddr;

	err = bind_interdomain_evtchn_to_irqhandler(
		pdev->xdev->otherend_id, remote_evtchn, pciback_handle_event,
		0, "pciback", pdev);
	if (err < 0) {
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error binding event channel to IRQ");
		goto out;
	}
	pdev->evtchn_irq = err;
	err = 0;

	dev_dbg(&pdev->xdev->dev, "Attached!\n");
out:
	return err;
}

static int pciback_attach(struct pciback_device *pdev)
{
	int err = 0;
	int gnt_ref, remote_evtchn;
	char *magic = NULL;

	spin_lock(&pdev->dev_lock);

	/* Make sure we only do this setup once */
	if (xenbus_read_driver_state(pdev->xdev->nodename) !=
	    XenbusStateInitialised)
		goto out;

	/* Wait for frontend to state that it has published the configuration */
	if (xenbus_read_driver_state(pdev->xdev->otherend) !=
	    XenbusStateInitialised)
		goto out;

	dev_dbg(&pdev->xdev->dev, "Reading frontend config\n");

	err = xenbus_gather(XBT_NIL, pdev->xdev->otherend,
			    "pci-op-ref", "%u", &gnt_ref,
			    "event-channel", "%u", &remote_evtchn,
			    "magic", NULL, &magic, NULL);
	if (err) {
		/* If configuration didn't get read correctly, wait longer */
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error reading configuration from frontend");
		goto out;
	}

	if (magic == NULL || strcmp(magic, XEN_PCI_MAGIC) != 0) {
		xenbus_dev_fatal(pdev->xdev, -EFAULT,
				 "version mismatch (%s/%s) with pcifront - "
				 "halting pciback",
				 magic, XEN_PCI_MAGIC);
		goto out;
	}

	err = pciback_do_attach(pdev, gnt_ref, remote_evtchn);
	if (err)
		goto out;

	dev_dbg(&pdev->xdev->dev, "Connecting...\n");

	err = xenbus_switch_state(pdev->xdev, XenbusStateConnected);
	if (err)
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error switching to connected state!");

	dev_dbg(&pdev->xdev->dev, "Connected? %d\n", err);
out:
	spin_unlock(&pdev->dev_lock);

	kfree(magic);

	return err;
}

static int pciback_publish_pci_dev(struct pciback_device *pdev,
				   unsigned int domain, unsigned int bus,
				   unsigned int devfn, unsigned int devid)
{
	int err;
	int len;
	char str[64];

	len = snprintf(str, sizeof(str), "vdev-%d", devid);
	if (unlikely(len >= (sizeof(str) - 1))) {
		err = -ENOMEM;
		goto out;
	}

	err = xenbus_printf(XBT_NIL, pdev->xdev->nodename, str,
			    "%04x:%02x:%02x.%02x", domain, bus,
			    PCI_SLOT(devfn), PCI_FUNC(devfn));

out:
	return err;
}

static int pciback_export_device(struct pciback_device *pdev,
				 int domain, int bus, int slot, int func,
				 int devid)
{
	struct pci_dev *dev;
	int err = 0;

	dev_dbg(&pdev->xdev->dev, "exporting dom %x bus %x slot %x func %x\n",
		domain, bus, slot, func);

	dev = pcistub_get_pci_dev_by_slot(pdev, domain, bus, slot, func);
	if (!dev) {
		err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Couldn't locate PCI device "
				 "(%04x:%02x:%02x.%01x)! "
				 "perhaps already in-use?",
				 domain, bus, slot, func);
		goto out;
	}

	err = pciback_add_pci_dev(pdev, dev, devid, pciback_publish_pci_dev);
	if (err)
		goto out;

	dev_dbg(&dev->dev, "registering for %d\n", pdev->xdev->otherend_id);
	if (xen_register_device_domain_owner(dev,
					     pdev->xdev->otherend_id) != 0) {
		dev_err(&dev->dev, "device has been assigned to another " \
			"domain! Over-writting the ownership, but beware.\n");
		xen_unregister_device_domain_owner(dev);
		xen_register_device_domain_owner(dev, pdev->xdev->otherend_id);
	}

	/* TODO: It'd be nice to export a bridge and have all of its children
	 * get exported with it. This may be best done in xend (which will
	 * have to calculate resource usage anyway) but we probably want to
	 * put something in here to ensure that if a bridge gets given to a
	 * driver domain, that all devices under that bridge are not given
	 * to other driver domains (as he who controls the bridge can disable
	 * it and stop the other devices from working).
	 */
out:
	return err;
}

static int pciback_remove_device(struct pciback_device *pdev,
				 int domain, int bus, int slot, int func)
{
	int err = 0;
	struct pci_dev *dev;

	dev_dbg(&pdev->xdev->dev, "removing dom %x bus %x slot %x func %x\n",
		domain, bus, slot, func);

	dev = pciback_get_pci_dev(pdev, domain, bus, PCI_DEVFN(slot, func));
	if (!dev) {
		err = -EINVAL;
		dev_dbg(&pdev->xdev->dev, "Couldn't locate PCI device "
			"(%04x:%02x:%02x.%01x)! not owned by this domain\n",
			domain, bus, slot, func);
		goto out;
	}

	dev_dbg(&dev->dev, "unregistering for %d\n", pdev->xdev->otherend_id);
	xen_unregister_device_domain_owner(dev);

	pciback_release_pci_dev(pdev, dev);

out:
	return err;
}

static int pciback_publish_pci_root(struct pciback_device *pdev,
				    unsigned int domain, unsigned int bus)
{
	unsigned int d, b;
	int i, root_num, len, err;
	char str[64];

	dev_dbg(&pdev->xdev->dev, "Publishing pci roots\n");

	err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename,
			   "root_num", "%d", &root_num);
	if (err == 0 || err == -ENOENT)
		root_num = 0;
	else if (err < 0)
		goto out;

	/* Verify that we haven't already published this pci root */
	for (i = 0; i < root_num; i++) {
		len = snprintf(str, sizeof(str), "root-%d", i);
		if (unlikely(len >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}

		err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename,
				   str, "%x:%x", &d, &b);
		if (err < 0)
			goto out;
		if (err != 2) {
			err = -EINVAL;
			goto out;
		}

		if (d == domain && b == bus) {
			err = 0;
			goto out;
		}
	}

	len = snprintf(str, sizeof(str), "root-%d", root_num);
	if (unlikely(len >= (sizeof(str) - 1))) {
		err = -ENOMEM;
		goto out;
	}

	dev_dbg(&pdev->xdev->dev, "writing root %d at %04x:%02x\n",
		root_num, domain, bus);

	err = xenbus_printf(XBT_NIL, pdev->xdev->nodename, str,
			    "%04x:%02x", domain, bus);
	if (err)
		goto out;

	err = xenbus_printf(XBT_NIL, pdev->xdev->nodename,
			    "root_num", "%d", (root_num + 1));

out:
	return err;
}

static int pciback_reconfigure(struct pciback_device *pdev)
{
	int err = 0;
	int num_devs;
	int domain, bus, slot, func;
	int substate;
	int i, len;
	char state_str[64];
	char dev_str[64];

	spin_lock(&pdev->dev_lock);

	dev_dbg(&pdev->xdev->dev, "Reconfiguring device ...\n");

	/* Make sure we only reconfigure once */
	if (xenbus_read_driver_state(pdev->xdev->nodename) !=
	    XenbusStateReconfiguring)
		goto out;

	err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename, "num_devs", "%d",
			   &num_devs);
	if (err != 1) {
		if (err >= 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error reading number of devices");
		goto out;
	}

	for (i = 0; i < num_devs; i++) {
		len = snprintf(state_str, sizeof(state_str), "state-%d", i);
		if (unlikely(len >= (sizeof(state_str) - 1))) {
			err = -ENOMEM;
			xenbus_dev_fatal(pdev->xdev, err,
					 "String overflow while reading "
					 "configuration");
			goto out;
		}
		err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename, state_str,
				   "%d", &substate);
		if (err != 1)
			substate = XenbusStateUnknown;

		switch (substate) {
		case XenbusStateInitialising:
			dev_dbg(&pdev->xdev->dev, "Attaching dev-%d ...\n", i);

			len = snprintf(dev_str, sizeof(dev_str), "dev-%d", i);
			if (unlikely(len >= (sizeof(dev_str) - 1))) {
				err = -ENOMEM;
				xenbus_dev_fatal(pdev->xdev, err,
						 "String overflow while "
						 "reading configuration");
				goto out;
			}
			err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename,
					   dev_str, "%x:%x:%x.%x",
					   &domain, &bus, &slot, &func);
			if (err < 0) {
				xenbus_dev_fatal(pdev->xdev, err,
						 "Error reading device "
						 "configuration");
				goto out;
			}
			if (err != 4) {
				err = -EINVAL;
				xenbus_dev_fatal(pdev->xdev, err,
						 "Error parsing pci device "
						 "configuration");
				goto out;
			}

			err = pciback_export_device(pdev, domain, bus, slot,
						    func, i);
			if (err)
				goto out;

			/* Publish pci roots. */
			err = pciback_publish_pci_roots(pdev,
						pciback_publish_pci_root);
			if (err) {
				xenbus_dev_fatal(pdev->xdev, err,
						 "Error while publish PCI root"
						 "buses for frontend");
				goto out;
			}

			err = xenbus_printf(XBT_NIL, pdev->xdev->nodename,
					    state_str, "%d",
					    XenbusStateInitialised);
			if (err) {
				xenbus_dev_fatal(pdev->xdev, err,
						 "Error switching substate of "
						 "dev-%d\n", i);
				goto out;
			}
			break;

		case XenbusStateClosing:
			dev_dbg(&pdev->xdev->dev, "Detaching dev-%d ...\n", i);

			len = snprintf(dev_str, sizeof(dev_str), "vdev-%d", i);
			if (unlikely(len >= (sizeof(dev_str) - 1))) {
				err = -ENOMEM;
				xenbus_dev_fatal(pdev->xdev, err,
						 "String overflow while "
						 "reading configuration");
				goto out;
			}
			err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename,
					   dev_str, "%x:%x:%x.%x",
					   &domain, &bus, &slot, &func);
			if (err < 0) {
				xenbus_dev_fatal(pdev->xdev, err,
						 "Error reading device "
						 "configuration");
				goto out;
			}
			if (err != 4) {
				err = -EINVAL;
				xenbus_dev_fatal(pdev->xdev, err,
						 "Error parsing pci device "
						 "configuration");
				goto out;
			}

			err = pciback_remove_device(pdev, domain, bus, slot,
						    func);
			if (err)
				goto out;

			/* TODO: If at some point we implement support for pci
			 * root hot-remove on pcifront side, we'll need to
			 * remove unnecessary xenstore nodes of pci roots here.
			 */

			break;

		default:
			break;
		}
	}

	err = xenbus_switch_state(pdev->xdev, XenbusStateReconfigured);
	if (err) {
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error switching to reconfigured state!");
		goto out;
	}

out:
	spin_unlock(&pdev->dev_lock);

	return 0;
}

static void pciback_frontend_changed(struct xenbus_device *xdev,
				     enum xenbus_state fe_state)
{
	struct pciback_device *pdev = dev_get_drvdata(&xdev->dev);

	dev_dbg(&xdev->dev, "fe state changed %d\n", fe_state);

	switch (fe_state) {
	case XenbusStateInitialised:
		pciback_attach(pdev);
		break;

	case XenbusStateReconfiguring:
		pciback_reconfigure(pdev);
		break;

	case XenbusStateConnected:
		/* pcifront switched its state from reconfiguring to connected.
		 * Then switch to connected state.
		 */
		xenbus_switch_state(xdev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		pciback_disconnect(pdev);
		xenbus_switch_state(xdev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		pciback_disconnect(pdev);
		xenbus_switch_state(xdev, XenbusStateClosed);
		if (xenbus_dev_is_online(xdev))
			break;
		/* fall through if not online */
	case XenbusStateUnknown:
		dev_dbg(&xdev->dev, "frontend is gone! unregister device\n");
		device_unregister(&xdev->dev);
		break;

	default:
		break;
	}
}

static int pciback_setup_backend(struct pciback_device *pdev)
{
	/* Get configuration from xend (if available now) */
	int domain, bus, slot, func;
	int err = 0;
	int i, num_devs;
	char dev_str[64];
	char state_str[64];

	spin_lock(&pdev->dev_lock);

	/* It's possible we could get the call to setup twice, so make sure
	 * we're not already connected.
	 */
	if (xenbus_read_driver_state(pdev->xdev->nodename) !=
	    XenbusStateInitWait)
		goto out;

	dev_dbg(&pdev->xdev->dev, "getting be setup\n");

	err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename, "num_devs", "%d",
			   &num_devs);
	if (err != 1) {
		if (err >= 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error reading number of devices");
		goto out;
	}

	for (i = 0; i < num_devs; i++) {
		int l = snprintf(dev_str, sizeof(dev_str), "dev-%d", i);
		if (unlikely(l >= (sizeof(dev_str) - 1))) {
			err = -ENOMEM;
			xenbus_dev_fatal(pdev->xdev, err,
					 "String overflow while reading "
					 "configuration");
			goto out;
		}

		err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename, dev_str,
				   "%x:%x:%x.%x", &domain, &bus, &slot, &func);
		if (err < 0) {
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error reading device configuration");
			goto out;
		}
		if (err != 4) {
			err = -EINVAL;
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error parsing pci device "
					 "configuration");
			goto out;
		}

		err = pciback_export_device(pdev, domain, bus, slot, func, i);
		if (err)
			goto out;

		/* Switch substate of this device. */
		l = snprintf(state_str, sizeof(state_str), "state-%d", i);
		if (unlikely(l >= (sizeof(state_str) - 1))) {
			err = -ENOMEM;
			xenbus_dev_fatal(pdev->xdev, err,
					 "String overflow while reading "
					 "configuration");
			goto out;
		}
		err = xenbus_printf(XBT_NIL, pdev->xdev->nodename, state_str,
				    "%d", XenbusStateInitialised);
		if (err) {
			xenbus_dev_fatal(pdev->xdev, err, "Error switching "
					 "substate of dev-%d\n", i);
			goto out;
		}
	}

	err = pciback_publish_pci_roots(pdev, pciback_publish_pci_root);
	if (err) {
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error while publish PCI root buses "
				 "for frontend");
		goto out;
	}

	err = xenbus_switch_state(pdev->xdev, XenbusStateInitialised);
	if (err)
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error switching to initialised state!");

out:
	spin_unlock(&pdev->dev_lock);

	if (!err)
		/* see if pcifront is already configured (if not, we'll wait) */
		pciback_attach(pdev);

	return err;
}

static void pciback_be_watch(struct xenbus_watch *watch,
			     const char **vec, unsigned int len)
{
	struct pciback_device *pdev =
	    container_of(watch, struct pciback_device, be_watch);

	switch (xenbus_read_driver_state(pdev->xdev->nodename)) {
	case XenbusStateInitWait:
		pciback_setup_backend(pdev);
		break;

	default:
		break;
	}
}

static int pciback_xenbus_probe(struct xenbus_device *dev,
				const struct xenbus_device_id *id)
{
	int err = 0;
	struct pciback_device *pdev = alloc_pdev(dev);

	if (pdev == NULL) {
		err = -ENOMEM;
		xenbus_dev_fatal(dev, err,
				 "Error allocating pciback_device struct");
		goto out;
	}

	/* wait for xend to configure us */
	err = xenbus_switch_state(dev, XenbusStateInitWait);
	if (err)
		goto out;

	/* watch the backend node for backend configuration information */
	err = xenbus_watch_path(dev, dev->nodename, &pdev->be_watch,
				pciback_be_watch);
	if (err)
		goto out;
	pdev->be_watching = 1;

	/* We need to force a call to our callback here in case
	 * xend already configured us!
	 */
	pciback_be_watch(&pdev->be_watch, NULL, 0);

out:
	return err;
}

static int pciback_xenbus_remove(struct xenbus_device *dev)
{
	struct pciback_device *pdev = dev_get_drvdata(&dev->dev);

	if (pdev != NULL)
		free_pdev(pdev);

	return 0;
}

static const struct xenbus_device_id xenpci_ids[] = {
	{"pci"},
	{""},
};

static struct xenbus_driver xenbus_pciback_driver = {
	.name			= "pciback",
	.owner			= THIS_MODULE,
	.ids			= xenpci_ids,
	.probe			= pciback_xenbus_probe,
	.remove			= pciback_xenbus_remove,
	.otherend_changed	= pciback_frontend_changed,
};

int __init pciback_xenbus_register(void)
{
	pciback_wq = create_workqueue("pciback_workqueue");
	if (!pciback_wq) {
		printk(KERN_ERR "%s: create"
			"pciback_workqueue failed\n", __func__);
		return -EFAULT;
	}
	return xenbus_register_backend(&xenbus_pciback_driver);
}

void __exit pciback_xenbus_unregister(void)
{
	destroy_workqueue(pciback_wq);
	xenbus_unregister_driver(&xenbus_pciback_driver);
}
