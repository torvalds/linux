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
#include <xen/xen.h>
#include <xen/interface/physdev.h>
#include <xen/interface/xen.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include "../pci/pci.h"

static int xen_add_device(struct device *dev)
{
	int r;
	struct pci_dev *pci_dev = to_pci_dev(dev);

#ifdef CONFIG_PCI_IOV
	if (pci_dev->is_virtfn) {
		struct physdev_manage_pci_ext manage_pci_ext = {
			.bus		= pci_dev->bus->number,
			.devfn		= pci_dev->devfn,
			.is_virtfn 	= 1,
			.physfn.bus	= pci_dev->physfn->bus->number,
			.physfn.devfn	= pci_dev->physfn->devfn,
		};

		r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_add_ext,
			&manage_pci_ext);
	} else
#endif
	if (pci_ari_enabled(pci_dev->bus) && PCI_SLOT(pci_dev->devfn)) {
		struct physdev_manage_pci_ext manage_pci_ext = {
			.bus		= pci_dev->bus->number,
			.devfn		= pci_dev->devfn,
			.is_extfn	= 1,
		};

		r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_add_ext,
			&manage_pci_ext);
	} else {
		struct physdev_manage_pci manage_pci = {
			.bus 	= pci_dev->bus->number,
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
	struct physdev_manage_pci manage_pci;

	manage_pci.bus = pci_dev->bus->number;
	manage_pci.devfn = pci_dev->devfn;

	r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_remove,
		&manage_pci);

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
		break;
	}

	return r;
}

struct notifier_block device_nb = {
	.notifier_call = xen_pci_notifier,
};

static int __init register_xen_pci_notifier(void)
{
	if (!xen_initial_domain())
		return 0;

	return bus_register_notifier(&pci_bus_type, &device_nb);
}

arch_initcall(register_xen_pci_notifier);
