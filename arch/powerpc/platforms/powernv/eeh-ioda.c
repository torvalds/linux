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

struct pnv_eeh_ops ioda_eeh_ops = {
	.post_init		= ioda_eeh_post_init,
	.set_option		= ioda_eeh_set_option,
	.get_state		= NULL,
	.reset			= NULL,
	.get_log		= NULL,
	.configure_bridge	= NULL,
	.next_error		= NULL
};
