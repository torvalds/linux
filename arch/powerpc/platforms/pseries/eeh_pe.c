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
		if ((pe->type & EEH_PE_PHB) &&
		    pe->phb == phb) {
			eeh_unlock();
			return pe;
		}
	}

	eeh_unlock();

	return NULL;
}

/**
 * eeh_pe_next - Retrieve the next PE in the tree
 * @pe: current PE
 * @root: root PE
 *
 * The function is used to retrieve the next PE in the
 * hierarchy PE tree.
 */
static struct eeh_pe *eeh_pe_next(struct eeh_pe *pe,
				  struct eeh_pe *root)
{
	struct list_head *next = pe->child_list.next;

	if (next == &pe->child_list) {
		while (1) {
			if (pe == root)
				return NULL;
			next = pe->child.next;
			if (next != &pe->parent->child_list)
				break;
			pe = pe->parent;
		}
	}

	return list_entry(next, struct eeh_pe, child);
}

/**
 * eeh_pe_traverse - Traverse PEs in the specified PHB
 * @root: root PE
 * @fn: callback
 * @flag: extra parameter to callback
 *
 * The function is used to traverse the specified PE and its
 * child PEs. The traversing is to be terminated once the
 * callback returns something other than NULL, or no more PEs
 * to be traversed.
 */
static void *eeh_pe_traverse(struct eeh_pe *root,
			eeh_traverse_func fn, void *flag)
{
	struct eeh_pe *pe;
	void *ret;

	for (pe = root; pe; pe = eeh_pe_next(pe, root)) {
		ret = fn(pe, flag);
		if (ret) return ret;
	}

	return NULL;
}

/**
 * eeh_pe_dev_traverse - Traverse the devices from the PE
 * @root: EEH PE
 * @fn: function callback
 * @flag: extra parameter to callback
 *
 * The function is used to traverse the devices of the specified
 * PE and its child PEs.
 */
void *eeh_pe_dev_traverse(struct eeh_pe *root,
		eeh_traverse_func fn, void *flag)
{
	struct eeh_pe *pe;
	struct eeh_dev *edev;
	void *ret;

	if (!root) {
		pr_warning("%s: Invalid PE %p\n", __func__, root);
		return NULL;
	}

	/* Traverse root PE */
	for (pe = root; pe; pe = eeh_pe_next(pe, root)) {
		eeh_pe_for_each_dev(pe, edev) {
			ret = fn(edev, flag);
			if (ret) return ret;
		}
	}

	return NULL;
}

/**
 * __eeh_pe_get - Check the PE address
 * @data: EEH PE
 * @flag: EEH device
 *
 * For one particular PE, it can be identified by PE address
 * or tranditional BDF address. BDF address is composed of
 * Bus/Device/Function number. The extra data referred by flag
 * indicates which type of address should be used.
 */
static void *__eeh_pe_get(void *data, void *flag)
{
	struct eeh_pe *pe = (struct eeh_pe *)data;
	struct eeh_dev *edev = (struct eeh_dev *)flag;

	/* Unexpected PHB PE */
	if (pe->type & EEH_PE_PHB)
		return NULL;

	/* We prefer PE address */
	if (edev->pe_config_addr &&
	   (edev->pe_config_addr == pe->addr))
		return pe;

	/* Try BDF address */
	if (edev->pe_config_addr &&
	   (edev->config_addr == pe->config_addr))
		return pe;

	return NULL;
}

/**
 * eeh_pe_get - Search PE based on the given address
 * @edev: EEH device
 *
 * Search the corresponding PE based on the specified address which
 * is included in the eeh device. The function is used to check if
 * the associated PE has been created against the PE address. It's
 * notable that the PE address has 2 format: traditional PE address
 * which is composed of PCI bus/device/function number, or unified
 * PE address.
 */
static struct eeh_pe *eeh_pe_get(struct eeh_dev *edev)
{
	struct eeh_pe *root = eeh_phb_pe_get(edev->phb);
	struct eeh_pe *pe;

