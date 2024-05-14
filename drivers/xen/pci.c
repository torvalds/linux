// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2009, Intel Corporation.
 *
 * Author: Weidong Han <weidong.han@intel.com>
 */

#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/pci-acpi.h>
#include <xen/pci.h>
#include <xen/xen.h>
#include <xen/interface/physdev.h>
#include <xen/interface/xen.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include "../pci/pci.h"
#ifdef CONFIG_PCI_MMCONFIG
#include <asm/pci_x86.h>

static int xen_mcfg_late(void);
#endif

static bool __read_mostly pci_seg_supported = true;

static int xen_add_device(struct device *dev)
{
	int r;
	struct pci_dev *pci_dev = to_pci_dev(dev);
#ifdef CONFIG_PCI_IOV
	struct pci_dev *physfn = pci_dev->physfn;
#endif
#ifdef CONFIG_PCI_MMCONFIG
	static bool pci_mcfg_reserved = false;
	/*
	 * Reserve MCFG areas in Xen on first invocation due to this being
	 * potentially called from inside of acpi_init immediately after
	 * MCFG table has been finally parsed.
	 */
	if (!pci_mcfg_reserved) {
		xen_mcfg_late();
		pci_mcfg_reserved = true;
	}
#endif
	if (pci_seg_supported) {
		struct {
			struct physdev_pci_device_add add;
			uint32_t pxm;
		} add_ext = {
			.add.seg = pci_domain_nr(pci_dev->bus),
			.add.bus = pci_dev->bus->number,
			.add.devfn = pci_dev->devfn
		};
		struct physdev_pci_device_add *add = &add_ext.add;

#ifdef CONFIG_ACPI
		acpi_handle handle;
#endif

#ifdef CONFIG_PCI_IOV
		if (pci_dev->is_virtfn) {
			add->flags = XEN_PCI_DEV_VIRTFN;
			add->physfn.bus = physfn->bus->number;
			add->physfn.devfn = physfn->devfn;
		} else
#endif
		if (pci_ari_enabled(pci_dev->bus) && PCI_SLOT(pci_dev->devfn))
			add->flags = XEN_PCI_DEV_EXTFN;

#ifdef CONFIG_ACPI
		handle = ACPI_HANDLE(&pci_dev->dev);
#ifdef CONFIG_PCI_IOV
		if (!handle && pci_dev->is_virtfn)
			handle = ACPI_HANDLE(physfn->bus->bridge);
#endif
		if (!handle) {
			/*
			 * This device was not listed in the ACPI name space at
			 * all. Try to get acpi handle of parent pci bus.
			 */
			struct pci_bus *pbus;
			for (pbus = pci_dev->bus; pbus; pbus = pbus->parent) {
				handle = acpi_pci_get_bridge_handle(pbus);
				if (handle)
					break;
			}
		}
		if (handle) {
			acpi_status status;

			do {
				unsigned long long pxm;

				status = acpi_evaluate_integer(handle, "_PXM",
							       NULL, &pxm);
				if (ACPI_SUCCESS(status)) {
					add->optarr[0] = pxm;
					add->flags |= XEN_PCI_DEV_PXM;
					break;
				}
				status = acpi_get_parent(handle, &handle);
			} while (ACPI_SUCCESS(status));
		}
#endif /* CONFIG_ACPI */

		r = HYPERVISOR_physdev_op(PHYSDEVOP_pci_device_add, add);
		if (r != -ENOSYS)
			return r;
		pci_seg_supported = false;
	}

	if (pci_domain_nr(pci_dev->bus))
		r = -ENOSYS;
#ifdef CONFIG_PCI_IOV
	else if (pci_dev->is_virtfn) {
		struct physdev_manage_pci_ext manage_pci_ext = {
			.bus		= pci_dev->bus->number,
			.devfn		= pci_dev->devfn,
			.is_virtfn 	= 1,
			.physfn.bus	= physfn->bus->number,
			.physfn.devfn	= physfn->devfn,
		};

		r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_add_ext,
			&manage_pci_ext);
	}
#endif
	else if (pci_ari_enabled(pci_dev->bus) && PCI_SLOT(pci_dev->devfn)) {
		struct physdev_manage_pci_ext manage_pci_ext = {
			.bus		= pci_dev->bus->number,
			.devfn		= pci_dev->devfn,
			.is_extfn	= 1,
		};

		r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_add_ext,
			&manage_pci_ext);
	} else {
		struct physdev_manage_pci manage_pci = {
			.bus	= pci_dev->bus->number,
			.devfn	= pci_dev->devfn,
		};

		r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_add,
			&manage_pci);
	}

	return r;
}

