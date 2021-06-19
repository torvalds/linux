// SPDX-License-Identifier: GPL-2.0
/*
 * Xen PCI Frontend
 *
 * Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/page.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <xen/interface/io/pciif.h>
#include <asm/xen/pci.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/swiotlb.h>
#include <xen/platform_pci.h>

#include <asm/xen/swiotlb-xen.h>

#define INVALID_EVTCHN    (-1)

struct pci_bus_entry {
	struct list_head list;
	struct pci_bus *bus;
};

#define _PDEVB_op_active		(0)
#define PDEVB_op_active			(1 << (_PDEVB_op_active))

struct pcifront_device {
	struct xenbus_device *xdev;
	struct list_head root_buses;

	int evtchn;
	grant_ref_t gnt_ref;

	int irq;

	/* Lock this when doing any operations in sh_info */
	spinlock_t sh_info_lock;
	struct xen_pci_sharedinfo *sh_info;
	struct work_struct op_work;
	unsigned long flags;

};

struct pcifront_sd {
	struct pci_sysdata sd;
	struct pcifront_device *pdev;
};

static inline struct pcifront_device *
pcifront_get_pdev(struct pcifront_sd *sd)
{
	return sd->pdev;
}

static inline void pcifront_init_sd(struct pcifront_sd *sd,
				    unsigned int domain, unsigned int bus,
				    struct pcifront_device *pdev)
{
	/* Because we do not expose that information via XenBus. */
	sd->sd.node = first_online_node;
	sd->sd.domain = domain;
	sd->pdev = pdev;
}

static DEFINE_SPINLOCK(pcifront_dev_lock);
static struct pcifront_device *pcifront_dev;

static int errno_to_pcibios_err(int errno)
{
	switch (errno) {
	case XEN_PCI_ERR_success:
		return PCIBIOS_SUCCESSFUL;

	case XEN_PCI_ERR_dev_not_found:
		return PCIBIOS_DEVICE_NOT_FOUND;

	case XEN_PCI_ERR_invalid_offset:
	case XEN_PCI_ERR_op_failed:
		return PCIBIOS_BAD_REGISTER_NUMBER;

	case XEN_PCI_ERR_not_implemented:
		return PCIBIOS_FUNC_NOT_SUPPORTED;

	case XEN_PCI_ERR_access_denied:
		return PCIBIOS_SET_FAILED;
	}
	return errno;
}

static inline void schedule_pcifront_aer_op(struct pcifront_device *pdev)
{
	if (test_bit(_XEN_PCIB_active, (unsigned long *)&pdev->sh_info->flags)
		&& !test_and_set_bit(_PDEVB_op_active, &pdev->flags)) {
		dev_dbg(&pdev->xdev->dev, "schedule aer frontend job\n");
		schedule_work(&pdev->op_work);
	}
}

static int do_pci_op(struct pcifront_device *pdev, struct xen_pci_op *op)
{
	int err = 0;
	struct xen_pci_op *active_op = &pdev->sh_info->op;
	unsigned long irq_flags;
	evtchn_port_t port = pdev->evtchn;
	unsigned irq = pdev->irq;
	s64 ns, ns_timeout;

	spin_lock_irqsave(&pdev->sh_info_lock, irq_flags);

	memcpy(active_op, op, sizeof(struct xen_pci_op));

	/* Go */
	wmb();
	set_bit(_XEN_PCIF_active, (unsigned long *)&pdev->sh_info->flags);
	notify_remote_via_evtchn(port);

	/*
	 * We set a poll timeout of 3 seconds but give up on return after
	 * 2 seconds. It is better to time out too late rather than too early
	 * (in the latter case we end up continually re-executing poll() with a
	 * timeout in the past). 1s difference gives plenty of slack for error.
	 */
	ns_timeout = ktime_get_ns() + 2 * (s64)NSEC_PER_SEC;

	xen_clear_irq_pending(irq);

	while (test_bit(_XEN_PCIF_active,
			(unsigned long *)&pdev->sh_info->flags)) {
		xen_poll_irq_timeout(irq, jiffies + 3*HZ);
		xen_clear_irq_pending(irq);
		ns = ktime_get_ns();
		if (ns > ns_timeout) {
			dev_err(&pdev->xdev->dev,
				"pciback not responding!!!\n");
			clear_bit(_XEN_PCIF_active,
				  (unsigned long *)&pdev->sh_info->flags);
			err = XEN_PCI_ERR_dev_not_found;
			goto out;
		}
	}

	/*
	* We might lose backend service request since we
	* reuse same evtchn with pci_conf backend response. So re-schedule
	* aer pcifront service.
	*/
	if (test_bit(_XEN_PCIB_active,
			(unsigned long *)&pdev->sh_info->flags)) {
		dev_err(&pdev->xdev->dev,
			"schedule aer pcifront service\n");
		schedule_pcifront_aer_op(pdev);
	}

	memcpy(op, active_op, sizeof(struct xen_pci_op));

	err = op->err;
out:
	spin_unlock_irqrestore(&pdev->sh_info_lock, irq_flags);
	return err;
}

