/*
 * The file intends to implement the functions needed by EEH, which is
 * built on IODA compliant chip. Actually, lots of functions related
 * to EEH would be built based on the OPAL APIs.
 *
 * Copyright Benjamin Herrenschmidt & Gavin Shan, IBM Corporation 2013.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/notifier.h>
#include <linux/pci.h>
#include <linux/string.h>

#include <asm/eeh.h>
#include <asm/eeh_event.h>
#include <asm/io.h>
#include <asm/iommu.h>
#include <asm/msi_bitmap.h>
#include <asm/opal.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <asm/tce.h>

#include "powernv.h"
#include "pci.h"

static s64 ioda_eeh_phb_poll(struct pnv_phb *phb)
{
	s64 rc = OPAL_HARDWARE;

	while (1) {
		rc = opal_pci_poll(phb->opal_id);
		if (rc <= 0)
			break;

		if (system_state < SYSTEM_RUNNING)
			udelay(1000 * rc);
		else
			msleep(rc);
	}

	return rc;
}

int ioda_eeh_phb_reset(struct pci_controller *hose, int option)
{
	struct pnv_phb *phb = hose->private_data;
	s64 rc = OPAL_HARDWARE;

	pr_debug("%s: Reset PHB#%x, option=%d\n",
		 __func__, hose->global_number, option);

	/* Issue PHB complete reset request */
	if (option == EEH_RESET_FUNDAMENTAL ||
	    option == EEH_RESET_HOT)
		rc = opal_pci_reset(phb->opal_id,
				OPAL_RESET_PHB_COMPLETE,
				OPAL_ASSERT_RESET);
	else if (option == EEH_RESET_DEACTIVATE)
		rc = opal_pci_reset(phb->opal_id,
				OPAL_RESET_PHB_COMPLETE,
				OPAL_DEASSERT_RESET);
	if (rc < 0)
		goto out;

	/*
	 * Poll state of the PHB until the request is done
	 * successfully. The PHB reset is usually PHB complete
	 * reset followed by hot reset on root bus. So we also
	 * need the PCI bus settlement delay.
	 */
	rc = ioda_eeh_phb_poll(phb);
	if (option == EEH_RESET_DEACTIVATE) {
		if (system_state < SYSTEM_RUNNING)
			udelay(1000 * EEH_PE_RST_SETTLE_TIME);
		else
			msleep(EEH_PE_RST_SETTLE_TIME);
	}
out:
	if (rc != OPAL_SUCCESS)
		return -EIO;

	return 0;
}

static int ioda_eeh_root_reset(struct pci_controller *hose, int option)
{
	struct pnv_phb *phb = hose->private_data;
	s64 rc = OPAL_SUCCESS;

	pr_debug("%s: Reset PHB#%x, option=%d\n",
		 __func__, hose->global_number, option);

	/*
	 * During the reset deassert time, we needn't care
	 * the reset scope because the firmware does nothing
	 * for fundamental or hot reset during deassert phase.
	 */
	if (option == EEH_RESET_FUNDAMENTAL)
		rc = opal_pci_reset(phb->opal_id,
				OPAL_RESET_PCI_FUNDAMENTAL,
				OPAL_ASSERT_RESET);
	else if (option == EEH_RESET_HOT)
		rc = opal_pci_reset(phb->opal_id,
				OPAL_RESET_PCI_HOT,
				OPAL_ASSERT_RESET);
	else if (option == EEH_RESET_DEACTIVATE)
		rc = opal_pci_reset(phb->opal_id,
				OPAL_RESET_PCI_HOT,
				OPAL_DEASSERT_RESET);
	if (rc < 0)
		goto out;

	/* Poll state of the PHB until the request is done */
	rc = ioda_eeh_phb_poll(phb);
	if (option == EEH_RESET_DEACTIVATE)
		msleep(EEH_PE_RST_SETTLE_TIME);
out:
	if (rc != OPAL_SUCCESS)
		return -EIO;

	return 0;
}

static int ioda_eeh_bridge_reset(struct pci_dev *dev, int option)

