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

/* Debugging option */
#ifdef IODA_EEH_DBG_ON
#define IODA_EEH_DBG(args...)	pr_info(args)
#else
#define IODA_EEH_DBG(args...)
#endif

static char *hub_diag = NULL;
static int ioda_eeh_nb_init = 0;

static int ioda_eeh_event(struct notifier_block *nb,
			  unsigned long events, void *change)
{
	uint64_t changed_evts = (uint64_t)change;

	/* We simply send special EEH event */
	if ((changed_evts & OPAL_EVENT_PCI_ERROR) &&
	    (events & OPAL_EVENT_PCI_ERROR))
		eeh_send_failure_event(NULL);

	return 0;
}

static struct notifier_block ioda_eeh_nb = {
	.notifier_call	= ioda_eeh_event,
	.next		= NULL,
	.priority	= 0
};

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
	int ret;

	/* Register OPAL event notifier */
	if (!ioda_eeh_nb_init) {
		ret = opal_notifier_register(&ioda_eeh_nb);
		if (ret) {
			pr_err("%s: Can't register OPAL event notifier (%d)\n",
			       __func__, ret);
			return ret;
		}

		ioda_eeh_nb_init = 1;
	}

	/* FIXME: Enable it for PHB3 later */
	if (phb->type == PNV_PHB_IODA1) {
		if (!hub_diag) {
			hub_diag = (char *)__get_free_page(GFP_KERNEL |
							   __GFP_ZERO);
			if (!hub_diag) {
				pr_err("%s: Out of memory !\n",
				       __func__);
				return -ENOMEM;
			}
		}

		phb->eeh_enabled = 1;
	}

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

static void ioda_eeh_hub_diag_common(struct OpalIoP7IOCErrorData *data)
{
	/* GEM */
	pr_info("  GEM XFIR:        %016llx\n", data->gemXfir);
	pr_info("  GEM RFIR:        %016llx\n", data->gemRfir);
	pr_info("  GEM RIRQFIR:     %016llx\n", data->gemRirqfir);
	pr_info("  GEM Mask:        %016llx\n", data->gemMask);
	pr_info("  GEM RWOF:        %016llx\n", data->gemRwof);

	/* LEM */
	pr_info("  LEM FIR:         %016llx\n", data->lemFir);
	pr_info("  LEM Error Mask:  %016llx\n", data->lemErrMask);
	pr_info("  LEM Action 0:    %016llx\n", data->lemAction0);
	pr_info("  LEM Action 1:    %016llx\n", data->lemAction1);
	pr_info("  LEM WOF:         %016llx\n", data->lemWof);
}

static void ioda_eeh_hub_diag(struct pci_controller *hose)
{
	struct pnv_phb *phb = hose->private_data;
	struct OpalIoP7IOCErrorData *data;
	long rc;

	data = (struct OpalIoP7IOCErrorData *)ioda_eeh_hub_diag;
	rc = opal_pci_get_hub_diag_data(phb->hub_id, data, PAGE_SIZE);
	if (rc != OPAL_SUCCESS) {
		pr_warning("%s: Failed to get HUB#%llx diag-data (%ld)\n",
			   __func__, phb->hub_id, rc);
		return;
	}

	switch (data->type) {
	case OPAL_P7IOC_DIAG_TYPE_RGC:
		pr_info("P7IOC diag-data for RGC\n\n");
		ioda_eeh_hub_diag_common(data);
		pr_info("  RGC Status:      %016llx\n", data->rgc.rgcStatus);
		pr_info("  RGC LDCP:        %016llx\n", data->rgc.rgcLdcp);
		break;
	case OPAL_P7IOC_DIAG_TYPE_BI:
		pr_info("P7IOC diag-data for BI %s\n\n",
			data->bi.biDownbound ? "Downbound" : "Upbound");
		ioda_eeh_hub_diag_common(data);
		pr_info("  BI LDCP 0:       %016llx\n", data->bi.biLdcp0);
		pr_info("  BI LDCP 1:       %016llx\n", data->bi.biLdcp1);
		pr_info("  BI LDCP 2:       %016llx\n", data->bi.biLdcp2);
		pr_info("  BI Fence Status: %016llx\n", data->bi.biFenceStatus);
		break;
	case OPAL_P7IOC_DIAG_TYPE_CI:
		pr_info("P7IOC diag-data for CI Port %d\\nn",
			data->ci.ciPort);
		ioda_eeh_hub_diag_common(data);
		pr_info("  CI Port Status:  %016llx\n", data->ci.ciPortStatus);
		pr_info("  CI Port LDCP:    %016llx\n", data->ci.ciPortLdcp);
		break;
	case OPAL_P7IOC_DIAG_TYPE_MISC:
		pr_info("P7IOC diag-data for MISC\n\n");
		ioda_eeh_hub_diag_common(data);
		break;
	case OPAL_P7IOC_DIAG_TYPE_I2C:
		pr_info("P7IOC diag-data for I2C\n\n");
		ioda_eeh_hub_diag_common(data);
		break;
	default:
		pr_warning("%s: Invalid type of HUB#%llx diag-data (%d)\n",
			   __func__, phb->hub_id, data->type);
	}
}

