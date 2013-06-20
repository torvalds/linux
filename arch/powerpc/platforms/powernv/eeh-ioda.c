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

#include <linux/bootmem.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/msi.h>
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

/**
 * ioda_eeh_post_init - Chip dependent post initialization
 * @hose: PCI controller
 *
 * The function will be called after eeh PEs and devices
 * have been built. That means the EEH is ready to supply
 * service with I/O cache.
 */
static int ioda_eeh_post_init(struct pci_controller *hose)
{
	struct pnv_phb *phb = hose->private_data;

	/* FIXME: Enable it for PHB3 later */
	if (phb->type == PNV_PHB_IODA1)
		phb->eeh_enabled = 1;

	return 0;
}

/**
 * ioda_eeh_set_option - Set EEH operation or I/O setting
 * @pe: EEH PE
 * @option: options
 *
 * Enable or disable EEH option for the indicated PE. The
 * function also can be used to enable I/O or DMA for the
 * PE.
 */
static int ioda_eeh_set_option(struct eeh_pe *pe, int option)
{
	s64 ret;
	u32 pe_no;
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;

	/* Check on PE number */
	if (pe->addr < 0 || pe->addr >= phb->ioda.total_pe) {
		pr_err("%s: PE address %x out of range [0, %x] "
		       "on PHB#%x\n",
			__func__, pe->addr, phb->ioda.total_pe,
			hose->global_number);
		return -EINVAL;
	}

	pe_no = pe->addr;
	switch (option) {
	case EEH_OPT_DISABLE:
		ret = -EEXIST;
		break;
	case EEH_OPT_ENABLE:
		ret = 0;
		break;
	case EEH_OPT_THAW_MMIO:
		ret = opal_pci_eeh_freeze_clear(phb->opal_id, pe_no,
				OPAL_EEH_ACTION_CLEAR_FREEZE_MMIO);
		if (ret) {
			pr_warning("%s: Failed to enable MMIO for "
				   "PHB#%x-PE#%x, err=%lld\n",
				__func__, hose->global_number, pe_no, ret);
			return -EIO;
		}

		break;
	case EEH_OPT_THAW_DMA:
		ret = opal_pci_eeh_freeze_clear(phb->opal_id, pe_no,
				OPAL_EEH_ACTION_CLEAR_FREEZE_DMA);
		if (ret) {
			pr_warning("%s: Failed to enable DMA for "
				   "PHB#%x-PE#%x, err=%lld\n",
				__func__, hose->global_number, pe_no, ret);
			return -EIO;
		}

		break;
	default:
		pr_warning("%s: Invalid option %d\n", __func__, option);
		return -EINVAL;
	}

	return ret;
}

/**
 * ioda_eeh_get_state - Retrieve the state of PE
 * @pe: EEH PE
 *
 * The PE's state should be retrieved from the PEEV, PEST
 * IODA tables. Since the OPAL has exported the function
 * to do it, it'd better to use that.
 */
static int ioda_eeh_get_state(struct eeh_pe *pe)
{
	s64 ret = 0;
	u8 fstate;
	u16 pcierr;
	u32 pe_no;
	int result;
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;

	/*
	 * Sanity check on PE address. The PHB PE address should
	 * be zero.
	 */
	if (pe->addr < 0 || pe->addr >= phb->ioda.total_pe) {
		pr_err("%s: PE address %x out of range [0, %x] "
		       "on PHB#%x\n",
		       __func__, pe->addr, phb->ioda.total_pe,
		       hose->global_number);
		return EEH_STATE_NOT_SUPPORT;
	}

	/* Retrieve PE status through OPAL */
	pe_no = pe->addr;
	ret = opal_pci_eeh_freeze_status(phb->opal_id, pe_no,
			&fstate, &pcierr, NULL);
	if (ret) {
		pr_err("%s: Failed to get EEH status on "
		       "PHB#%x-PE#%x\n, err=%lld\n",
		       __func__, hose->global_number, pe_no, ret);
		return EEH_STATE_NOT_SUPPORT;
	}

	/* Check PHB status */
	if (pe->type & EEH_PE_PHB) {
		result = 0;
		result &= ~EEH_STATE_RESET_ACTIVE;

		if (pcierr != OPAL_EEH_PHB_ERROR) {
			result |= EEH_STATE_MMIO_ACTIVE;
			result |= EEH_STATE_DMA_ACTIVE;
			result |= EEH_STATE_MMIO_ENABLED;
			result |= EEH_STATE_DMA_ENABLED;
		}

		return result;
	}

	/* Parse result out */
	result = 0;
	switch (fstate) {
	case OPAL_EEH_STOPPED_NOT_FROZEN:
		result &= ~EEH_STATE_RESET_ACTIVE;
		result |= EEH_STATE_MMIO_ACTIVE;
		result |= EEH_STATE_DMA_ACTIVE;
		result |= EEH_STATE_MMIO_ENABLED;
		result |= EEH_STATE_DMA_ENABLED;
		break;
	case OPAL_EEH_STOPPED_MMIO_FREEZE:
		result &= ~EEH_STATE_RESET_ACTIVE;
		result |= EEH_STATE_DMA_ACTIVE;
		result |= EEH_STATE_DMA_ENABLED;
		break;
	case OPAL_EEH_STOPPED_DMA_FREEZE:
		result &= ~EEH_STATE_RESET_ACTIVE;
		result |= EEH_STATE_MMIO_ACTIVE;
		result |= EEH_STATE_MMIO_ENABLED;
		break;
	case OPAL_EEH_STOPPED_MMIO_DMA_FREEZE:
		result &= ~EEH_STATE_RESET_ACTIVE;
		break;
	case OPAL_EEH_STOPPED_RESET:
		result |= EEH_STATE_RESET_ACTIVE;
		break;
	case OPAL_EEH_STOPPED_TEMP_UNAVAIL:
		result |= EEH_STATE_UNAVAILABLE;
		break;
	case OPAL_EEH_STOPPED_PERM_UNAVAIL:
		result |= EEH_STATE_NOT_SUPPORT;
		break;
	default:
		pr_warning("%s: Unexpected EEH status 0x%x "
			   "on PHB#%x-PE#%x\n",
			   __func__, fstate, hose->global_number, pe_no);
	}

	return result;
}

