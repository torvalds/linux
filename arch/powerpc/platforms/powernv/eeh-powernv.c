/*
 * The file intends to implement the platform dependent EEH operations on
 * powernv platform. Actually, the powernv was created in order to fully
 * hypervisor support.
 *
 * Copyright Benjamin Herrenschmidt & Gavin Shan, IBM Corporation 2013.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <asm/eeh.h>
#include <asm/eeh_event.h>
#include <asm/firmware.h>
#include <asm/io.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/msi_bitmap.h>
#include <asm/opal.h>
#include <asm/ppc-pci.h>

#include "powernv.h"
#include "pci.h"

/**
 * powernv_eeh_init - EEH platform dependent initialization
 *
 * EEH platform dependent initialization on powernv
 */
static int powernv_eeh_init(void)
{
	struct pci_controller *hose;
	struct pnv_phb *phb;

	/* We require OPALv3 */
	if (!firmware_has_feature(FW_FEATURE_OPALv3)) {
		pr_warning("%s: OPALv3 is required !\n", __func__);
		return -EINVAL;
	}

	/* Set probe mode */
	eeh_add_flag(EEH_PROBE_MODE_DEV);

	/*
	 * P7IOC blocks PCI config access to frozen PE, but PHB3
	 * doesn't do that. So we have to selectively enable I/O
	 * prior to collecting error log.
	 */
	list_for_each_entry(hose, &hose_list, list_node) {
		phb = hose->private_data;

		if (phb->model == PNV_PHB_MODEL_P7IOC)
			eeh_add_flag(EEH_ENABLE_IO_FOR_LOG);
		break;
	}

	return 0;
}

/**
 * powernv_eeh_post_init - EEH platform dependent post initialization
 *
 * EEH platform dependent post initialization on powernv. When
 * the function is called, the EEH PEs and devices should have
 * been built. If the I/O cache staff has been built, EEH is
 * ready to supply service.
 */
static int powernv_eeh_post_init(void)
{
	struct pci_controller *hose;
	struct pnv_phb *phb;
	int ret = 0;

	list_for_each_entry(hose, &hose_list, list_node) {
		phb = hose->private_data;

		if (phb->eeh_ops && phb->eeh_ops->post_init) {
			ret = phb->eeh_ops->post_init(hose);
			if (ret)
				break;
		}
	}

	return ret;
}

/**
 * powernv_eeh_dev_probe - Do probe on PCI device
 * @dev: PCI device
 * @flag: unused
 *
 * When EEH module is installed during system boot, all PCI devices
 * are checked one by one to see if it supports EEH. The function
 * is introduced for the purpose. By default, EEH has been enabled
 * on all PCI devices. That's to say, we only need do necessary
 * initialization on the corresponding eeh device and create PE
 * accordingly.
 *
 * It's notable that's unsafe to retrieve the EEH device through
 * the corresponding PCI device. During the PCI device hotplug, which
 * was possiblly triggered by EEH core, the binding between EEH device
 * and the PCI device isn't built yet.
 */
static int powernv_eeh_dev_probe(struct pci_dev *dev, void *flag)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct device_node *dn = pci_device_to_OF_node(dev);
	struct eeh_dev *edev = of_node_to_eeh_dev(dn);
	int ret;

	/*
	 * When probing the root bridge, which doesn't have any
	 * subordinate PCI devices. We don't have OF node for
	 * the root bridge. So it's not reasonable to continue
	 * the probing.
	 */
	if (!dn || !edev || edev->pe)
		return 0;

	/* Skip for PCI-ISA bridge */
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_ISA)
		return 0;

	/* Initialize eeh device */
	edev->class_code = dev->class;
	edev->mode	&= 0xFFFFFF00;
	if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE)
		edev->mode |= EEH_DEV_BRIDGE;
	edev->pcix_cap = pci_find_capability(dev, PCI_CAP_ID_PCIX);
	if (pci_is_pcie(dev)) {
		edev->pcie_cap = pci_pcie_cap(dev);

		if (pci_pcie_type(dev) == PCI_EXP_TYPE_ROOT_PORT)
			edev->mode |= EEH_DEV_ROOT_PORT;
		else if (pci_pcie_type(dev) == PCI_EXP_TYPE_DOWNSTREAM)
			edev->mode |= EEH_DEV_DS_PORT;

		edev->aer_cap = pci_find_ext_capability(dev,
							PCI_EXT_CAP_ID_ERR);
	}

	edev->config_addr	= ((dev->bus->number << 8) | dev->devfn);
	edev->pe_config_addr	= phb->bdfn_to_pe(phb, dev->bus, dev->devfn & 0xff);

	/* Create PE */
	ret = eeh_add_to_parent_pe(edev);
	if (ret) {
		pr_warn("%s: Can't add PCI dev %s to parent PE (%d)\n",
			__func__, pci_name(dev), ret);
		return ret;
	}

	/*
	 * Cache the PE primary bus, which can't be fetched when
	 * full hotplug is in progress. In that case, all child
	 * PCI devices of the PE are expected to be removed prior
	 * to PE reset.
	 */
	if (!edev->pe->bus)
		edev->pe->bus = dev->bus;

	/*
	 * Enable EEH explicitly so that we will do EEH check
	 * while accessing I/O stuff
	 */
	eeh_add_flag(EEH_ENABLED);

	/* Save memory bars */
	eeh_save_bars(edev);

	return 0;
}