static void ioda_eeh_p7ioc_phb_diag(struct pci_controller *hose,
				    struct OpalIoPhbErrorCommon *common)
{
	struct OpalIoP7IOCPhbErrorData *data;
	int i;

	data = (struct OpalIoP7IOCPhbErrorData *)common;

	pr_info("P7IOC PHB#%x Diag-data (Version: %d)\n\n",
		hose->global_number, common->version);

	pr_info("  brdgCtl:              %08x\n", data->brdgCtl);

	pr_info("  portStatusReg:        %08x\n", data->portStatusReg);
	pr_info("  rootCmplxStatus:      %08x\n", data->rootCmplxStatus);
	pr_info("  busAgentStatus:       %08x\n", data->busAgentStatus);

	pr_info("  deviceStatus:         %08x\n", data->deviceStatus);
	pr_info("  slotStatus:           %08x\n", data->slotStatus);
	pr_info("  linkStatus:           %08x\n", data->linkStatus);
	pr_info("  devCmdStatus:         %08x\n", data->devCmdStatus);
	pr_info("  devSecStatus:         %08x\n", data->devSecStatus);

	pr_info("  rootErrorStatus:      %08x\n", data->rootErrorStatus);
	pr_info("  uncorrErrorStatus:    %08x\n", data->uncorrErrorStatus);
	pr_info("  corrErrorStatus:      %08x\n", data->corrErrorStatus);
	pr_info("  tlpHdr1:              %08x\n", data->tlpHdr1);
	pr_info("  tlpHdr2:              %08x\n", data->tlpHdr2);
	pr_info("  tlpHdr3:              %08x\n", data->tlpHdr3);
	pr_info("  tlpHdr4:              %08x\n", data->tlpHdr4);
	pr_info("  sourceId:             %08x\n", data->sourceId);