	eeh_lock();
	pe = eeh_pe_traverse(root, __eeh_pe_get, edev);
	eeh_unlock();

	return pe;
}

/**
 * eeh_pe_get_parent - Retrieve the parent PE
 * @edev: EEH device
 *
 * The whole PEs existing in the system are organized as hierarchy
 * tree. The function is used to retrieve the parent PE according
 * to the parent EEH device.
 */
static struct eeh_pe *eeh_pe_get_parent(struct eeh_dev *edev)
{
	struct device_node *dn;
	struct eeh_dev *parent;

	/*
	 * It might have the case for the indirect parent
	 * EEH device already having associated PE, but
	 * the direct parent EEH device doesn't have yet.
	 */
	dn = edev->dn->parent;
	while (dn) {
		/* We're poking out of PCI territory */
		if (!PCI_DN(dn)) return NULL;

		parent = of_node_to_eeh_dev(dn);
		/* We're poking out of PCI territory */
		if (!parent) return NULL;

		if (parent->pe)
			return parent->pe;

		dn = dn->parent;
	}

	return NULL;
}

/**
 * eeh_add_to_parent_pe - Add EEH device to parent PE
 * @edev: EEH device
 *
 * Add EEH device to the parent PE. If the parent PE already
 * exists, the PE type will be changed to EEH_PE_BUS. Otherwise,
 * we have to create new PE to hold the EEH device and the new
 * PE will be linked to its parent PE as well.
 */
int eeh_add_to_parent_pe(struct eeh_dev *edev)
{
	struct eeh_pe *pe, *parent;

	/*
	 * Search the PE has been existing or not according
	 * to the PE address. If that has been existing, the
	 * PE should be composed of PCI bus and its subordinate
	 * components.
	 */
	pe = eeh_pe_get(edev);
	if (pe && !(pe->type & EEH_PE_INVALID)) {
		if (!edev->pe_config_addr) {
			pr_err("%s: PE with addr 0x%x already exists\n",
				__func__, edev->config_addr);
			return -EEXIST;
		}

		/* Mark the PE as type of PCI bus */
		pe->type = EEH_PE_BUS;
		edev->pe = pe;

		/* Put the edev to PE */
		list_add_tail(&edev->list, &pe->edevs);
		pr_debug("EEH: Add %s to Bus PE#%x\n",
			edev->dn->full_name, pe->addr);

		return 0;
	} else if (pe && (pe->type & EEH_PE_INVALID)) {
		list_add_tail(&edev->list, &pe->edevs);
		edev->pe = pe;
		/*
		 * We're running to here because of PCI hotplug caused by
		 * EEH recovery. We need clear EEH_PE_INVALID until the top.
		 */
		parent = pe;
		while (parent) {
			if (!(parent->type & EEH_PE_INVALID))
				break;
			parent->type &= ~EEH_PE_INVALID;
			parent = parent->parent;
		}
		pr_debug("EEH: Add %s to Device PE#%x, Parent PE#%x\n",
			edev->dn->full_name, pe->addr, pe->parent->addr);

		return 0;
	}

	/* Create a new EEH PE */
	pe = eeh_pe_alloc(edev->phb, EEH_PE_DEVICE);
	if (!pe) {
		pr_err("%s: out of memory!\n", __func__);
		return -ENOMEM;
	}
	pe->addr	= edev->pe_config_addr;
	pe->config_addr	= edev->config_addr;

	/*
	 * Put the new EEH PE into hierarchy tree. If the parent
	 * can't be found, the newly created PE will be attached
	 * to PHB directly. Otherwise, we have to associate the
	 * PE with its parent.
	 */
	parent = eeh_pe_get_parent(edev);
	if (!parent) {
		parent = eeh_phb_pe_get(edev->phb);
		if (!parent) {
			pr_err("%s: No PHB PE is found (PHB Domain=%d)\n",
				__func__, edev->phb->global_number);
			edev->pe = NULL;
			kfree(pe);
			return -EEXIST;
		}
	}
	pe->parent = parent;

	/*
	 * Put the newly created PE into the child list and
	 * link the EEH device accordingly.
	 */
	list_add_tail(&pe->child, &parent->child_list);
	list_add_tail(&edev->list, &pe->edevs);
	edev->pe = pe;
	pr_debug("EEH: Add %s to Device PE#%x, Parent PE#%x\n",
		edev->dn->full_name, pe->addr, pe->parent->addr);

	return 0;
}