/**
 * powernv_eeh_set_option - Initialize EEH or MMIO/DMA reenable
 * @pe: EEH PE
 * @option: operation to be issued
 *
 * The function is used to control the EEH functionality globally.
 * Currently, following options are support according to PAPR:
 * Enable EEH, Disable EEH, Enable MMIO and Enable DMA
 */
static int powernv_eeh_set_option(struct eeh_pe *pe, int option)
{
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;
	int ret = -EEXIST;

	/*
	 * What we need do is pass it down for hardware
	 * implementation to handle it.
	 */
	if (phb->eeh_ops && phb->eeh_ops->set_option)
		ret = phb->eeh_ops->set_option(pe, option);

	return ret;
}

/**
 * powernv_eeh_get_pe_addr - Retrieve PE address
 * @pe: EEH PE
 *
 * Retrieve the PE address according to the given tranditional
 * PCI BDF (Bus/Device/Function) address.
 */
static int powernv_eeh_get_pe_addr(struct eeh_pe *pe)
{
	return pe->addr;
}

/**
 * powernv_eeh_get_state - Retrieve PE state
 * @pe: EEH PE
 * @delay: delay while PE state is temporarily unavailable
 *
 * Retrieve the state of the specified PE. For IODA-compitable
 * platform, it should be retrieved from IODA table. Therefore,
 * we prefer passing down to hardware implementation to handle
 * it.
 */
static int powernv_eeh_get_state(struct eeh_pe *pe, int *delay)
{
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;
	int ret = EEH_STATE_NOT_SUPPORT;

	if (phb->eeh_ops && phb->eeh_ops->get_state) {
		ret = phb->eeh_ops->get_state(pe);

		/*
		 * If the PE state is temporarily unavailable,
		 * to inform the EEH core delay for default
		 * period (1 second)
		 */
		if (delay) {
			*delay = 0;
			if (ret & EEH_STATE_UNAVAILABLE)
				*delay = 1000;
		}
	}

	return ret;
}

/**
 * powernv_eeh_reset - Reset the specified PE
 * @pe: EEH PE
 * @option: reset option
 *
 * Reset the specified PE
 */
static int powernv_eeh_reset(struct eeh_pe *pe, int option)
{
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;
	int ret = -EEXIST;

	if (phb->eeh_ops && phb->eeh_ops->reset)
		ret = phb->eeh_ops->reset(pe, option);

	return ret;
}

/**
 * powernv_eeh_wait_state - Wait for PE state
 * @pe: EEH PE
 * @max_wait: maximal period in microsecond
 *
 * Wait for the state of associated PE. It might take some time
 * to retrieve the PE's state.
 */