static int ioda_eeh_pe_clear(struct eeh_pe *pe)
{
	struct pci_controller *hose;
	struct pnv_phb *phb;
	u32 pe_no;
	u8 fstate;
	u16 pcierr;
	s64 ret;

	pe_no = pe->addr;
	hose = pe->phb;
	phb = pe->phb->private_data;

	/* Clear the EEH error on the PE */
	ret = opal_pci_eeh_freeze_clear(phb->opal_id,
			pe_no, OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
	if (ret) {
		pr_err("%s: Failed to clear EEH error for "
		       "PHB#%x-PE#%x, err=%lld\n",
		       __func__, hose->global_number, pe_no, ret);
		return -EIO;
	}

	/*
	 * Read the PE state back and verify that the frozen
	 * state has been removed.
	 */
	ret = opal_pci_eeh_freeze_status(phb->opal_id, pe_no,
			&fstate, &pcierr, NULL);
	if (ret) {
		pr_err("%s: Failed to get EEH status on "
		       "PHB#%x-PE#%x\n, err=%lld\n",
		       __func__, hose->global_number, pe_no, ret);
		return -EIO;
	}

	if (fstate != OPAL_EEH_STOPPED_NOT_FROZEN) {
		pr_err("%s: Frozen state not cleared on "
		       "PHB#%x-PE#%x, sts=%x\n",
		       __func__, hose->global_number, pe_no, fstate);
		return -EIO;
	}

	return 0;
}

static s64 ioda_eeh_phb_poll(struct pnv_phb *phb)
{
	s64 rc = OPAL_HARDWARE;

	while (1) {
		rc = opal_pci_poll(phb->opal_id);
		if (rc <= 0)
			break;

		msleep(rc);
	}

	return rc;
}

static int ioda_eeh_phb_reset(struct pci_controller *hose, int option)
{
	struct pnv_phb *phb = hose->private_data;
	s64 rc = OPAL_HARDWARE;

	pr_debug("%s: Reset PHB#%x, option=%d\n",
		 __func__, hose->global_number, option);

	/* Issue PHB complete reset request */
	if (option == EEH_RESET_FUNDAMENTAL ||
	    option == EEH_RESET_HOT)
		rc = opal_pci_reset(phb->opal_id,
				OPAL_PHB_COMPLETE,
				OPAL_ASSERT_RESET);
	else if (option == EEH_RESET_DEACTIVATE)
		rc = opal_pci_reset(phb->opal_id,
				OPAL_PHB_COMPLETE,
				OPAL_DEASSERT_RESET);
	if (rc < 0)
		goto out;

	/*
	 * Poll state of the PHB until the request is done
	 * successfully.
	 */
	rc = ioda_eeh_phb_poll(phb);
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
				OPAL_PCI_FUNDAMENTAL_RESET,
				OPAL_ASSERT_RESET);
	else if (option == EEH_RESET_HOT)
		rc = opal_pci_reset(phb->opal_id,
				OPAL_PCI_HOT_RESET,
				OPAL_ASSERT_RESET);
	else if (option == EEH_RESET_DEACTIVATE)
		rc = opal_pci_reset(phb->opal_id,
				OPAL_PCI_HOT_RESET,
				OPAL_DEASSERT_RESET);
	if (rc < 0)
		goto out;

	/* Poll state of the PHB until the request is done */
	rc = ioda_eeh_phb_poll(phb);
out:
	if (rc != OPAL_SUCCESS)
		return -EIO;

	return 0;
}