/* Access to this function is spinlocked in drivers/pci/access.c */
static int pcifront_bus_read(struct pci_bus *bus, unsigned int devfn,
			     int where, int size, u32 *val)
{
	int err = 0;
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_conf_read,
		.domain = pci_domain_nr(bus),
		.bus    = bus->number,
		.devfn  = devfn,
		.offset = where,
		.size   = size,
	};
	struct pcifront_sd *sd = bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	dev_dbg(&pdev->xdev->dev,
		"read dev=%04x:%02x:%02x.%d - offset %x size %d\n",
		pci_domain_nr(bus), bus->number, PCI_SLOT(devfn),
		PCI_FUNC(devfn), where, size);

	err = do_pci_op(pdev, &op);

	if (likely(!err)) {
		dev_dbg(&pdev->xdev->dev, "read got back value %x\n",
			op.value);

		*val = op.value;
	} else if (err == -ENODEV) {
		/* No device here, pretend that it just returned 0 */
		err = 0;
		*val = 0;
	}

	return errno_to_pcibios_err(err);
}

/* Access to this function is spinlocked in drivers/pci/access.c */
static int pcifront_bus_write(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 val)
{
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_conf_write,
		.domain = pci_domain_nr(bus),
		.bus    = bus->number,
		.devfn  = devfn,
		.offset = where,
		.size   = size,
		.value  = val,
	};
	struct pcifront_sd *sd = bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	dev_dbg(&pdev->xdev->dev,
		"write dev=%04x:%02x:%02x.%d - offset %x size %d val %x\n",
		pci_domain_nr(bus), bus->number,
		PCI_SLOT(devfn), PCI_FUNC(devfn), where, size, val);

	return errno_to_pcibios_err(do_pci_op(pdev, &op));
}

static struct pci_ops pcifront_bus_ops = {
	.read = pcifront_bus_read,
	.write = pcifront_bus_write,
};

#ifdef CONFIG_PCI_MSI
static int pci_frontend_enable_msix(struct pci_dev *dev,
				    int vector[], int nvec)
{
	int err;
	int i;
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_enable_msix,
		.domain = pci_domain_nr(dev->bus),
		.bus = dev->bus->number,
		.devfn = dev->devfn,
		.value = nvec,
	};
	struct pcifront_sd *sd = dev->bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);
	struct msi_desc *entry;

	if (nvec > SH_INFO_MAX_VEC) {
		pci_err(dev, "too many vectors (0x%x) for PCI frontend:"
				   " Increase SH_INFO_MAX_VEC\n", nvec);
		return -EINVAL;
	}

	i = 0;
	for_each_pci_msi_entry(entry, dev) {
		op.msix_entries[i].entry = entry->msi_attrib.entry_nr;
		/* Vector is useless at this point. */
		op.msix_entries[i].vector = -1;
		i++;
	}

	err = do_pci_op(pdev, &op);

	if (likely(!err)) {
		if (likely(!op.value)) {
			/* we get the result */
			for (i = 0; i < nvec; i++) {
				if (op.msix_entries[i].vector <= 0) {
					pci_warn(dev, "MSI-X entry %d is invalid: %d!\n",
						i, op.msix_entries[i].vector);
					err = -EINVAL;
					vector[i] = -1;
					continue;
				}
				vector[i] = op.msix_entries[i].vector;
			}
		} else {
			pr_info("enable msix get value %x\n", op.value);
			err = op.value;
		}
	} else {
		pci_err(dev, "enable msix get err %x\n", err);
	}
	return err;
}