/**
 * eeh_rmv_from_parent_pe - Remove one EEH device from the associated PE
 * @edev: EEH device
 * @purge_pe: remove PE or not
 *
 * The PE hierarchy tree might be changed when doing PCI hotplug.
 * Also, the PCI devices or buses could be removed from the system
 * during EEH recovery. So we have to call the function remove the
 * corresponding PE accordingly if necessary.
 */
int eeh_rmv_from_parent_pe(struct eeh_dev *edev, int purge_pe)
{
	struct eeh_pe *pe, *parent, *child;
	int cnt;

	if (!edev->pe) {
		pr_warning("%s: No PE found for EEH device %s\n",
			__func__, edev->dn->full_name);
		return -EEXIST;
	}

	/* Remove the EEH device */
	pe = edev->pe;
	edev->pe = NULL;
	list_del(&edev->list);

	/*
	 * Check if the parent PE includes any EEH devices.
	 * If not, we should delete that. Also, we should
	 * delete the parent PE if it doesn't have associated
	 * child PEs and EEH devices.
	 */
	while (1) {
		parent = pe->parent;
		if (pe->type & EEH_PE_PHB)
			break;

		if (purge_pe) {
			if (list_empty(&pe->edevs) &&
			    list_empty(&pe->child_list)) {
				list_del(&pe->child);
				kfree(pe);
			} else {
				break;
			}
		} else {
			if (list_empty(&pe->edevs)) {
				cnt = 0;
				list_for_each_entry(child, &pe->child_list, child) {
					if (!(pe->type & EEH_PE_INVALID)) {
						cnt++;
						break;
					}
				}

				if (!cnt)
					pe->type |= EEH_PE_INVALID;
				else
					break;
			}
		}

		pe = parent;
	}

	return 0;
}

/**
 * __eeh_pe_state_mark - Mark the state for the PE
 * @data: EEH PE
 * @flag: state
 *
 * The function is used to mark the indicated state for the given
 * PE. Also, the associated PCI devices will be put into IO frozen
 * state as well.
 */
static void *__eeh_pe_state_mark(void *data, void *flag)
{
	struct eeh_pe *pe = (struct eeh_pe *)data;
	int state = *((int *)flag);
	struct eeh_dev *tmp;
	struct pci_dev *pdev;

	/*
	 * Mark the PE with the indicated state. Also,
	 * the associated PCI device will be put into
	 * I/O frozen state to avoid I/O accesses from
	 * the PCI device driver.
	 */
	pe->state |= state;
	eeh_pe_for_each_dev(pe, tmp) {
		pdev = eeh_dev_to_pci_dev(tmp);
		if (pdev)
			pdev->error_state = pci_channel_io_frozen;
	}

	return NULL;
}

/**
 * eeh_pe_state_mark - Mark specified state for PE and its associated device
 * @pe: EEH PE
 *
 * EEH error affects the current PE and its child PEs. The function
 * is used to mark appropriate state for the affected PEs and the
 * associated devices.
 */
void eeh_pe_state_mark(struct eeh_pe *pe, int state)
{
	eeh_pe_traverse(pe, __eeh_pe_state_mark, &state);
}

/**
 * __eeh_pe_state_clear - Clear state for the PE
 * @data: EEH PE
 * @flag: state
 *
 * The function is used to clear the indicated state from the
 * given PE. Besides, we also clear the check count of the PE
 * as well.
 */
static void *__eeh_pe_state_clear(void *data, void *flag)
{
	struct eeh_pe *pe = (struct eeh_pe *)data;
	int state = *((int *)flag);

	pe->state &= ~state;
	pe->check_count = 0;

	return NULL;
}

