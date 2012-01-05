/*
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Author: Weidong Han <weidong.han@intel.com>
 */

#include <linux/pci.h>
#include <linux/acpi.h>
#include <xen/xen.h>
#include <xen/interface/physdev.h>
#include <xen/interface/xen.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include "../pci/pci.h"

static bool __read_mostly pci_seg_supported = true;

static int xen_add_device(struct device *dev)
{
	int r;
	struct pci_dev *pci_dev = to_pci_dev(dev);
#ifdef CONFIG_PCI_IOV
	struct pci_dev *physfn = pci_dev->physfn;
#endif

	if (pci_seg_supported) {
		struct physdev_pci_device_add add = {
			.seg = pci_domain_nr(pci_dev->bus),
			.bus = pci_dev->bus->number,
			.devfn = pci_dev->devfn
		};
#ifdef CONFIG_ACPI
		acpi_handle handle;
#endif

#ifdef CONFIG_PCI_IOV
		if (pci_dev->is_virtfn) {
			add.flags = XEN_PCI_DEV_VIRTFN;
			add.physfn.bus = physfn->bus->number;
			add.physfn.devfn = physfn->devfn;
		} else
#endif
		if (pci_ari_enabled(pci_dev->bus) && PCI_SLOT(pci_dev->devfn))
			add.flags = XEN_PCI_DEV_EXTFN;

#ifdef CONFIG_ACPI
		handle = DEVICE_ACPI_HANDLE(&pci_dev->dev);
		if (!handle)
			handle = DEVICE_ACPI_HANDLE(pci_dev->bus->bridge);
#ifdef CONFIG_PCI_IOV
		if (!handle && pci_dev->is_virtfn)
			handle = DEVICE_ACPI_HANDLE(physfn->bus->bridge);
#endif
		if (handle) {
			acpi_status status;

			do {
				unsigned long long pxm;

				status = acpi_evaluate_integer(handle, "_PXM",
							       NULL, &pxm);
				if (ACPI_SUCCESS(status)) {
					add.optarr[0] = pxm;
					add.flags |= XEN_PCI_DEV_PXM;
					break;
				}
				status = acpi_get_parent(handle, &handle);
			} while (ACPI_SUCCESS(status));
		}
#endif /* CONFIG_ACPI */

		r = HYPERVISOR_physdev_op(PHYSDEVOP_pci_device_add, &add);
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