static void pci_frontend_disable_msix(struct pci_dev *dev)
{
	int err;
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_disable_msix,
		.domain = pci_domain_nr(dev->bus),
		.bus = dev->bus->number,
		.devfn = dev->devfn,
	};
	struct pcifront_sd *sd = dev->bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	err = do_pci_op(pdev, &op);

	/* What should do for error ? */
	if (err)
		pci_err(dev, "pci_disable_msix get err %x\n", err);
}

static int pci_frontend_enable_msi(struct pci_dev *dev, int vector[])
{
	int err;
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_enable_msi,
		.domain = pci_domain_nr(dev->bus),
		.bus = dev->bus->number,
		.devfn = dev->devfn,
	};
	struct pcifront_sd *sd = dev->bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	err = do_pci_op(pdev, &op);
	if (likely(!err)) {
		vector[0] = op.value;
		if (op.value <= 0) {
			pci_warn(dev, "MSI entry is invalid: %d!\n",
				op.value);
			err = -EINVAL;
			vector[0] = -1;
		}
	} else {
		pci_err(dev, "pci frontend enable msi failed for dev "
				    "%x:%x\n", op.bus, op.devfn);
		err = -EINVAL;
	}
	return err;
}

static void pci_frontend_disable_msi(struct pci_dev *dev)
{
	int err;
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_disable_msi,
		.domain = pci_domain_nr(dev->bus),
		.bus = dev->bus->number,
		.devfn = dev->devfn,
	};
	struct pcifront_sd *sd = dev->bus->sysdata;
	struct pcifront_device *pdev = pcifront_get_pdev(sd);

	err = do_pci_op(pdev, &op);
	if (err == XEN_PCI_ERR_dev_not_found) {
		/* XXX No response from backend, what shall we do? */
		pr_info("get no response from backend for disable MSI\n");
		return;
	}
	if (err)
		/* how can pciback notify us fail? */
		pr_info("get fake response from backend\n");
}

static struct xen_pci_frontend_ops pci_frontend_ops = {
	.enable_msi = pci_frontend_enable_msi,
	.disable_msi = pci_frontend_disable_msi,
	.enable_msix = pci_frontend_enable_msix,
	.disable_msix = pci_frontend_disable_msix,
};

static void pci_frontend_registrar(int enable)
{
	if (enable)
		xen_pci_frontend = &pci_frontend_ops;
	else
		xen_pci_frontend = NULL;
};
#else
static inline void pci_frontend_registrar(int enable) { };
#endif /* CONFIG_PCI_MSI */

/* Claim resources for the PCI frontend as-is, backend won't allow changes */
static int pcifront_claim_resource(struct pci_dev *dev, void *data)
{
	struct pcifront_device *pdev = data;
	int i;
	struct resource *r;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		r = &dev->resource[i];

		if (!r->parent && r->start && r->flags) {
			dev_info(&pdev->xdev->dev, "claiming resource %s/%d\n",
				pci_name(dev), i);
			if (pci_claim_resource(dev, i)) {
				dev_err(&pdev->xdev->dev, "Could not claim resource %s/%d! "
					"Device offline. Try using e820_host=1 in the guest config.\n",
					pci_name(dev), i);
			}
		}
	}

	return 0;
}

static int pcifront_scan_bus(struct pcifront_device *pdev,
				unsigned int domain, unsigned int bus,
				struct pci_bus *b)
{
	struct pci_dev *d;
	unsigned int devfn;

	/* Scan the bus for functions and add.
	 * We omit handling of PCI bridge attachment because pciback prevents
	 * bridges from being exported.
	 */
	for (devfn = 0; devfn < 0x100; devfn++) {
		d = pci_get_slot(b, devfn);
		if (d) {
			/* Device is already known. */
			pci_dev_put(d);
			continue;
		}

		d = pci_scan_single_device(b, devfn);
		if (d)
			dev_info(&pdev->xdev->dev, "New device on "
				 "%04x:%02x:%02x.%d found.\n", domain, bus,
				 PCI_SLOT(devfn), PCI_FUNC(devfn));
	}

	return 0;
}