static int ioda_eeh_bridge_reset(struct pci_controller *hose,
		struct pci_dev *dev, int option)
{
	u16 ctrl;

	pr_debug("%s: Reset device %04x:%02x:%02x.%01x with option %d\n",
		 __func__, hose->global_number, dev->bus->number,
		 PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), option);

	switch (option) {
	case EEH_RESET_FUNDAMENTAL:
	case EEH_RESET_HOT:
		pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &ctrl);
		ctrl |= PCI_BRIDGE_CTL_BUS_RESET;
		pci_write_config_word(dev, PCI_BRIDGE_CONTROL, ctrl);
		break;
	case EEH_RESET_DEACTIVATE:
		pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &ctrl);
		ctrl &= ~PCI_BRIDGE_CTL_BUS_RESET;
		pci_write_config_word(dev, PCI_BRIDGE_CONTROL, ctrl);
		break;
	}

	return 0;
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
	struct eeh_dev *edev;
	struct pci_dev *dev;
	int ret;

	/*
	 * Anyway, we have to clear the problematic state for the
	 * corresponding PE. However, we needn't do it if the PE
	 * is PHB associated. That means the PHB is having fatal
	 * errors and it needs reset. Further more, the AIB interface
	 * isn't reliable any more.
	 */
	if (!(pe->type & EEH_PE_PHB) &&
	    (option == EEH_RESET_HOT ||
	    option == EEH_RESET_FUNDAMENTAL)) {
		ret = ioda_eeh_pe_clear(pe);
		if (ret)
			return -EIO;
	}

	/*
	 * The rules applied to reset, either fundamental or hot reset:
	 *
	 * We always reset the direct upstream bridge of the PE. If the
	 * direct upstream bridge isn't root bridge, we always take hot
	 * reset no matter what option (fundamental or hot) is. Otherwise,
	 * we should do the reset according to the required option.
	 */
	if (pe->type & EEH_PE_PHB) {
		ret = ioda_eeh_phb_reset(hose, option);
	} else {
		if (pe->type & EEH_PE_DEVICE) {
			/*
			 * If it's device PE, we didn't refer to the parent
			 * PCI bus yet. So we have to figure it out indirectly.
			 */
			edev = list_first_entry(&pe->edevs,
					struct eeh_dev, list);
			dev = eeh_dev_to_pci_dev(edev);
			dev = dev->bus->self;
		} else {
			/*
			 * If it's bus PE, the parent PCI bus is already there
			 * and just pick it up.
			 */
			dev = pe->bus->self;
		}

		/*
		 * Do reset based on the fact that the direct upstream bridge
		 * is root bridge (port) or not.
		 */
		if (dev->bus->number == 0)
			ret = ioda_eeh_root_reset(hose, option);
		else
			ret = ioda_eeh_bridge_reset(hose, dev, option);
	}

	return ret;
}

/**
 * ioda_eeh_get_log - Retrieve error log
 * @pe: EEH PE
 * @severity: Severity level of the log
 * @drv_log: buffer to store the log
 * @len: space of the log buffer
 *
 * The function is used to retrieve error log from P7IOC.
 */
static int ioda_eeh_get_log(struct eeh_pe *pe, int severity,
			    char *drv_log, unsigned long len)
{
	s64 ret;
	unsigned long flags;
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;

	spin_lock_irqsave(&phb->lock, flags);

	ret = opal_pci_get_phb_diag_data2(phb->opal_id,
			phb->diag.blob, PNV_PCI_DIAG_BUF_SIZE);
	if (ret) {
		spin_unlock_irqrestore(&phb->lock, flags);
		pr_warning("%s: Failed to get log for PHB#%x-PE#%x\n",
			   __func__, hose->global_number, pe->addr);
		return -EIO;
	}

	/*
	 * FIXME: We probably need log the error in somewhere.
	 * Lets make it up in future.
	 */
	/* pr_info("%s", phb->diag.blob); */

	spin_unlock_irqrestore(&phb->lock, flags);

	return 0;
}

/**
 * ioda_eeh_configure_bridge - Configure the PCI bridges for the indicated PE
 * @pe: EEH PE
 *
 * For particular PE, it might have included PCI bridges. In order
 * to make the PE work properly, those PCI bridges should be configured
 * correctly. However, we need do nothing on P7IOC since the reset
 * function will do everything that should be covered by the function.
 */
static int ioda_eeh_configure_bridge(struct eeh_pe *pe)
{
	return 0;
}

struct pnv_eeh_ops ioda_eeh_ops = {
	.post_init		= ioda_eeh_post_init,
	.set_option		= ioda_eeh_set_option,
	.get_state		= ioda_eeh_get_state,
	.reset			= ioda_eeh_reset,
	.get_log		= ioda_eeh_get_log,
	.configure_bridge	= ioda_eeh_configure_bridge,
	.next_error		= NULL
};