static int powernv_eeh_wait_state(struct eeh_pe *pe, int max_wait)
{
	int ret;
	int mwait;

	while (1) {
		ret = powernv_eeh_get_state(pe, &mwait);

		/*
		 * If the PE's state is temporarily unavailable,
		 * we have to wait for the specified time. Otherwise,
		 * the PE's state will be returned immediately.
		 */
		if (ret != EEH_STATE_UNAVAILABLE)
			return ret;

		max_wait -= mwait;
		if (max_wait <= 0) {
			pr_warning("%s: Timeout getting PE#%x's state (%d)\n",
				   __func__, pe->addr, max_wait);
			return EEH_STATE_NOT_SUPPORT;
		}

		msleep(mwait);
	}

	return EEH_STATE_NOT_SUPPORT;
}

/**
 * powernv_eeh_get_log - Retrieve error log
 * @pe: EEH PE
 * @severity: temporary or permanent error log
 * @drv_log: driver log to be combined with retrieved error log
 * @len: length of driver log
 *
 * Retrieve the temporary or permanent error from the PE.
 */
static int powernv_eeh_get_log(struct eeh_pe *pe, int severity,
			char *drv_log, unsigned long len)
{
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;
	int ret = -EEXIST;

	if (phb->eeh_ops && phb->eeh_ops->get_log)
		ret = phb->eeh_ops->get_log(pe, severity, drv_log, len);

	return ret;
}

/**
 * powernv_eeh_configure_bridge - Configure PCI bridges in the indicated PE
 * @pe: EEH PE
 *
 * The function will be called to reconfigure the bridges included
 * in the specified PE so that the mulfunctional PE would be recovered
 * again.
 */
static int powernv_eeh_configure_bridge(struct eeh_pe *pe)
{
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;
	int ret = 0;

	if (phb->eeh_ops && phb->eeh_ops->configure_bridge)
		ret = phb->eeh_ops->configure_bridge(pe);

	return ret;
}

/**
 * powernv_eeh_next_error - Retrieve next EEH error to handle
 * @pe: Affected PE
 *
 * Using OPAL API, to retrieve next EEH error for EEH core to handle
 */
static int powernv_eeh_next_error(struct eeh_pe **pe)
{
	struct pci_controller *hose;
	struct pnv_phb *phb = NULL;

	list_for_each_entry(hose, &hose_list, list_node) {
		phb = hose->private_data;
		break;
	}

	if (phb && phb->eeh_ops->next_error)
		return phb->eeh_ops->next_error(pe);

	return -EEXIST;
}

static int powernv_eeh_restore_config(struct device_node *dn)
{
	struct eeh_dev *edev = of_node_to_eeh_dev(dn);
	struct pnv_phb *phb;
	s64 ret;

	if (!edev)
		return -EEXIST;

	phb = edev->phb->private_data;
	ret = opal_pci_reinit(phb->opal_id,
			      OPAL_REINIT_PCI_DEV, edev->config_addr);
	if (ret) {
		pr_warn("%s: Can't reinit PCI dev 0x%x (%lld)\n",
			__func__, edev->config_addr, ret);
		return -EIO;
	}

	return 0;
}

static struct eeh_ops powernv_eeh_ops = {
	.name                   = "powernv",
	.init                   = powernv_eeh_init,
	.post_init              = powernv_eeh_post_init,
	.of_probe               = NULL,
	.dev_probe              = powernv_eeh_dev_probe,
	.set_option             = powernv_eeh_set_option,
	.get_pe_addr            = powernv_eeh_get_pe_addr,
	.get_state              = powernv_eeh_get_state,
	.reset                  = powernv_eeh_reset,
	.wait_state             = powernv_eeh_wait_state,
	.get_log                = powernv_eeh_get_log,
	.configure_bridge       = powernv_eeh_configure_bridge,
	.read_config            = pnv_pci_cfg_read,
	.write_config           = pnv_pci_cfg_write,
	.next_error		= powernv_eeh_next_error,
	.restore_config		= powernv_eeh_restore_config
};

/**
 * eeh_powernv_init - Register platform dependent EEH operations
 *
 * EEH initialization on powernv platform. This function should be
 * called before any EEH related functions.
 */
static int __init eeh_powernv_init(void)
{
	int ret = -EINVAL;

	ret = eeh_ops_register(&powernv_eeh_ops);
	if (!ret)
		pr_info("EEH: PowerNV platform initialized\n");
	else
		pr_info("EEH: Failed to initialize PowerNV platform (%d)\n", ret);

	return ret;
}
machine_early_initcall(powernv, eeh_powernv_init);