static int pcifront_scan_root(struct pcifront_device *pdev,
				 unsigned int domain, unsigned int bus)
{
	struct pci_bus *b;
	LIST_HEAD(resources);
	struct pcifront_sd *sd = NULL;
	struct pci_bus_entry *bus_entry = NULL;
	int err = 0;
	static struct resource busn_res = {
		.start = 0,
		.end = 255,
		.flags = IORESOURCE_BUS,
	};

#ifndef CONFIG_PCI_DOMAINS
	if (domain != 0) {
		dev_err(&pdev->xdev->dev,
			"PCI Root in non-zero PCI Domain! domain=%d\n", domain);
		dev_err(&pdev->xdev->dev,
			"Please compile with CONFIG_PCI_DOMAINS\n");
		err = -EINVAL;
		goto err_out;
	}
#endif

	dev_info(&pdev->xdev->dev, "Creating PCI Frontend Bus %04x:%02x\n",
		 domain, bus);

	bus_entry = kzalloc(sizeof(*bus_entry), GFP_KERNEL);
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!bus_entry || !sd) {
		err = -ENOMEM;
		goto err_out;
	}
	pci_add_resource(&resources, &ioport_resource);
	pci_add_resource(&resources, &iomem_resource);
	pci_add_resource(&resources, &busn_res);
	pcifront_init_sd(sd, domain, bus, pdev);

	pci_lock_rescan_remove();

	b = pci_scan_root_bus(&pdev->xdev->dev, bus,
				  &pcifront_bus_ops, sd, &resources);
	if (!b) {
		dev_err(&pdev->xdev->dev,
			"Error creating PCI Frontend Bus!\n");
		err = -ENOMEM;
		pci_unlock_rescan_remove();
		pci_free_resource_list(&resources);
		goto err_out;
	}

	bus_entry->bus = b;

	list_add(&bus_entry->list, &pdev->root_buses);

	/* pci_scan_root_bus skips devices which do not have a
	* devfn==0. The pcifront_scan_bus enumerates all devfn. */
	err = pcifront_scan_bus(pdev, domain, bus, b);

	/* Claim resources before going "live" with our devices */
	pci_walk_bus(b, pcifront_claim_resource, pdev);

	/* Create SysFS and notify udev of the devices. Aka: "going live" */
	pci_bus_add_devices(b);

	pci_unlock_rescan_remove();
	return err;

err_out:
	kfree(bus_entry);
	kfree(sd);

	return err;
}

static int pcifront_rescan_root(struct pcifront_device *pdev,
				   unsigned int domain, unsigned int bus)
{
	int err;
	struct pci_bus *b;

#ifndef CONFIG_PCI_DOMAINS
	if (domain != 0) {
		dev_err(&pdev->xdev->dev,
			"PCI Root in non-zero PCI Domain! domain=%d\n", domain);
		dev_err(&pdev->xdev->dev,
			"Please compile with CONFIG_PCI_DOMAINS\n");
		return -EINVAL;
	}
#endif

	dev_info(&pdev->xdev->dev, "Rescanning PCI Frontend Bus %04x:%02x\n",
		 domain, bus);

	b = pci_find_bus(domain, bus);
	if (!b)
		/* If the bus is unknown, create it. */
		return pcifront_scan_root(pdev, domain, bus);

	err = pcifront_scan_bus(pdev, domain, bus, b);

	/* Claim resources before going "live" with our devices */
	pci_walk_bus(b, pcifront_claim_resource, pdev);

	/* Create SysFS and notify udev of the devices. Aka: "going live" */
	pci_bus_add_devices(b);

	return err;
}

static void free_root_bus_devs(struct pci_bus *bus)
{
	struct pci_dev *dev;

	while (!list_empty(&bus->devices)) {
		dev = container_of(bus->devices.next, struct pci_dev,
				   bus_list);
		pci_dbg(dev, "removing device\n");
		pci_stop_and_remove_bus_device(dev);
	}
}

static void pcifront_free_roots(struct pcifront_device *pdev)
{
	struct pci_bus_entry *bus_entry, *t;

	dev_dbg(&pdev->xdev->dev, "cleaning up root buses\n");

	pci_lock_rescan_remove();
	list_for_each_entry_safe(bus_entry, t, &pdev->root_buses, list) {
		list_del(&bus_entry->list);

		free_root_bus_devs(bus_entry->bus);

		kfree(bus_entry->bus->sysdata);

		device_unregister(bus_entry->bus->bridge);
		pci_remove_bus(bus_entry->bus);

		kfree(bus_entry);
	}
	pci_unlock_rescan_remove();
}