	pr_info("  errorClass:           %016llx\n", data->errorClass);
	pr_info("  correlator:           %016llx\n", data->correlator);
	pr_info("  p7iocPlssr:           %016llx\n", data->p7iocPlssr);
	pr_info("  p7iocCsr:             %016llx\n", data->p7iocCsr);
	pr_info("  lemFir:               %016llx\n", data->lemFir);
	pr_info("  lemErrorMask:         %016llx\n", data->lemErrorMask);
	pr_info("  lemWOF:               %016llx\n", data->lemWOF);
	pr_info("  phbErrorStatus:       %016llx\n", data->phbErrorStatus);
	pr_info("  phbFirstErrorStatus:  %016llx\n", data->phbFirstErrorStatus);
	pr_info("  phbErrorLog0:         %016llx\n", data->phbErrorLog0);
	pr_info("  phbErrorLog1:         %016llx\n", data->phbErrorLog1);
	pr_info("  mmioErrorStatus:      %016llx\n", data->mmioErrorStatus);
	pr_info("  mmioFirstErrorStatus: %016llx\n", data->mmioFirstErrorStatus);
	pr_info("  mmioErrorLog0:        %016llx\n", data->mmioErrorLog0);
	pr_info("  mmioErrorLog1:        %016llx\n", data->mmioErrorLog1);
	pr_info("  dma0ErrorStatus:      %016llx\n", data->dma0ErrorStatus);
	pr_info("  dma0FirstErrorStatus: %016llx\n", data->dma0FirstErrorStatus);
	pr_info("  dma0ErrorLog0:        %016llx\n", data->dma0ErrorLog0);
	pr_info("  dma0ErrorLog1:        %016llx\n", data->dma0ErrorLog1);
	pr_info("  dma1ErrorStatus:      %016llx\n", data->dma1ErrorStatus);
	pr_info("  dma1FirstErrorStatus: %016llx\n", data->dma1FirstErrorStatus);
	pr_info("  dma1ErrorLog0:        %016llx\n", data->dma1ErrorLog0);
	pr_info("  dma1ErrorLog1:        %016llx\n", data->dma1ErrorLog1);

	for (i = 0; i < OPAL_P7IOC_NUM_PEST_REGS; i++) {
		if ((data->pestA[i] >> 63) == 0 &&
		    (data->pestB[i] >> 63) == 0)
			continue;

		pr_info("  PE[%3d] PESTA:        %016llx\n", i, data->pestA[i]);
		pr_info("          PESTB:        %016llx\n", data->pestB[i]);
	}
}

static void ioda_eeh_phb_diag(struct pci_controller *hose)
{
	struct pnv_phb *phb = hose->private_data;
	struct OpalIoPhbErrorCommon *common;
	long rc;

	common = (struct OpalIoPhbErrorCommon *)phb->diag.blob;
	rc = opal_pci_get_phb_diag_data2(phb->opal_id, common, PAGE_SIZE);
	if (rc != OPAL_SUCCESS) {
		pr_warning("%s: Failed to get diag-data for PHB#%x (%ld)\n",
			    __func__, hose->global_number, rc);
		return;
	}

	switch (common->ioType) {
	case OPAL_PHB_ERROR_DATA_TYPE_P7IOC:
		ioda_eeh_p7ioc_phb_diag(hose, common);
		break;
	default:
		pr_warning("%s: Unrecognized I/O chip %d\n",
			   __func__, common->ioType);
	}
}

static int ioda_eeh_get_phb_pe(struct pci_controller *hose,
			       struct eeh_pe **pe)
{
	struct eeh_pe *phb_pe;

	phb_pe = eeh_phb_pe_get(hose);
	if (!phb_pe) {
		pr_warning("%s Can't find PE for PHB#%d\n",
			   __func__, hose->global_number);
		return -EEXIST;
	}

	*pe = phb_pe;
	return 0;
}

static int ioda_eeh_get_pe(struct pci_controller *hose,
			   u16 pe_no, struct eeh_pe **pe)
{
	struct eeh_pe *phb_pe, *dev_pe;
	struct eeh_dev dev;

	/* Find the PHB PE */
	if (ioda_eeh_get_phb_pe(hose, &phb_pe))
		return -EEXIST;

	/* Find the PE according to PE# */
	memset(&dev, 0, sizeof(struct eeh_dev));
	dev.phb = hose;
	dev.pe_config_addr = pe_no;
	dev_pe = eeh_pe_get(&dev);
	if (!dev_pe) {
		pr_warning("%s: Can't find PE for PHB#%x - PE#%x\n",
			   __func__, hose->global_number, pe_no);
		return -EEXIST;
	}

	*pe = dev_pe;
	return 0;
}

/**
 * ioda_eeh_next_error - Retrieve next error for EEH core to handle
 * @pe: The affected PE
 *
 * The function is expected to be called by EEH core while it gets
 * special EEH event (without binding PE). The function calls to
 * OPAL APIs for next error to handle. The informational error is
 * handled internally by platform. However, the dead IOC, dead PHB,
 * fenced PHB and frozen PE should be handled by EEH core eventually.
 */