static int xen_remove_device(struct device *dev)
{
	int r;
	struct pci_dev *pci_dev = to_pci_dev(dev);

	if (pci_seg_supported) {
		struct physdev_pci_device device = {
			.seg = pci_domain_nr(pci_dev->bus),
			.bus = pci_dev->bus->number,
			.devfn = pci_dev->devfn
		};

		r = HYPERVISOR_physdev_op(PHYSDEVOP_pci_device_remove,
					  &device);
	} else if (pci_domain_nr(pci_dev->bus))
		r = -ENOSYS;
	else {
		struct physdev_manage_pci manage_pci = {
			.bus = pci_dev->bus->number,
			.devfn = pci_dev->devfn
		};

		r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_remove,
					  &manage_pci);
	}

	return r;
}

static int xen_pci_notifier(struct notifier_block *nb,
			    unsigned long action, void *data)
{
	struct device *dev = data;
	int r = 0;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		r = xen_add_device(dev);
		break;
	case BUS_NOTIFY_DEL_DEVICE:
		r = xen_remove_device(dev);
		break;
	default:
		return NOTIFY_DONE;
	}
	if (r)
		dev_err(dev, "Failed to %s - passthrough or MSI/MSI-X might fail!\n",
			action == BUS_NOTIFY_ADD_DEVICE ? "add" :
			(action == BUS_NOTIFY_DEL_DEVICE ? "delete" : "?"));
	return NOTIFY_OK;
}

static struct notifier_block device_nb = {
	.notifier_call = xen_pci_notifier,
};

static int __init register_xen_pci_notifier(void)
{
	if (!xen_initial_domain())
		return 0;

	return bus_register_notifier(&pci_bus_type, &device_nb);
}

arch_initcall(register_xen_pci_notifier);

#ifdef CONFIG_PCI_MMCONFIG
static int xen_mcfg_late(void)
{
	struct pci_mmcfg_region *cfg;
	int rc;

	if (!xen_initial_domain())
		return 0;

	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		return 0;

	if (list_empty(&pci_mmcfg_list))
		return 0;

	/* Check whether they are in the right area. */
	list_for_each_entry(cfg, &pci_mmcfg_list, list) {
		struct physdev_pci_mmcfg_reserved r;

		r.address = cfg->address;
		r.segment = cfg->segment;
		r.start_bus = cfg->start_bus;
		r.end_bus = cfg->end_bus;
		r.flags = XEN_PCI_MMCFG_RESERVED;

		rc = HYPERVISOR_physdev_op(PHYSDEVOP_pci_mmcfg_reserved, &r);
		switch (rc) {
		case 0:
		case -ENOSYS:
			continue;

		default:
			pr_warn("Failed to report MMCONFIG reservation"
				" state for %s to hypervisor"
				" (%d)\n",
				cfg->name, rc);
		}
	}
	return 0;
}
#endif

#ifdef CONFIG_XEN_DOM0
struct xen_device_domain_owner {
	domid_t domain;
	struct pci_dev *dev;
	struct list_head list;
};

static DEFINE_SPINLOCK(dev_domain_list_spinlock);
static LIST_HEAD(dev_domain_list);

static struct xen_device_domain_owner *find_device(struct pci_dev *dev)
{
	struct xen_device_domain_owner *owner;

	list_for_each_entry(owner, &dev_domain_list, list) {
		if (owner->dev == dev)
			return owner;
	}
	return NULL;
}

int xen_find_device_domain_owner(struct pci_dev *dev)
{
	struct xen_device_domain_owner *owner;
	int domain = -ENODEV;

	spin_lock(&dev_domain_list_spinlock);
	owner = find_device(dev);
	if (owner)
		domain = owner->domain;
	spin_unlock(&dev_domain_list_spinlock);
	return domain;
}
EXPORT_SYMBOL_GPL(xen_find_device_domain_owner);

int xen_register_device_domain_owner(struct pci_dev *dev, uint16_t domain)
{
	struct xen_device_domain_owner *owner;

	owner = kzalloc(sizeof(struct xen_device_domain_owner), GFP_KERNEL);
	if (!owner)
		return -ENODEV;

	spin_lock(&dev_domain_list_spinlock);
	if (find_device(dev)) {
		spin_unlock(&dev_domain_list_spinlock);
		kfree(owner);
		return -EEXIST;
	}
	owner->domain = domain;
	owner->dev = dev;
	list_add_tail(&owner->list, &dev_domain_list);
	spin_unlock(&dev_domain_list_spinlock);
	return 0;
}
EXPORT_SYMBOL_GPL(xen_register_device_domain_owner);

int xen_unregister_device_domain_owner(struct pci_dev *dev)
{
	struct xen_device_domain_owner *owner;

	spin_lock(&dev_domain_list_spinlock);
	owner = find_device(dev);
	if (!owner) {
		spin_unlock(&dev_domain_list_spinlock);
		return -ENODEV;
	}
	list_del(&owner->list);
	spin_unlock(&dev_domain_list_spinlock);
	kfree(owner);
	return 0;
}
EXPORT_SYMBOL_GPL(xen_unregister_device_domain_owner);
#endif