static pci_ers_result_t pcifront_common_process(int cmd,
						struct pcifront_device *pdev,
						pci_channel_state_t state)
{
	pci_ers_result_t result;
	struct pci_driver *pdrv;
	int bus = pdev->sh_info->aer_op.bus;
	int devfn = pdev->sh_info->aer_op.devfn;
	int domain = pdev->sh_info->aer_op.domain;
	struct pci_dev *pcidev;
	int flag = 0;

	dev_dbg(&pdev->xdev->dev,
		"pcifront AER process: cmd %x (bus:%x, devfn%x)",
		cmd, bus, devfn);
	result = PCI_ERS_RESULT_NONE;

	pcidev = pci_get_domain_bus_and_slot(domain, bus, devfn);
	if (!pcidev || !pcidev->driver) {
		dev_err(&pdev->xdev->dev, "device or AER driver is NULL\n");
		pci_dev_put(pcidev);
		return result;
	}
	pdrv = pcidev->driver;

	if (pdrv) {
		if (pdrv->err_handler && pdrv->err_handler->error_detected) {
			pci_dbg(pcidev, "trying to call AER service\n");
			if (pcidev) {
				flag = 1;
				switch (cmd) {
				case XEN_PCI_OP_aer_detected:
					result = pdrv->err_handler->
						 error_detected(pcidev, state);
					break;
				case XEN_PCI_OP_aer_mmio:
					result = pdrv->err_handler->
						 mmio_enabled(pcidev);
					break;
				case XEN_PCI_OP_aer_slotreset:
					result = pdrv->err_handler->
						 slot_reset(pcidev);
					break;
				case XEN_PCI_OP_aer_resume:
					pdrv->err_handler->resume(pcidev);
					break;
				default:
					dev_err(&pdev->xdev->dev,
						"bad request in aer recovery "
						"operation!\n");

				}
			}
		}
	}
	if (!flag)
		result = PCI_ERS_RESULT_NONE;

	return result;
}


static void pcifront_do_aer(struct work_struct *data)
{
	struct pcifront_device *pdev =
		container_of(data, struct pcifront_device, op_work);
	int cmd = pdev->sh_info->aer_op.cmd;
	pci_channel_state_t state =
		(pci_channel_state_t)pdev->sh_info->aer_op.err;

	/*If a pci_conf op is in progress,
		we have to wait until it is done before service aer op*/
	dev_dbg(&pdev->xdev->dev,
		"pcifront service aer bus %x devfn %x\n",
		pdev->sh_info->aer_op.bus, pdev->sh_info->aer_op.devfn);

	pdev->sh_info->aer_op.err = pcifront_common_process(cmd, pdev, state);

	/* Post the operation to the guest. */
	wmb();
	clear_bit(_XEN_PCIB_active, (unsigned long *)&pdev->sh_info->flags);
	notify_remote_via_evtchn(pdev->evtchn);

	/*in case of we lost an aer request in four lines time_window*/
	smp_mb__before_atomic();
	clear_bit(_PDEVB_op_active, &pdev->flags);
	smp_mb__after_atomic();

	schedule_pcifront_aer_op(pdev);

}

static irqreturn_t pcifront_handler_aer(int irq, void *dev)
{
	struct pcifront_device *pdev = dev;
	schedule_pcifront_aer_op(pdev);
	return IRQ_HANDLED;
}
static int pcifront_connect_and_init_dma(struct pcifront_device *pdev)
{
	int err = 0;

	spin_lock(&pcifront_dev_lock);

	if (!pcifront_dev) {
		dev_info(&pdev->xdev->dev, "Installing PCI frontend\n");
		pcifront_dev = pdev;
	} else
		err = -EEXIST;

	spin_unlock(&pcifront_dev_lock);

	if (!err && !is_swiotlb_active(&pdev->xdev->dev)) {
		err = pci_xen_swiotlb_init_late();
		if (err)
			dev_err(&pdev->xdev->dev, "Could not setup SWIOTLB!\n");
	}
	return err;
}

