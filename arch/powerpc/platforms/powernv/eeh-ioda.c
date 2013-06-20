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

struct pnv_eeh_ops ioda_eeh_ops = {
	.post_init		= ioda_eeh_post_init,
	.set_option		= ioda_eeh_set_option,
	.get_state		= ioda_eeh_get_state,
	.reset			= NULL,
	.get_log		= NULL,
	.configure_bridge	= NULL,
	.next_error		= NULL
};
