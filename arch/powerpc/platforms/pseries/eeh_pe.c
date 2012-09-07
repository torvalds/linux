/*
 * The file intends to implement PE based on the information from
 * platforms. Basically, there have 3 types of PEs: PHB/Bus/Device.
 * All the PEs should be organized as hierarchy tree. The first level
 * of the tree will be associated to existing PHBs since the particular
 * PE is only meaningful in one PHB domain.
 *
 * Copyright Benjamin Herrenschmidt & Gavin Shan, IBM Corporation 2012.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>

#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>

static LIST_HEAD(eeh_phb_pe);

/**
 * eeh_pe_alloc - Allocate PE
 * @phb: PCI controller
 * @type: PE type
 *
 * Allocate PE instance dynamically.
 */
static struct eeh_pe *eeh_pe_alloc(struct pci_controller *phb, int type)
{
	struct eeh_pe *pe;

	/* Allocate PHB PE */
	pe = kzalloc(sizeof(struct eeh_pe), GFP_KERNEL);
	if (!pe) return NULL;

	/* Initialize PHB PE */
	pe->type = type;
	pe->phb = phb;
	INIT_LIST_HEAD(&pe->child_list);
	INIT_LIST_HEAD(&pe->child);
	INIT_LIST_HEAD(&pe->edevs);

	return pe;
}

/**
 * eeh_phb_pe_create - Create PHB PE
 * @phb: PCI controller
 *
 * The function should be called while the PHB is detected during
 * system boot or PCI hotplug in order to create PHB PE.
 */
int __devinit eeh_phb_pe_create(struct pci_controller *phb)
{
	struct eeh_pe *pe;

	/* Allocate PHB PE */
	pe = eeh_pe_alloc(phb, EEH_PE_PHB);
	if (!pe) {
		pr_err("%s: out of memory!\n", __func__);
		return -ENOMEM;
	}

	/* Put it into the list */
	eeh_lock();
	list_add_tail(&pe->child, &eeh_phb_pe);
	eeh_unlock();

	pr_debug("EEH: Add PE for PHB#%d\n", phb->global_number);

	return 0;
}

/**
 * eeh_phb_pe_get - Retrieve PHB PE based on the given PHB
 * @phb: PCI controller
 *
 * The overall PEs form hierarchy tree. The first layer of the
 * hierarchy tree is composed of PHB PEs. The function is used
 * to retrieve the corresponding PHB PE according to the given PHB.
 */
static struct eeh_pe *eeh_phb_pe_get(struct pci_controller *phb)
{
	struct eeh_pe *pe;

	eeh_lock();

	list_for_each_entry(pe, &eeh_phb_pe, child) {
		/*
		 * Actually, we needn't check the type since
		 * the PE for PHB has been determined when that
		 * was created.
		 */
		if (pe->type == EEH_PE_PHB &&
		    pe->phb == phb) {
			eeh_unlock();
			return pe;
		}
	}

	eeh_unlock();

	return NULL;
}