static void pcifront_disconnect(struct pcifront_device *pdev)
{
	spin_lock(&pcifront_dev_lock);

	if (pdev == pcifront_dev) {
		dev_info(&pdev->xdev->dev,
			 "Disconnecting PCI Frontend Buses\n");
		pcifront_dev = NULL;
	}

	spin_unlock(&pcifront_dev_lock);
}
static struct pcifront_device *alloc_pdev(struct xenbus_device *xdev)
{
	struct pcifront_device *pdev;

	pdev = kzalloc(sizeof(struct pcifront_device), GFP_KERNEL);
	if (pdev == NULL)
		goto out;

	pdev->sh_info =
	    (struct xen_pci_sharedinfo *)__get_free_page(GFP_KERNEL);
	if (pdev->sh_info == NULL) {
		kfree(pdev);
		pdev = NULL;
		goto out;
	}
	pdev->sh_info->flags = 0;

	/*Flag for registering PV AER handler*/
	set_bit(_XEN_PCIB_AERHANDLER, (void *)&pdev->sh_info->flags);

	dev_set_drvdata(&xdev->dev, pdev);
	pdev->xdev = xdev;

	INIT_LIST_HEAD(&pdev->root_buses);

	spin_lock_init(&pdev->sh_info_lock);

	pdev->evtchn = INVALID_EVTCHN;
	pdev->gnt_ref = INVALID_GRANT_REF;
	pdev->irq = -1;

	INIT_WORK(&pdev->op_work, pcifront_do_aer);

	dev_dbg(&xdev->dev, "Allocated pdev @ 0x%p pdev->sh_info @ 0x%p\n",
		pdev, pdev->sh_info);
out:
	return pdev;
}

static void free_pdev(struct pcifront_device *pdev)
{
	dev_dbg(&pdev->xdev->dev, "freeing pdev @ 0x%p\n", pdev);

	pcifront_free_roots(pdev);

	cancel_work_sync(&pdev->op_work);

	if (pdev->irq >= 0)
		unbind_from_irqhandler(pdev->irq, pdev);

	if (pdev->evtchn != INVALID_EVTCHN)
		xenbus_free_evtchn(pdev->xdev, pdev->evtchn);

	if (pdev->gnt_ref != INVALID_GRANT_REF)
		gnttab_end_foreign_access(pdev->gnt_ref, 0 /* r/w page */,
					  (unsigned long)pdev->sh_info);
	else
		free_page((unsigned long)pdev->sh_info);

	dev_set_drvdata(&pdev->xdev->dev, NULL);

	kfree(pdev);
}

static int pcifront_publish_info(struct pcifront_device *pdev)
{
	int err = 0;
	struct xenbus_transaction trans;
	grant_ref_t gref;

	err = xenbus_grant_ring(pdev->xdev, pdev->sh_info, 1, &gref);
	if (err < 0)
		goto out;

	pdev->gnt_ref = gref;

	err = xenbus_alloc_evtchn(pdev->xdev, &pdev->evtchn);
	if (err)
		goto out;

	err = bind_evtchn_to_irqhandler(pdev->evtchn, pcifront_handler_aer,
		0, "pcifront", pdev);

	if (err < 0)
		return err;

	pdev->irq = err;

do_publish:
	err = xenbus_transaction_start(&trans);
	if (err) {
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error writing configuration for backend "
				 "(start transaction)");
		goto out;
	}

	err = xenbus_printf(trans, pdev->xdev->nodename,
			    "pci-op-ref", "%u", pdev->gnt_ref);
	if (!err)
		err = xenbus_printf(trans, pdev->xdev->nodename,
				    "event-channel", "%u", pdev->evtchn);
	if (!err)
		err = xenbus_printf(trans, pdev->xdev->nodename,
				    "magic", XEN_PCI_MAGIC);

	if (err) {
		xenbus_transaction_end(trans, 1);
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error writing configuration for backend");
		goto out;
	} else {
		err = xenbus_transaction_end(trans, 0);
		if (err == -EAGAIN)
			goto do_publish;
		else if (err) {
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error completing transaction "
					 "for backend");
			goto out;
		}
	}

	xenbus_switch_state(pdev->xdev, XenbusStateInitialised);

	dev_dbg(&pdev->xdev->dev, "publishing successful!\n");