{
	struct device_node *dn = pci_device_to_OF_node(dev);
	struct eeh_dev *edev = of_node_to_eeh_dev(dn);
	int aer = edev ? edev->aer_cap : 0;
	u32 ctrl;

	pr_debug("%s: Reset PCI bus %04x:%02x with option %d\n",
		 __func__, pci_domain_nr(dev->bus),
		 dev->bus->number, option);

	switch (option) {
	case EEH_RESET_FUNDAMENTAL:
	case EEH_RESET_HOT:
		/* Don't report linkDown event */
		if (aer) {
			eeh_ops->read_config(dn, aer + PCI_ERR_UNCOR_MASK,
					     4, &ctrl);
			ctrl |= PCI_ERR_UNC_SURPDN;
                        eeh_ops->write_config(dn, aer + PCI_ERR_UNCOR_MASK,
					      4, ctrl);
                }

		eeh_ops->read_config(dn, PCI_BRIDGE_CONTROL, 2, &ctrl);
		ctrl |= PCI_BRIDGE_CTL_BUS_RESET;
		eeh_ops->write_config(dn, PCI_BRIDGE_CONTROL, 2, ctrl);
		msleep(EEH_PE_RST_HOLD_TIME);

		break;
	case EEH_RESET_DEACTIVATE:
		eeh_ops->read_config(dn, PCI_BRIDGE_CONTROL, 2, &ctrl);
		ctrl &= ~PCI_BRIDGE_CTL_BUS_RESET;
		eeh_ops->write_config(dn, PCI_BRIDGE_CONTROL, 2, ctrl);
		msleep(EEH_PE_RST_SETTLE_TIME);

		/* Continue reporting linkDown event */
		if (aer) {
			eeh_ops->read_config(dn, aer + PCI_ERR_UNCOR_MASK,
					     4, &ctrl);
			ctrl &= ~PCI_ERR_UNC_SURPDN;
			eeh_ops->write_config(dn, aer + PCI_ERR_UNCOR_MASK,
					      4, ctrl);
		}

		break;
	}

	return 0;
}

void pnv_pci_reset_secondary_bus(struct pci_dev *dev)
{
	struct pci_controller *hose;

	if (pci_is_root_bus(dev->bus)) {
		hose = pci_bus_to_host(dev->bus);
		ioda_eeh_root_reset(hose, EEH_RESET_HOT);
		ioda_eeh_root_reset(hose, EEH_RESET_DEACTIVATE);
	} else {
		ioda_eeh_bridge_reset(dev, EEH_RESET_HOT);
		ioda_eeh_bridge_reset(dev, EEH_RESET_DEACTIVATE);
	}
}

/**
 * ioda_eeh_reset - Reset the indicated PE
 * @pe: EEH PE
 * @option: reset option
 *
 * Do reset on the indicated PE. For PCI bus sensitive PE,
 * we need to reset the parent p2p bridge. The PHB has to
 * be reinitialized if the p2p bridge is root bridge. For
 * PCI device sensitive PE, we will try to reset the device
 * through FLR. For now, we don't have OPAL APIs to do HARD
 * reset yet, so all reset would be SOFT (HOT) reset.
 */
static int ioda_eeh_reset(struct eeh_pe *pe, int option)
{
	struct pci_controller *hose = pe->phb;
	struct pci_bus *bus;
	int ret;

	/*
	 * For PHB reset, we always have complete reset. For those PEs whose
	 * primary bus derived from root complex (root bus) or root port
	 * (usually bus#1), we apply hot or fundamental reset on the root port.
	 * For other PEs, we always have hot reset on the PE primary bus.
	 *
	 * Here, we have different design to pHyp, which always clear the
	 * frozen state during PE reset. However, the good idea here from
	 * benh is to keep frozen state before we get PE reset done completely
	 * (until BAR restore). With the frozen state, HW drops illegal IO
	 * or MMIO access, which can incur recrusive frozen PE during PE
	 * reset. The side effect is that EEH core has to clear the frozen
	 * state explicitly after BAR restore.
	 */
	if (pe->type & EEH_PE_PHB) {
		ret = ioda_eeh_phb_reset(hose, option);
	} else {
		struct pnv_phb *phb;
		s64 rc;

		/*
		 * The frozen PE might be caused by PAPR error injection
		 * registers, which are expected to be cleared after hitting
		 * frozen PE as stated in the hardware spec. Unfortunately,
		 * that's not true on P7IOC. So we have to clear it manually
		 * to avoid recursive EEH errors during recovery.
		 */
		phb = hose->private_data;
		if (phb->model == PNV_PHB_MODEL_P7IOC &&
		    (option == EEH_RESET_HOT ||
		    option == EEH_RESET_FUNDAMENTAL)) {
			rc = opal_pci_reset(phb->opal_id,
					    OPAL_RESET_PHB_ERROR,
					    OPAL_ASSERT_RESET);
			if (rc != OPAL_SUCCESS) {
				pr_warn("%s: Failure %lld clearing "
					"error injection registers\n",
					__func__, rc);
				return -EIO;
			}
		}

		bus = eeh_pe_bus_get(pe);
		if (pci_is_root_bus(bus) ||
		    pci_is_root_bus(bus->parent))
			ret = ioda_eeh_root_reset(hose, option);
		else
			ret = ioda_eeh_bridge_reset(bus->self, option);
	}

	return ret;
}

struct pnv_eeh_ops ioda_eeh_ops = {
	.reset			= ioda_eeh_reset,
};