static int ioda_eeh_next_error(struct eeh_pe **pe)
{
	struct pci_controller *hose, *tmp;
	struct pnv_phb *phb;
	u64 frozen_pe_no;
	u16 err_type, severity;
	long rc;
	int ret = 1;

	/*
	 * While running here, it's safe to purge the event queue.
	 * And we should keep the cached OPAL notifier event sychronized
	 * between the kernel and firmware.
	 */
	eeh_remove_event(NULL);
	opal_notifier_update_evt(OPAL_EVENT_PCI_ERROR, 0x0ul);

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		/*
		 * If the subordinate PCI buses of the PHB has been
		 * removed, we needn't take care of it any more.
		 */
		phb = hose->private_data;
		if (phb->removed)
			continue;

		rc = opal_pci_next_error(phb->opal_id,
				&frozen_pe_no, &err_type, &severity);

		/* If OPAL API returns error, we needn't proceed */
		if (rc != OPAL_SUCCESS) {
			IODA_EEH_DBG("%s: Invalid return value on "
				     "PHB#%x (0x%lx) from opal_pci_next_error",
				     __func__, hose->global_number, rc);
			continue;
		}

		/* If the PHB doesn't have error, stop processing */
		if (err_type == OPAL_EEH_NO_ERROR ||
		    severity == OPAL_EEH_SEV_NO_ERROR) {
			IODA_EEH_DBG("%s: No error found on PHB#%x\n",
				     __func__, hose->global_number);
			continue;
		}

		/*
		 * Processing the error. We're expecting the error with
		 * highest priority reported upon multiple errors on the
		 * specific PHB.
		 */
		IODA_EEH_DBG("%s: Error (%d, %d, %d) on PHB#%x\n",
			err_type, severity, pe_no, hose->global_number);
		switch (err_type) {
		case OPAL_EEH_IOC_ERROR:
			if (severity == OPAL_EEH_SEV_IOC_DEAD) {
				list_for_each_entry_safe(hose, tmp,
						&hose_list, list_node) {
					phb = hose->private_data;
					phb->removed = 1;
				}

				WARN(1, "EEH: dead IOC detected\n");
				ret = 4;
				goto out;
			} else if (severity == OPAL_EEH_SEV_INF)
				ioda_eeh_hub_diag(hose);

			break;
		case OPAL_EEH_PHB_ERROR:
			if (severity == OPAL_EEH_SEV_PHB_DEAD) {
				if (ioda_eeh_get_phb_pe(hose, pe))
					break;

				WARN(1, "EEH: dead PHB#%x detected\n",
				     hose->global_number);
				phb->removed = 1;
				ret = 3;
				goto out;
			} else if (severity == OPAL_EEH_SEV_PHB_FENCED) {
				if (ioda_eeh_get_phb_pe(hose, pe))
					break;

				WARN(1, "EEH: fenced PHB#%x detected\n",
				     hose->global_number);
				ret = 2;
				goto out;
			} else if (severity == OPAL_EEH_SEV_INF)
				ioda_eeh_phb_diag(hose);

			break;
		case OPAL_EEH_PE_ERROR:
			if (ioda_eeh_get_pe(hose, frozen_pe_no, pe))
				break;

			WARN(1, "EEH: Frozen PE#%x on PHB#%x detected\n",
			     (*pe)->addr, (*pe)->phb->global_number);
			ret = 1;
			goto out;
		}
	}

	ret = 0;
out:
	return ret;
}

struct pnv_eeh_ops ioda_eeh_ops = {
	.post_init		= ioda_eeh_post_init,
	.set_option		= ioda_eeh_set_option,
	.get_state		= ioda_eeh_get_state,
	.reset			= ioda_eeh_reset,
	.get_log		= ioda_eeh_get_log,
	.configure_bridge	= ioda_eeh_configure_bridge,
	.next_error		= ioda_eeh_next_error
};