/**
 * eeh_pe_state_clear - Clear state for the PE and its children
 * @pe: PE
 * @state: state to be cleared
 *
 * When the PE and its children has been recovered from error,
 * we need clear the error state for that. The function is used
 * for the purpose.
 */
void eeh_pe_state_clear(struct eeh_pe *pe, int state)
{
	eeh_pe_traverse(pe, __eeh_pe_state_clear, &state);
}

/**
 * eeh_restore_one_device_bars - Restore the Base Address Registers for one device
 * @data: EEH device
 * @flag: Unused
 *
 * Loads the PCI configuration space base address registers,
 * the expansion ROM base address, the latency timer, and etc.
 * from the saved values in the device node.
 */
static void *eeh_restore_one_device_bars(void *data, void *flag)
{
	int i;
	u32 cmd;
	struct eeh_dev *edev = (struct eeh_dev *)data;
	struct device_node *dn = eeh_dev_to_of_node(edev);

	for (i = 4; i < 10; i++)
		eeh_ops->write_config(dn, i*4, 4, edev->config_space[i]);
	/* 12 == Expansion ROM Address */
	eeh_ops->write_config(dn, 12*4, 4, edev->config_space[12]);

#define BYTE_SWAP(OFF) (8*((OFF)/4)+3-(OFF))
#define SAVED_BYTE(OFF) (((u8 *)(edev->config_space))[BYTE_SWAP(OFF)])

	eeh_ops->write_config(dn, PCI_CACHE_LINE_SIZE, 1,
		SAVED_BYTE(PCI_CACHE_LINE_SIZE));
	eeh_ops->write_config(dn, PCI_LATENCY_TIMER, 1,
		SAVED_BYTE(PCI_LATENCY_TIMER));

	/* max latency, min grant, interrupt pin and line */
	eeh_ops->write_config(dn, 15*4, 4, edev->config_space[15]);

	/*
	 * Restore PERR & SERR bits, some devices require it,
	 * don't touch the other command bits
	 */
	eeh_ops->read_config(dn, PCI_COMMAND, 4, &cmd);
	if (edev->config_space[1] & PCI_COMMAND_PARITY)
		cmd |= PCI_COMMAND_PARITY;
	else
		cmd &= ~PCI_COMMAND_PARITY;
	if (edev->config_space[1] & PCI_COMMAND_SERR)
		cmd |= PCI_COMMAND_SERR;
	else
		cmd &= ~PCI_COMMAND_SERR;
	eeh_ops->write_config(dn, PCI_COMMAND, 4, cmd);

	return NULL;
}

/**
 * eeh_pe_restore_bars - Restore the PCI config space info
 * @pe: EEH PE
 *
 * This routine performs a recursive walk to the children
 * of this device as well.
 */
void eeh_pe_restore_bars(struct eeh_pe *pe)
{
	eeh_pe_dev_traverse(pe, eeh_restore_one_device_bars, NULL);
}

/**
 * eeh_pe_bus_get - Retrieve PCI bus according to the given PE
 * @pe: EEH PE
 *
 * Retrieve the PCI bus according to the given PE. Basically,
 * there're 3 types of PEs: PHB/Bus/Device. For PHB PE, the
 * primary PCI bus will be retrieved. The parent bus will be
 * returned for BUS PE. However, we don't have associated PCI
 * bus for DEVICE PE.
 */
struct pci_bus *eeh_pe_bus_get(struct eeh_pe *pe)
{
	struct pci_bus *bus = NULL;
	struct eeh_dev *edev;
	struct pci_dev *pdev;

	if (pe->type & EEH_PE_PHB) {
		bus = pe->phb->bus;
	} else if (pe->type & EEH_PE_BUS) {
		edev = list_first_entry(&pe->edevs, struct eeh_dev, list);
		pdev = eeh_dev_to_pci_dev(edev);
		if (pdev)
			bus = pdev->bus;
	}

	return bus;
}