out:
	return err;
}

static int pcifront_try_connect(struct pcifront_device *pdev)
{
	int err = -EFAULT;
	int i, num_roots, len;
	char str[64];
	unsigned int domain, bus;


	/* Only connect once */
	if (xenbus_read_driver_state(pdev->xdev->nodename) !=
	    XenbusStateInitialised)
		goto out;

	err = pcifront_connect_and_init_dma(pdev);
	if (err && err != -EEXIST) {
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error setting up PCI Frontend");
		goto out;
	}

	err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend,
			   "root_num", "%d", &num_roots);
	if (err == -ENOENT) {
		xenbus_dev_error(pdev->xdev, err,
				 "No PCI Roots found, trying 0000:00");
		err = pcifront_scan_root(pdev, 0, 0);
		if (err) {
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error scanning PCI root 0000:00");
			goto out;
		}
		num_roots = 0;
	} else if (err != 1) {
		if (err == 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error reading number of PCI roots");
		goto out;
	}

	for (i = 0; i < num_roots; i++) {
		len = snprintf(str, sizeof(str), "root-%d", i);
		if (unlikely(len >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}

		err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, str,
				   "%x:%x", &domain, &bus);
		if (err != 2) {
			if (err >= 0)
				err = -EINVAL;
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error reading PCI root %d", i);
			goto out;
		}

		err = pcifront_scan_root(pdev, domain, bus);
		if (err) {
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error scanning PCI root %04x:%02x",
					 domain, bus);
			goto out;
		}
	}

	err = xenbus_switch_state(pdev->xdev, XenbusStateConnected);

out:
	return err;
}

static int pcifront_try_disconnect(struct pcifront_device *pdev)
{
	int err = 0;
	enum xenbus_state prev_state;


	prev_state = xenbus_read_driver_state(pdev->xdev->nodename);

	if (prev_state >= XenbusStateClosing)
		goto out;

	if (prev_state == XenbusStateConnected) {
		pcifront_free_roots(pdev);
		pcifront_disconnect(pdev);
	}

	err = xenbus_switch_state(pdev->xdev, XenbusStateClosed);

out:

	return err;
}

static int pcifront_attach_devices(struct pcifront_device *pdev)
{
	int err = -EFAULT;
	int i, num_roots, len;
	unsigned int domain, bus;
	char str[64];

	if (xenbus_read_driver_state(pdev->xdev->nodename) !=
	    XenbusStateReconfiguring)
		goto out;

	err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend,
			   "root_num", "%d", &num_roots);
	if (err == -ENOENT) {
		xenbus_dev_error(pdev->xdev, err,
				 "No PCI Roots found, trying 0000:00");
		err = pcifront_rescan_root(pdev, 0, 0);
		if (err) {
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error scanning PCI root 0000:00");
			goto out;
		}
		num_roots = 0;
	} else if (err != 1) {
		if (err == 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error reading number of PCI roots");
		goto out;
	}

	for (i = 0; i < num_roots; i++) {
		len = snprintf(str, sizeof(str), "root-%d", i);
		if (unlikely(len >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}

		err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, str,
				   "%x:%x", &domain, &bus);
		if (err != 2) {
			if (err >= 0)
				err = -EINVAL;
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error reading PCI root %d", i);
			goto out;
		}

		err = pcifront_rescan_root(pdev, domain, bus);
		if (err) {
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error scanning PCI root %04x:%02x",
					 domain, bus);
			goto out;
		}
	}

	xenbus_switch_state(pdev->xdev, XenbusStateConnected);

out:
	return err;
}

