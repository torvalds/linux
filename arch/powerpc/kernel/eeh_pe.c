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

#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>

#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>

static int eeh_pe_aux_size = 0;
static LIST_HEAD(eeh_phb_pe);

/**
 * eeh_set_pe_aux_size - Set PE auxillary data size
 * @size: PE auxillary data size
 *
 * Set PE auxillary data size
 */
void eeh_set_pe_aux_size(int size)
{
	if (size < 0)
		return;

	eeh_pe_aux_size = size;
}

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
	size_t alloc_size;

	alloc_size = sizeof(struct eeh_pe);
	if (eeh_pe_aux_size) {
		alloc_size = ALIGN(alloc_size, cache_line_size());
		alloc_size += eeh_pe_aux_size;
	}

	/* Allocate PHB PE */
	pe = kzalloc(alloc_size, GFP_KERNEL);
	if (!pe) return NULL;

	/* Initialize PHB PE */
	pe->type = type;
	pe->phb = phb;
	INIT_LIST_HEAD(&pe->child_list);
	INIT_LIST_HEAD(&pe->child);
	INIT_LIST_HEAD(&pe->edevs);

	pe->data = (void *)pe + ALIGN(sizeof(struct eeh_pe),
				      cache_line_size());
	return pe;
}

/**
 * eeh_phb_pe_create - Create PHB PE
 * @phb: PCI controller
 *
 * The function should be called while the PHB is detected during
 * system boot or PCI hotplug in order to create PHB PE.
 */