static int pcifront_detach_devices(struct pcifront_device *pdev)
{
	int err = 0;
	int i, num_devs;
	unsigned int domain, bus, slot, func;
	struct pci_dev *pci_dev;
	char str[64];

	if (xenbus_read_driver_state(pdev->xdev->nodename) !=
	    XenbusStateConnected)
		goto out;

	err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, "num_devs", "%d",
			   &num_devs);
	if (err != 1) {
		if (err >= 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error reading number of PCI devices");
		goto out;
	}

	/* Find devices being detached and remove them. */
	for (i = 0; i < num_devs; i++) {
		int l, state;
		l = snprintf(str, sizeof(str), "state-%d", i);
		if (unlikely(l >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}
		state = xenbus_read_unsigned(pdev->xdev->otherend, str,
					     XenbusStateUnknown);

		if (state != XenbusStateClosing)
			continue;

		/* Remove device. */
		l = snprintf(str, sizeof(str), "vdev-%d", i);
		if (unlikely(l >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}
		err = xenbus_scanf(XBT_NIL, pdev->xdev->otherend, str,
				   "%x:%x:%x.%x", &domain, &bus, &slot, &func);
		if (err != 4) {
			if (err >= 0)
				err = -EINVAL;
			xenbus_dev_fatal(pdev->xdev, err,
					 "Error reading PCI device %d", i);
			goto out;
		}

		pci_dev = pci_get_domain_bus_and_slot(domain, bus,
				PCI_DEVFN(slot, func));
		if (!pci_dev) {
			dev_dbg(&pdev->xdev->dev,
				"Cannot get PCI device %04x:%02x:%02x.%d\n",
				domain, bus, slot, func);
			continue;
		}
		pci_lock_rescan_remove();
		pci_stop_and_remove_bus_device(pci_dev);
		pci_dev_put(pci_dev);
		pci_unlock_rescan_remove();

		dev_dbg(&pdev->xdev->dev,
			"PCI device %04x:%02x:%02x.%d removed.\n",
			domain, bus, slot, func);
	}

	err = xenbus_switch_state(pdev->xdev, XenbusStateReconfiguring);

out:
	return err;
}

static void __ref pcifront_backend_changed(struct xenbus_device *xdev,
						  enum xenbus_state be_state)
{
	struct pcifront_device *pdev = dev_get_drvdata(&xdev->dev);

	switch (be_state) {
	case XenbusStateUnknown:
	case XenbusStateInitialising:
	case XenbusStateInitWait:
	case XenbusStateInitialised:
		break;

	case XenbusStateConnected:
		pcifront_try_connect(pdev);
		break;

	case XenbusStateClosed:
		if (xdev->state == XenbusStateClosed)
			break;
		fallthrough;	/* Missed the backend's CLOSING state */
	case XenbusStateClosing:
		dev_warn(&xdev->dev, "backend going away!\n");
		pcifront_try_disconnect(pdev);
		break;

	case XenbusStateReconfiguring:
		pcifront_detach_devices(pdev);
		break;

	case XenbusStateReconfigured:
		pcifront_attach_devices(pdev);
		break;
	}
}

static int pcifront_xenbus_probe(struct xenbus_device *xdev,
				 const struct xenbus_device_id *id)
{
	int err = 0;
	struct pcifront_device *pdev = alloc_pdev(xdev);

	if (pdev == NULL) {
		err = -ENOMEM;
		xenbus_dev_fatal(xdev, err,
				 "Error allocating pcifront_device struct");
		goto out;
	}

	err = pcifront_publish_info(pdev);
	if (err)
		free_pdev(pdev);

out:
	return err;
}

static int pcifront_xenbus_remove(struct xenbus_device *xdev)
{
	struct pcifront_device *pdev = dev_get_drvdata(&xdev->dev);
	if (pdev)
		free_pdev(pdev);

	return 0;
}

static const struct xenbus_device_id xenpci_ids[] = {
	{"pci"},
	{""},
};

static struct xenbus_driver xenpci_driver = {
	.name			= "pcifront",
	.ids			= xenpci_ids,
	.probe			= pcifront_xenbus_probe,
	.remove			= pcifront_xenbus_remove,
	.otherend_changed	= pcifront_backend_changed,
};

static int __init pcifront_init(void)
{
	if (!xen_pv_domain() || xen_initial_domain())
		return -ENODEV;

	if (!xen_has_pv_devices())
		return -ENODEV;

	pci_frontend_registrar(1 /* enable */);

	return xenbus_register_frontend(&xenpci_driver);
}

static void __exit pcifront_cleanup(void)
{
	xenbus_unregister_driver(&xenpci_driver);
	pci_frontend_registrar(0 /* disable */);
}
module_init(pcifront_init);
module_exit(pcifront_cleanup);

MODULE_DESCRIPTION("Xen PCI passthrough frontend.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("xen:pci");