int eeh_phb_pe_create(struct pci_controller *phb)
{
	struct eeh_pe *pe;

	/* Allocate PHB PE */
	pe = eeh_pe_alloc(phb, EEH_PE_PHB);
	if (!pe) {
		pr_err("%s: out of memory!\n", __func__);
		return -ENOMEM;
	}

	/* Put it into the list */
	list_add_tail(&pe->child, &eeh_phb_pe);

	pr_debug("EEH: Add PE for PHB#%x\n", phb->global_number);

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
struct eeh_pe *eeh_phb_pe_get(struct pci_controller *phb)
{
	struct eeh_pe *pe;

	list_for_each_entry(pe, &eeh_phb_pe, child) {
		/*
		 * Actually, we needn't check the type since
		 * the PE for PHB has been determined when that
		 * was created.
		 */
		if ((pe->type & EEH_PE_PHB) && pe->phb == phb)
			return pe;
	}

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
void *eeh_pe_traverse(struct eeh_pe *root,
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
	struct eeh_dev *edev, *tmp;
	void *ret;

	if (!root) {
		pr_warn("%s: Invalid PE %p\n",
			__func__, root);
		return NULL;
	}

	/* Traverse root PE */
	for (pe = root; pe; pe = eeh_pe_next(pe, root)) {
		eeh_pe_for_each_dev(pe, edev, tmp) {
			ret = fn(edev, flag);
			if (ret)
				return ret;
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
struct eeh_pe_get_flag {
	int pe_no;
	int config_addr;
};

static void *__eeh_pe_get(void *data, void *flag)
{
	struct eeh_pe *pe = (struct eeh_pe *)data;
	struct eeh_pe_get_flag *tmp = (struct eeh_pe_get_flag *) flag;

	/* Unexpected PHB PE */
	if (pe->type & EEH_PE_PHB)
		return NULL;

	/*
	 * We prefer PE address. For most cases, we should
	 * have non-zero PE address
	 */
	if (eeh_has_flag(EEH_VALID_PE_ZERO)) {
		if (tmp->pe_no == pe->addr)
			return pe;
	} else {
		if (tmp->pe_no &&
		    (tmp->pe_no == pe->addr))
			return pe;
	}

	/* Try BDF address */
	if (tmp->config_addr &&
	   (tmp->config_addr == pe->config_addr))
		return pe;

	return NULL;
}

/**
 * eeh_pe_get - Search PE based on the given address
 * @phb: PCI controller
 * @pe_no: PE number
 * @config_addr: Config address
 *
 * Search the corresponding PE based on the specified address which
 * is included in the eeh device. The function is used to check if
 * the associated PE has been created against the PE address. It's
 * notable that the PE address has 2 format: traditional PE address
 * which is composed of PCI bus/device/function number, or unified
 * PE address.
 */
struct eeh_pe *eeh_pe_get(struct pci_controller *phb,
		int pe_no, int config_addr)
{
	struct eeh_pe *root = eeh_phb_pe_get(phb);
	struct eeh_pe_get_flag tmp = { pe_no, config_addr };
	struct eeh_pe *pe;

	pe = eeh_pe_traverse(root, __eeh_pe_get, &tmp);

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
	struct eeh_dev *parent;
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);

	/*
	 * It might have the case for the indirect parent
	 * EEH device already having associated PE, but
	 * the direct parent EEH device doesn't have yet.
	 */
	if (edev->physfn)
		pdn = pci_get_pdn(edev->physfn);
	else
		pdn = pdn ? pdn->parent : NULL;
	while (pdn) {
		/* We're poking out of PCI territory */
		parent = pdn_to_eeh_dev(pdn);
		if (!parent)
			return NULL;

		if (parent->pe)
			return parent->pe;

		pdn = pdn->parent;
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
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);
	int config_addr = (pdn->busno << 8) | (pdn->devfn);

	/* Check if the PE number is valid */
	if (!eeh_has_flag(EEH_VALID_PE_ZERO) && !edev->pe_config_addr) {
		pr_err("%s: Invalid PE#0 for edev 0x%x on PHB#%x\n",
		       __func__, config_addr, pdn->phb->global_number);
		return -EINVAL;
	}

	/*
	 * Search the PE has been existing or not according
	 * to the PE address. If that has been existing, the
	 * PE should be composed of PCI bus and its subordinate
	 * components.
	 */
	pe = eeh_pe_get(pdn->phb, edev->pe_config_addr, config_addr);
	if (pe && !(pe->type & EEH_PE_INVALID)) {
		/* Mark the PE as type of PCI bus */
		pe->type = EEH_PE_BUS;
		edev->pe = pe;

		/* Put the edev to PE */
		list_add_tail(&edev->list, &pe->edevs);
		pr_debug("EEH: Add %04x:%02x:%02x.%01x to Bus PE#%x\n",
			 pdn->phb->global_number,
			 pdn->busno,
			 PCI_SLOT(pdn->devfn),
			 PCI_FUNC(pdn->devfn),
			 pe->addr);
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
			parent->type &= ~(EEH_PE_INVALID | EEH_PE_KEEP);
			parent = parent->parent;
		}

		pr_debug("EEH: Add %04x:%02x:%02x.%01x to Device "
			 "PE#%x, Parent PE#%x\n",
			 pdn->phb->global_number,
			 pdn->busno,
			 PCI_SLOT(pdn->devfn),
			 PCI_FUNC(pdn->devfn),
			 pe->addr, pe->parent->addr);
		return 0;
	}

	/* Create a new EEH PE */
	if (edev->physfn)
		pe = eeh_pe_alloc(pdn->phb, EEH_PE_VF);
	else
		pe = eeh_pe_alloc(pdn->phb, EEH_PE_DEVICE);
	if (!pe) {
		pr_err("%s: out of memory!\n", __func__);
		return -ENOMEM;
	}
	pe->addr	= edev->pe_config_addr;
	pe->config_addr	= config_addr;

	/*
	 * Put the new EEH PE into hierarchy tree. If the parent
	 * can't be found, the newly created PE will be attached
	 * to PHB directly. Otherwise, we have to associate the
	 * PE with its parent.
	 */
	parent = eeh_pe_get_parent(edev);
	if (!parent) {
		parent = eeh_phb_pe_get(pdn->phb);
		if (!parent) {
			pr_err("%s: No PHB PE is found (PHB Domain=%d)\n",
				__func__, pdn->phb->global_number);
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
	pr_debug("EEH: Add %04x:%02x:%02x.%01x to "
		 "Device PE#%x, Parent PE#%x\n",
		 pdn->phb->global_number,
		 pdn->busno,
		 PCI_SLOT(pdn->devfn),
		 PCI_FUNC(pdn->devfn),
		 pe->addr, pe->parent->addr);

	return 0;
}

/**
 * eeh_rmv_from_parent_pe - Remove one EEH device from the associated PE
 * @edev: EEH device
 *
 * The PE hierarchy tree might be changed when doing PCI hotplug.
 * Also, the PCI devices or buses could be removed from the system
 * during EEH recovery. So we have to call the function remove the
 * corresponding PE accordingly if necessary.
 */
int eeh_rmv_from_parent_pe(struct eeh_dev *edev)
{
	struct eeh_pe *pe, *parent, *child;
	int cnt;
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);

	if (!edev->pe) {
		pr_debug("%s: No PE found for device %04x:%02x:%02x.%01x\n",
			 __func__,  pdn->phb->global_number,
			 pdn->busno,
			 PCI_SLOT(pdn->devfn),
			 PCI_FUNC(pdn->devfn));
		return -EEXIST;
	}

	/* Remove the EEH device */
	pe = eeh_dev_to_pe(edev);
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

		if (!(pe->state & EEH_PE_KEEP)) {
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
					if (!(child->type & EEH_PE_INVALID)) {
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
 * eeh_pe_update_time_stamp - Update PE's frozen time stamp
 * @pe: EEH PE
 *
 * We have time stamp for each PE to trace its time of getting
 * frozen in last hour. The function should be called to update
 * the time stamp on first error of the specific PE. On the other
 * handle, we needn't account for errors happened in last hour.
 */
void eeh_pe_update_time_stamp(struct eeh_pe *pe)
{
	struct timeval tstamp;

	if (!pe) return;

	if (pe->freeze_count <= 0) {
		pe->freeze_count = 0;
		do_gettimeofday(&pe->tstamp);
	} else {
		do_gettimeofday(&tstamp);
		if (tstamp.tv_sec - pe->tstamp.tv_sec > 3600) {
			pe->tstamp = tstamp;
			pe->freeze_count = 0;
		}
	}
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
	struct eeh_dev *edev, *tmp;
	struct pci_dev *pdev;

	/* Keep the state of permanently removed PE intact */
	if (pe->state & EEH_PE_REMOVED)
		return NULL;

	pe->state |= state;

	/* Offline PCI devices if applicable */
	if (!(state & EEH_PE_ISOLATED))
		return NULL;

	eeh_pe_for_each_dev(pe, edev, tmp) {
		pdev = eeh_dev_to_pci_dev(edev);
		if (pdev)
			pdev->error_state = pci_channel_io_frozen;
	}

	/* Block PCI config access if required */
	if (pe->state & EEH_PE_CFG_RESTRICTED)
		pe->state |= EEH_PE_CFG_BLOCKED;

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
EXPORT_SYMBOL_GPL(eeh_pe_state_mark);

static void *__eeh_pe_dev_mode_mark(void *data, void *flag)
{
	struct eeh_dev *edev = data;
	int mode = *((int *)flag);

	edev->mode |= mode;

	return NULL;
}

/**
 * eeh_pe_dev_state_mark - Mark state for all device under the PE
 * @pe: EEH PE
 *
 * Mark specific state for all child devices of the PE.
 */
void eeh_pe_dev_mode_mark(struct eeh_pe *pe, int mode)
{
	eeh_pe_dev_traverse(pe, __eeh_pe_dev_mode_mark, &mode);
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
	struct eeh_dev *edev, *tmp;
	struct pci_dev *pdev;

	/* Keep the state of permanently removed PE intact */
	if (pe->state & EEH_PE_REMOVED)
		return NULL;

	pe->state &= ~state;

	/*
	 * Special treatment on clearing isolated state. Clear
	 * check count since last isolation and put all affected
	 * devices to normal state.
	 */
	if (!(state & EEH_PE_ISOLATED))
		return NULL;

	pe->check_count = 0;
	eeh_pe_for_each_dev(pe, edev, tmp) {
		pdev = eeh_dev_to_pci_dev(edev);
		if (!pdev)
			continue;

		pdev->error_state = pci_channel_io_normal;
	}

	/* Unblock PCI config access if required */
	if (pe->state & EEH_PE_CFG_RESTRICTED)
		pe->state &= ~EEH_PE_CFG_BLOCKED;

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
 * eeh_pe_state_mark_with_cfg - Mark PE state with unblocked config space
 * @pe: PE
 * @state: PE state to be set
 *
 * Set specified flag to PE and its child PEs. The PCI config space
 * of some PEs is blocked automatically when EEH_PE_ISOLATED is set,
 * which isn't needed in some situations. The function allows to set
 * the specified flag to indicated PEs without blocking their PCI
 * config space.
 */
void eeh_pe_state_mark_with_cfg(struct eeh_pe *pe, int state)
{
	eeh_pe_traverse(pe, __eeh_pe_state_mark, &state);
	if (!(state & EEH_PE_ISOLATED))
		return;

	/* Clear EEH_PE_CFG_BLOCKED, which might be set just now */
	state = EEH_PE_CFG_BLOCKED;
	eeh_pe_traverse(pe, __eeh_pe_state_clear, &state);
}

/*
 * Some PCI bridges (e.g. PLX bridges) have primary/secondary
 * buses assigned explicitly by firmware, and we probably have
 * lost that after reset. So we have to delay the check until
 * the PCI-CFG registers have been restored for the parent
 * bridge.
 *
 * Don't use normal PCI-CFG accessors, which probably has been
 * blocked on normal path during the stage. So we need utilize
 * eeh operations, which is always permitted.
 */
static void eeh_bridge_check_link(struct eeh_dev *edev)
{
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);
	int cap;
	uint32_t val;
	int timeout = 0;

	/*
	 * We only check root port and downstream ports of
	 * PCIe switches
	 */
	if (!(edev->mode & (EEH_DEV_ROOT_PORT | EEH_DEV_DS_PORT)))
		return;

	pr_debug("%s: Check PCIe link for %04x:%02x:%02x.%01x ...\n",
		 __func__, pdn->phb->global_number,
		 pdn->busno,
		 PCI_SLOT(pdn->devfn),
		 PCI_FUNC(pdn->devfn));

	/* Check slot status */
	cap = edev->pcie_cap;
	eeh_ops->read_config(pdn, cap + PCI_EXP_SLTSTA, 2, &val);
	if (!(val & PCI_EXP_SLTSTA_PDS)) {
		pr_debug("  No card in the slot (0x%04x) !\n", val);
		return;
	}

	/* Check power status if we have the capability */
	eeh_ops->read_config(pdn, cap + PCI_EXP_SLTCAP, 2, &val);
	if (val & PCI_EXP_SLTCAP_PCP) {
		eeh_ops->read_config(pdn, cap + PCI_EXP_SLTCTL, 2, &val);
		if (val & PCI_EXP_SLTCTL_PCC) {
			pr_debug("  In power-off state, power it on ...\n");
			val &= ~(PCI_EXP_SLTCTL_PCC | PCI_EXP_SLTCTL_PIC);
			val |= (0x0100 & PCI_EXP_SLTCTL_PIC);
			eeh_ops->write_config(pdn, cap + PCI_EXP_SLTCTL, 2, val);
			msleep(2 * 1000);
		}
	}

	/* Enable link */
	eeh_ops->read_config(pdn, cap + PCI_EXP_LNKCTL, 2, &val);
	val &= ~PCI_EXP_LNKCTL_LD;
	eeh_ops->write_config(pdn, cap + PCI_EXP_LNKCTL, 2, val);

	/* Check link */
	eeh_ops->read_config(pdn, cap + PCI_EXP_LNKCAP, 4, &val);
	if (!(val & PCI_EXP_LNKCAP_DLLLARC)) {
		pr_debug("  No link reporting capability (0x%08x) \n", val);
		msleep(1000);
		return;
	}

	/* Wait the link is up until timeout (5s) */
	timeout = 0;
	while (timeout < 5000) {
		msleep(20);
		timeout += 20;

		eeh_ops->read_config(pdn, cap + PCI_EXP_LNKSTA, 2, &val);
		if (val & PCI_EXP_LNKSTA_DLLLA)
			break;
	}

	if (val & PCI_EXP_LNKSTA_DLLLA)
		pr_debug("  Link up (%s)\n",
			 (val & PCI_EXP_LNKSTA_CLS_2_5GB) ? "2.5GB" : "5GB");
	else
		pr_debug("  Link not ready (0x%04x)\n", val);
}

#define BYTE_SWAP(OFF)	(8*((OFF)/4)+3-(OFF))
#define SAVED_BYTE(OFF)	(((u8 *)(edev->config_space))[BYTE_SWAP(OFF)])

static void eeh_restore_bridge_bars(struct eeh_dev *edev)
{
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);
	int i;

	/*
	 * Device BARs: 0x10 - 0x18
	 * Bus numbers and windows: 0x18 - 0x30
	 */
	for (i = 4; i < 13; i++)
		eeh_ops->write_config(pdn, i*4, 4, edev->config_space[i]);
	/* Rom: 0x38 */
	eeh_ops->write_config(pdn, 14*4, 4, edev->config_space[14]);

	/* Cache line & Latency timer: 0xC 0xD */
	eeh_ops->write_config(pdn, PCI_CACHE_LINE_SIZE, 1,
                SAVED_BYTE(PCI_CACHE_LINE_SIZE));
        eeh_ops->write_config(pdn, PCI_LATENCY_TIMER, 1,
                SAVED_BYTE(PCI_LATENCY_TIMER));
	/* Max latency, min grant, interrupt ping and line: 0x3C */
	eeh_ops->write_config(pdn, 15*4, 4, edev->config_space[15]);

	/* PCI Command: 0x4 */
	eeh_ops->write_config(pdn, PCI_COMMAND, 4, edev->config_space[1] |
			      PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

	/* Check the PCIe link is ready */
	eeh_bridge_check_link(edev);
}

static void eeh_restore_device_bars(struct eeh_dev *edev)
{
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);
	int i;
	u32 cmd;

	for (i = 4; i < 10; i++)
		eeh_ops->write_config(pdn, i*4, 4, edev->config_space[i]);
	/* 12 == Expansion ROM Address */
	eeh_ops->write_config(pdn, 12*4, 4, edev->config_space[12]);

	eeh_ops->write_config(pdn, PCI_CACHE_LINE_SIZE, 1,
		SAVED_BYTE(PCI_CACHE_LINE_SIZE));
	eeh_ops->write_config(pdn, PCI_LATENCY_TIMER, 1,
		SAVED_BYTE(PCI_LATENCY_TIMER));

	/* max latency, min grant, interrupt pin and line */
	eeh_ops->write_config(pdn, 15*4, 4, edev->config_space[15]);

	/*
	 * Restore PERR & SERR bits, some devices require it,
	 * don't touch the other command bits
	 */
	eeh_ops->read_config(pdn, PCI_COMMAND, 4, &cmd);
	if (edev->config_space[1] & PCI_COMMAND_PARITY)
		cmd |= PCI_COMMAND_PARITY;
	else
		cmd &= ~PCI_COMMAND_PARITY;
	if (edev->config_space[1] & PCI_COMMAND_SERR)
		cmd |= PCI_COMMAND_SERR;
	else
		cmd &= ~PCI_COMMAND_SERR;
	eeh_ops->write_config(pdn, PCI_COMMAND, 4, cmd);
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
	struct eeh_dev *edev = (struct eeh_dev *)data;
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);

	/* Do special restore for bridges */
	if (edev->mode & EEH_DEV_BRIDGE)
		eeh_restore_bridge_bars(edev);
	else
		eeh_restore_device_bars(edev);

	if (eeh_ops->restore_config && pdn)
		eeh_ops->restore_config(pdn);

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
	/*
	 * We needn't take the EEH lock since eeh_pe_dev_traverse()
	 * will take that.
	 */
	eeh_pe_dev_traverse(pe, eeh_restore_one_device_bars, NULL);
}

/**
 * eeh_pe_loc_get - Retrieve location code binding to the given PE
 * @pe: EEH PE
 *
 * Retrieve the location code of the given PE. If the primary PE bus
 * is root bus, we will grab location code from PHB device tree node
 * or root port. Otherwise, the upstream bridge's device tree node
 * of the primary PE bus will be checked for the location code.
 */
const char *eeh_pe_loc_get(struct eeh_pe *pe)
{
	struct pci_bus *bus = eeh_pe_bus_get(pe);
	struct device_node *dn;
	const char *loc = NULL;

	while (bus) {
		dn = pci_bus_to_OF_node(bus);
		if (!dn) {
			bus = bus->parent;
			continue;
		}

		if (pci_is_root_bus(bus))
			loc = of_get_property(dn, "ibm,io-base-loc-code", NULL);
		else
			loc = of_get_property(dn, "ibm,slot-location-code",
					      NULL);

		if (loc)
			return loc;

		bus = bus->parent;
	}

	return "N/A";
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
	struct eeh_dev *edev;
	struct pci_dev *pdev;

	if (pe->type & EEH_PE_PHB)
		return pe->phb->bus;

	/* The primary bus might be cached during probe time */
	if (pe->state & EEH_PE_PRI_BUS)
		return pe->bus;

	/* Retrieve the parent PCI bus of first (top) PCI device */
	edev = list_first_entry_or_null(&pe->edevs, struct eeh_dev, list);
	pdev = eeh_dev_to_pci_dev(edev);
	if (pdev)
		return pdev->bus;

	return NULL;
}
