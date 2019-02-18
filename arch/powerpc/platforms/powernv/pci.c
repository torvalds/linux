/*
 * Support PCI/PCIe on PowerNV platforms
 *
 * Copyright 2011 Benjamin Herrenschmidt, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/msi.h>
#include <linux/iommu.h>
#include <linux/sched/mm.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/msi_bitmap.h>
#include <asm/ppc-pci.h>
#include <asm/pnv-pci.h>
#include <asm/opal.h>
#include <asm/iommu.h>
#include <asm/tce.h>
#include <asm/firmware.h>
#include <asm/eeh_event.h>
#include <asm/eeh.h>

#include "powernv.h"
#include "pci.h"

static DEFINE_MUTEX(p2p_mutex);
static DEFINE_MUTEX(tunnel_mutex);

int pnv_pci_get_slot_id(struct device_node *np, uint64_t *id)
{
	struct device_node *parent = np;
	u32 bdfn;
	u64 phbid;
	int ret;

	ret = of_property_read_u32(np, "reg", &bdfn);
	if (ret)
		return -ENXIO;

	bdfn = ((bdfn & 0x00ffff00) >> 8);
	while ((parent = of_get_parent(parent))) {
		if (!PCI_DN(parent)) {
			of_node_put(parent);
			break;
		}

		if (!of_device_is_compatible(parent, "ibm,ioda2-phb")) {
			of_node_put(parent);
			continue;
		}

		ret = of_property_read_u64(parent, "ibm,opal-phbid", &phbid);
		if (ret) {
			of_node_put(parent);
			return -ENXIO;
		}

		*id = PCI_SLOT_ID(phbid, bdfn);
		return 0;
	}

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(pnv_pci_get_slot_id);

int pnv_pci_get_device_tree(uint32_t phandle, void *buf, uint64_t len)
{
	int64_t rc;

	if (!opal_check_token(OPAL_GET_DEVICE_TREE))
		return -ENXIO;

	rc = opal_get_device_tree(phandle, (uint64_t)buf, len);
	if (rc < OPAL_SUCCESS)
		return -EIO;

	return rc;
}
EXPORT_SYMBOL_GPL(pnv_pci_get_device_tree);

int pnv_pci_get_presence_state(uint64_t id, uint8_t *state)
{
	int64_t rc;

	if (!opal_check_token(OPAL_PCI_GET_PRESENCE_STATE))
		return -ENXIO;

	rc = opal_pci_get_presence_state(id, (uint64_t)state);
	if (rc != OPAL_SUCCESS)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_GPL(pnv_pci_get_presence_state);

int pnv_pci_get_power_state(uint64_t id, uint8_t *state)
{
	int64_t rc;

	if (!opal_check_token(OPAL_PCI_GET_POWER_STATE))
		return -ENXIO;

	rc = opal_pci_get_power_state(id, (uint64_t)state);
	if (rc != OPAL_SUCCESS)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_GPL(pnv_pci_get_power_state);

int pnv_pci_set_power_state(uint64_t id, uint8_t state, struct opal_msg *msg)
{
	struct opal_msg m;
	int token, ret;
	int64_t rc;

	if (!opal_check_token(OPAL_PCI_SET_POWER_STATE))
		return -ENXIO;

	token = opal_async_get_token_interruptible();
	if (unlikely(token < 0))
		return token;

	rc = opal_pci_set_power_state(token, id, (uint64_t)&state);
	if (rc == OPAL_SUCCESS) {
		ret = 0;
		goto exit;
	} else if (rc != OPAL_ASYNC_COMPLETION) {
		ret = -EIO;
		goto exit;
	}

	ret = opal_async_wait_response(token, &m);
	if (ret < 0)
		goto exit;

	if (msg) {
		ret = 1;
		memcpy(msg, &m, sizeof(m));
	}

exit:
	opal_async_release_token(token);
	return ret;
}
EXPORT_SYMBOL_GPL(pnv_pci_set_power_state);

int pnv_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct msi_desc *entry;
	struct msi_msg msg;
	int hwirq;
	unsigned int virq;
	int rc;

	if (WARN_ON(!phb) || !phb->msi_bmp.bitmap)
		return -ENODEV;

	if (pdev->no_64bit_msi && !phb->msi32_support)
		return -ENODEV;

	for_each_pci_msi_entry(entry, pdev) {
		if (!entry->msi_attrib.is_64 && !phb->msi32_support) {
			pr_warn("%s: Supports only 64-bit MSIs\n",
				pci_name(pdev));
			return -ENXIO;
		}
		hwirq = msi_bitmap_alloc_hwirqs(&phb->msi_bmp, 1);
		if (hwirq < 0) {
			pr_warn("%s: Failed to find a free MSI\n",
				pci_name(pdev));
			return -ENOSPC;
		}
		virq = irq_create_mapping(NULL, phb->msi_base + hwirq);
		if (!virq) {
			pr_warn("%s: Failed to map MSI to linux irq\n",
				pci_name(pdev));
			msi_bitmap_free_hwirqs(&phb->msi_bmp, hwirq, 1);
			return -ENOMEM;
		}
		rc = phb->msi_setup(phb, pdev, phb->msi_base + hwirq,
				    virq, entry->msi_attrib.is_64, &msg);
		if (rc) {
			pr_warn("%s: Failed to setup MSI\n", pci_name(pdev));
			irq_dispose_mapping(virq);
			msi_bitmap_free_hwirqs(&phb->msi_bmp, hwirq, 1);
			return rc;
		}
		irq_set_msi_desc(virq, entry);
		pci_write_msi_msg(virq, &msg);
	}
	return 0;
}

void pnv_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct msi_desc *entry;
	irq_hw_number_t hwirq;

	if (WARN_ON(!phb))
		return;

	for_each_pci_msi_entry(entry, pdev) {
		if (!entry->irq)
			continue;
		hwirq = virq_to_hw(entry->irq);
		irq_set_msi_desc(entry->irq, NULL);
		irq_dispose_mapping(entry->irq);
		msi_bitmap_free_hwirqs(&phb->msi_bmp, hwirq - phb->msi_base, 1);
	}
}

/* Nicely print the contents of the PE State Tables (PEST). */
static void pnv_pci_dump_pest(__be64 pestA[], __be64 pestB[], int pest_size)
{
	__be64 prevA = ULONG_MAX, prevB = ULONG_MAX;
	bool dup = false;
	int i;

	for (i = 0; i < pest_size; i++) {
		__be64 peA = be64_to_cpu(pestA[i]);
		__be64 peB = be64_to_cpu(pestB[i]);

		if (peA != prevA || peB != prevB) {
			if (dup) {
				pr_info("PE[..%03x] A/B: as above\n", i-1);
				dup = false;
			}
			prevA = peA;
			prevB = peB;
			if (peA & PNV_IODA_STOPPED_STATE ||
			    peB & PNV_IODA_STOPPED_STATE)
				pr_info("PE[%03x] A/B: %016llx %016llx\n",
					i, peA, peB);
		} else if (!dup && (peA & PNV_IODA_STOPPED_STATE ||
				    peB & PNV_IODA_STOPPED_STATE)) {
			dup = true;
		}
	}
}

static void pnv_pci_dump_p7ioc_diag_data(struct pci_controller *hose,
					 struct OpalIoPhbErrorCommon *common)
{
	struct OpalIoP7IOCPhbErrorData *data;

	data = (struct OpalIoP7IOCPhbErrorData *)common;
	pr_info("P7IOC PHB#%x Diag-data (Version: %d)\n",
		hose->global_number, be32_to_cpu(common->version));

	if (data->brdgCtl)
		pr_info("brdgCtl:     %08x\n",
			be32_to_cpu(data->brdgCtl));
	if (data->portStatusReg || data->rootCmplxStatus ||
	    data->busAgentStatus)
		pr_info("UtlSts:      %08x %08x %08x\n",
			be32_to_cpu(data->portStatusReg),
			be32_to_cpu(data->rootCmplxStatus),
			be32_to_cpu(data->busAgentStatus));
	if (data->deviceStatus || data->slotStatus   ||
	    data->linkStatus   || data->devCmdStatus ||
	    data->devSecStatus)
		pr_info("RootSts:     %08x %08x %08x %08x %08x\n",
			be32_to_cpu(data->deviceStatus),
			be32_to_cpu(data->slotStatus),
			be32_to_cpu(data->linkStatus),
			be32_to_cpu(data->devCmdStatus),
			be32_to_cpu(data->devSecStatus));
	if (data->rootErrorStatus   || data->uncorrErrorStatus ||
	    data->corrErrorStatus)
		pr_info("RootErrSts:  %08x %08x %08x\n",
			be32_to_cpu(data->rootErrorStatus),
			be32_to_cpu(data->uncorrErrorStatus),
			be32_to_cpu(data->corrErrorStatus));
	if (data->tlpHdr1 || data->tlpHdr2 ||
	    data->tlpHdr3 || data->tlpHdr4)
		pr_info("RootErrLog:  %08x %08x %08x %08x\n",
			be32_to_cpu(data->tlpHdr1),
			be32_to_cpu(data->tlpHdr2),
			be32_to_cpu(data->tlpHdr3),
			be32_to_cpu(data->tlpHdr4));
	if (data->sourceId || data->errorClass ||
	    data->correlator)
		pr_info("RootErrLog1: %08x %016llx %016llx\n",
			be32_to_cpu(data->sourceId),
			be64_to_cpu(data->errorClass),
			be64_to_cpu(data->correlator));
	if (data->p7iocPlssr || data->p7iocCsr)
		pr_info("PhbSts:      %016llx %016llx\n",
			be64_to_cpu(data->p7iocPlssr),
			be64_to_cpu(data->p7iocCsr));
	if (data->lemFir)
		pr_info("Lem:         %016llx %016llx %016llx\n",
			be64_to_cpu(data->lemFir),
			be64_to_cpu(data->lemErrorMask),
			be64_to_cpu(data->lemWOF));
	if (data->phbErrorStatus)
		pr_info("PhbErr:      %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbErrorStatus),
			be64_to_cpu(data->phbFirstErrorStatus),
			be64_to_cpu(data->phbErrorLog0),
			be64_to_cpu(data->phbErrorLog1));
	if (data->mmioErrorStatus)
		pr_info("OutErr:      %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->mmioErrorStatus),
			be64_to_cpu(data->mmioFirstErrorStatus),
			be64_to_cpu(data->mmioErrorLog0),
			be64_to_cpu(data->mmioErrorLog1));
	if (data->dma0ErrorStatus)
		pr_info("InAErr:      %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->dma0ErrorStatus),
			be64_to_cpu(data->dma0FirstErrorStatus),
			be64_to_cpu(data->dma0ErrorLog0),
			be64_to_cpu(data->dma0ErrorLog1));
	if (data->dma1ErrorStatus)
		pr_info("InBErr:      %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->dma1ErrorStatus),
			be64_to_cpu(data->dma1FirstErrorStatus),
			be64_to_cpu(data->dma1ErrorLog0),
			be64_to_cpu(data->dma1ErrorLog1));

	pnv_pci_dump_pest(data->pestA, data->pestB, OPAL_P7IOC_NUM_PEST_REGS);
}

static void pnv_pci_dump_phb3_diag_data(struct pci_controller *hose,
					struct OpalIoPhbErrorCommon *common)
{
	struct OpalIoPhb3ErrorData *data;

	data = (struct OpalIoPhb3ErrorData*)common;
	pr_info("PHB3 PHB#%x Diag-data (Version: %d)\n",
		hose->global_number, be32_to_cpu(common->version));
	if (data->brdgCtl)
		pr_info("brdgCtl:     %08x\n",
			be32_to_cpu(data->brdgCtl));
	if (data->portStatusReg || data->rootCmplxStatus ||
	    data->busAgentStatus)
		pr_info("UtlSts:      %08x %08x %08x\n",
			be32_to_cpu(data->portStatusReg),
			be32_to_cpu(data->rootCmplxStatus),
			be32_to_cpu(data->busAgentStatus));
	if (data->deviceStatus || data->slotStatus   ||
	    data->linkStatus   || data->devCmdStatus ||
	    data->devSecStatus)
		pr_info("RootSts:     %08x %08x %08x %08x %08x\n",
			be32_to_cpu(data->deviceStatus),
			be32_to_cpu(data->slotStatus),
			be32_to_cpu(data->linkStatus),
			be32_to_cpu(data->devCmdStatus),
			be32_to_cpu(data->devSecStatus));
	if (data->rootErrorStatus || data->uncorrErrorStatus ||
	    data->corrErrorStatus)
		pr_info("RootErrSts:  %08x %08x %08x\n",
			be32_to_cpu(data->rootErrorStatus),
			be32_to_cpu(data->uncorrErrorStatus),
			be32_to_cpu(data->corrErrorStatus));
	if (data->tlpHdr1 || data->tlpHdr2 ||
	    data->tlpHdr3 || data->tlpHdr4)
		pr_info("RootErrLog:  %08x %08x %08x %08x\n",
			be32_to_cpu(data->tlpHdr1),
			be32_to_cpu(data->tlpHdr2),
			be32_to_cpu(data->tlpHdr3),
			be32_to_cpu(data->tlpHdr4));
	if (data->sourceId || data->errorClass ||
	    data->correlator)
		pr_info("RootErrLog1: %08x %016llx %016llx\n",
			be32_to_cpu(data->sourceId),
			be64_to_cpu(data->errorClass),
			be64_to_cpu(data->correlator));
	if (data->nFir)
		pr_info("nFir:        %016llx %016llx %016llx\n",
			be64_to_cpu(data->nFir),
			be64_to_cpu(data->nFirMask),
			be64_to_cpu(data->nFirWOF));
	if (data->phbPlssr || data->phbCsr)
		pr_info("PhbSts:      %016llx %016llx\n",
			be64_to_cpu(data->phbPlssr),
			be64_to_cpu(data->phbCsr));
	if (data->lemFir)
		pr_info("Lem:         %016llx %016llx %016llx\n",
			be64_to_cpu(data->lemFir),
			be64_to_cpu(data->lemErrorMask),
			be64_to_cpu(data->lemWOF));
	if (data->phbErrorStatus)
		pr_info("PhbErr:      %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbErrorStatus),
			be64_to_cpu(data->phbFirstErrorStatus),
			be64_to_cpu(data->phbErrorLog0),
			be64_to_cpu(data->phbErrorLog1));
	if (data->mmioErrorStatus)
		pr_info("OutErr:      %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->mmioErrorStatus),
			be64_to_cpu(data->mmioFirstErrorStatus),
			be64_to_cpu(data->mmioErrorLog0),
			be64_to_cpu(data->mmioErrorLog1));
	if (data->dma0ErrorStatus)
		pr_info("InAErr:      %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->dma0ErrorStatus),
			be64_to_cpu(data->dma0FirstErrorStatus),
			be64_to_cpu(data->dma0ErrorLog0),
			be64_to_cpu(data->dma0ErrorLog1));
	if (data->dma1ErrorStatus)
		pr_info("InBErr:      %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->dma1ErrorStatus),
			be64_to_cpu(data->dma1FirstErrorStatus),
			be64_to_cpu(data->dma1ErrorLog0),
			be64_to_cpu(data->dma1ErrorLog1));

	pnv_pci_dump_pest(data->pestA, data->pestB, OPAL_PHB3_NUM_PEST_REGS);
}

static void pnv_pci_dump_phb4_diag_data(struct pci_controller *hose,
					struct OpalIoPhbErrorCommon *common)
{
	struct OpalIoPhb4ErrorData *data;

	data = (struct OpalIoPhb4ErrorData*)common;
	pr_info("PHB4 PHB#%d Diag-data (Version: %d)\n",
		hose->global_number, be32_to_cpu(common->version));
	if (data->brdgCtl)
		pr_info("brdgCtl:    %08x\n",
			be32_to_cpu(data->brdgCtl));
	if (data->deviceStatus || data->slotStatus   ||
	    data->linkStatus   || data->devCmdStatus ||
	    data->devSecStatus)
		pr_info("RootSts:    %08x %08x %08x %08x %08x\n",
			be32_to_cpu(data->deviceStatus),
			be32_to_cpu(data->slotStatus),
			be32_to_cpu(data->linkStatus),
			be32_to_cpu(data->devCmdStatus),
			be32_to_cpu(data->devSecStatus));
	if (data->rootErrorStatus || data->uncorrErrorStatus ||
	    data->corrErrorStatus)
		pr_info("RootErrSts: %08x %08x %08x\n",
			be32_to_cpu(data->rootErrorStatus),
			be32_to_cpu(data->uncorrErrorStatus),
			be32_to_cpu(data->corrErrorStatus));
	if (data->tlpHdr1 || data->tlpHdr2 ||
	    data->tlpHdr3 || data->tlpHdr4)
		pr_info("RootErrLog: %08x %08x %08x %08x\n",
			be32_to_cpu(data->tlpHdr1),
			be32_to_cpu(data->tlpHdr2),
			be32_to_cpu(data->tlpHdr3),
			be32_to_cpu(data->tlpHdr4));
	if (data->sourceId)
		pr_info("sourceId:   %08x\n", be32_to_cpu(data->sourceId));
	if (data->nFir)
		pr_info("nFir:       %016llx %016llx %016llx\n",
			be64_to_cpu(data->nFir),
			be64_to_cpu(data->nFirMask),
			be64_to_cpu(data->nFirWOF));
	if (data->phbPlssr || data->phbCsr)
		pr_info("PhbSts:     %016llx %016llx\n",
			be64_to_cpu(data->phbPlssr),
			be64_to_cpu(data->phbCsr));
	if (data->lemFir)
		pr_info("Lem:        %016llx %016llx %016llx\n",
			be64_to_cpu(data->lemFir),
			be64_to_cpu(data->lemErrorMask),
			be64_to_cpu(data->lemWOF));
	if (data->phbErrorStatus)
		pr_info("PhbErr:     %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbErrorStatus),
			be64_to_cpu(data->phbFirstErrorStatus),
			be64_to_cpu(data->phbErrorLog0),
			be64_to_cpu(data->phbErrorLog1));
	if (data->phbTxeErrorStatus)
		pr_info("PhbTxeErr:  %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbTxeErrorStatus),
			be64_to_cpu(data->phbTxeFirstErrorStatus),
			be64_to_cpu(data->phbTxeErrorLog0),
			be64_to_cpu(data->phbTxeErrorLog1));
	if (data->phbRxeArbErrorStatus)
		pr_info("RxeArbErr:  %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbRxeArbErrorStatus),
			be64_to_cpu(data->phbRxeArbFirstErrorStatus),
			be64_to_cpu(data->phbRxeArbErrorLog0),
			be64_to_cpu(data->phbRxeArbErrorLog1));
	if (data->phbRxeMrgErrorStatus)
		pr_info("RxeMrgErr:  %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbRxeMrgErrorStatus),
			be64_to_cpu(data->phbRxeMrgFirstErrorStatus),
			be64_to_cpu(data->phbRxeMrgErrorLog0),
			be64_to_cpu(data->phbRxeMrgErrorLog1));
	if (data->phbRxeTceErrorStatus)
		pr_info("RxeTceErr:  %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbRxeTceErrorStatus),
			be64_to_cpu(data->phbRxeTceFirstErrorStatus),
			be64_to_cpu(data->phbRxeTceErrorLog0),
			be64_to_cpu(data->phbRxeTceErrorLog1));

	if (data->phbPblErrorStatus)
		pr_info("PblErr:     %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbPblErrorStatus),
			be64_to_cpu(data->phbPblFirstErrorStatus),
			be64_to_cpu(data->phbPblErrorLog0),
			be64_to_cpu(data->phbPblErrorLog1));
	if (data->phbPcieDlpErrorStatus)
		pr_info("PcieDlp:    %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbPcieDlpErrorLog1),
			be64_to_cpu(data->phbPcieDlpErrorLog2),
			be64_to_cpu(data->phbPcieDlpErrorStatus));
	if (data->phbRegbErrorStatus)
		pr_info("RegbErr:    %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->phbRegbErrorStatus),
			be64_to_cpu(data->phbRegbFirstErrorStatus),
			be64_to_cpu(data->phbRegbErrorLog0),
			be64_to_cpu(data->phbRegbErrorLog1));


	pnv_pci_dump_pest(data->pestA, data->pestB, OPAL_PHB4_NUM_PEST_REGS);
}

void pnv_pci_dump_phb_diag_data(struct pci_controller *hose,
				unsigned char *log_buff)
{
	struct OpalIoPhbErrorCommon *common;

	if (!hose || !log_buff)
		return;

	common = (struct OpalIoPhbErrorCommon *)log_buff;
	switch (be32_to_cpu(common->ioType)) {
	case OPAL_PHB_ERROR_DATA_TYPE_P7IOC:
		pnv_pci_dump_p7ioc_diag_data(hose, common);
		break;
	case OPAL_PHB_ERROR_DATA_TYPE_PHB3:
		pnv_pci_dump_phb3_diag_data(hose, common);
		break;
	case OPAL_PHB_ERROR_DATA_TYPE_PHB4:
		pnv_pci_dump_phb4_diag_data(hose, common);
		break;
	default:
		pr_warn("%s: Unrecognized ioType %d\n",
			__func__, be32_to_cpu(common->ioType));
	}
}

static void pnv_pci_handle_eeh_config(struct pnv_phb *phb, u32 pe_no)
{
	unsigned long flags, rc;
	int has_diag, ret = 0;

	spin_lock_irqsave(&phb->lock, flags);

	/* Fetch PHB diag-data */
	rc = opal_pci_get_phb_diag_data2(phb->opal_id, phb->diag_data,
					 phb->diag_data_size);
	has_diag = (rc == OPAL_SUCCESS);

	/* If PHB supports compound PE, to handle it */
	if (phb->unfreeze_pe) {
		ret = phb->unfreeze_pe(phb,
				       pe_no,
				       OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
	} else {
		rc = opal_pci_eeh_freeze_clear(phb->opal_id,
					     pe_no,
					     OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
		if (rc) {
			pr_warn("%s: Failure %ld clearing frozen "
				"PHB#%x-PE#%x\n",
				__func__, rc, phb->hose->global_number,
				pe_no);
			ret = -EIO;
		}
	}

	/*
	 * For now, let's only display the diag buffer when we fail to clear
	 * the EEH status. We'll do more sensible things later when we have
	 * proper EEH support. We need to make sure we don't pollute ourselves
	 * with the normal errors generated when probing empty slots
	 */
	if (has_diag && ret)
		pnv_pci_dump_phb_diag_data(phb->hose, phb->diag_data);

	spin_unlock_irqrestore(&phb->lock, flags);
}

static void pnv_pci_config_check_eeh(struct pci_dn *pdn)
{
	struct pnv_phb *phb = pdn->phb->private_data;
	u8	fstate = 0;
	__be16	pcierr = 0;
	unsigned int pe_no;
	s64	rc;

	/*
	 * Get the PE#. During the PCI probe stage, we might not
	 * setup that yet. So all ER errors should be mapped to
	 * reserved PE.
	 */
	pe_no = pdn->pe_number;
	if (pe_no == IODA_INVALID_PE) {
		pe_no = phb->ioda.reserved_pe_idx;
	}

	/*
	 * Fetch frozen state. If the PHB support compound PE,
	 * we need handle that case.
	 */
	if (phb->get_pe_state) {
		fstate = phb->get_pe_state(phb, pe_no);
	} else {
		rc = opal_pci_eeh_freeze_status(phb->opal_id,
						pe_no,
						&fstate,
						&pcierr,
						NULL);
		if (rc) {
			pr_warn("%s: Failure %lld getting PHB#%x-PE#%x state\n",
				__func__, rc, phb->hose->global_number, pe_no);
			return;
		}
	}

	pr_devel(" -> EEH check, bdfn=%04x PE#%x fstate=%x\n",
		 (pdn->busno << 8) | (pdn->devfn), pe_no, fstate);

	/* Clear the frozen state if applicable */
	if (fstate == OPAL_EEH_STOPPED_MMIO_FREEZE ||
	    fstate == OPAL_EEH_STOPPED_DMA_FREEZE  ||
	    fstate == OPAL_EEH_STOPPED_MMIO_DMA_FREEZE) {
		/*
		 * If PHB supports compound PE, freeze it for
		 * consistency.
		 */
		if (phb->freeze_pe)
			phb->freeze_pe(phb, pe_no);

		pnv_pci_handle_eeh_config(phb, pe_no);
	}
}

int pnv_pci_cfg_read(struct pci_dn *pdn,
		     int where, int size, u32 *val)
{
	struct pnv_phb *phb = pdn->phb->private_data;
	u32 bdfn = (pdn->busno << 8) | pdn->devfn;
	s64 rc;

	switch (size) {
	case 1: {
		u8 v8;
		rc = opal_pci_config_read_byte(phb->opal_id, bdfn, where, &v8);
		*val = (rc == OPAL_SUCCESS) ? v8 : 0xff;
		break;
	}
	case 2: {
		__be16 v16;
		rc = opal_pci_config_read_half_word(phb->opal_id, bdfn, where,
						   &v16);
		*val = (rc == OPAL_SUCCESS) ? be16_to_cpu(v16) : 0xffff;
		break;
	}
	case 4: {
		__be32 v32;
		rc = opal_pci_config_read_word(phb->opal_id, bdfn, where, &v32);
		*val = (rc == OPAL_SUCCESS) ? be32_to_cpu(v32) : 0xffffffff;
		break;
	}
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	pr_devel("%s: bus: %x devfn: %x +%x/%x -> %08x\n",
		 __func__, pdn->busno, pdn->devfn, where, size, *val);
	return PCIBIOS_SUCCESSFUL;
}

int pnv_pci_cfg_write(struct pci_dn *pdn,
		      int where, int size, u32 val)
{
	struct pnv_phb *phb = pdn->phb->private_data;
	u32 bdfn = (pdn->busno << 8) | pdn->devfn;

	pr_devel("%s: bus: %x devfn: %x +%x/%x -> %08x\n",
		 __func__, pdn->busno, pdn->devfn, where, size, val);
	switch (size) {
	case 1:
		opal_pci_config_write_byte(phb->opal_id, bdfn, where, val);
		break;
	case 2:
		opal_pci_config_write_half_word(phb->opal_id, bdfn, where, val);
		break;
	case 4:
		opal_pci_config_write_word(phb->opal_id, bdfn, where, val);
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	return PCIBIOS_SUCCESSFUL;
}

#if CONFIG_EEH
static bool pnv_pci_cfg_check(struct pci_dn *pdn)
{
	struct eeh_dev *edev = NULL;
	struct pnv_phb *phb = pdn->phb->private_data;

	/* EEH not enabled ? */
	if (!(phb->flags & PNV_PHB_FLAG_EEH))
		return true;

	/* PE reset or device removed ? */
	edev = pdn->edev;
	if (edev) {
		if (edev->pe &&
		    (edev->pe->state & EEH_PE_CFG_BLOCKED))
			return false;

		if (edev->mode & EEH_DEV_REMOVED)
			return false;
	}

	return true;
}
#else
static inline pnv_pci_cfg_check(struct pci_dn *pdn)
{
	return true;
}
#endif /* CONFIG_EEH */

static int pnv_pci_read_config(struct pci_bus *bus,
			       unsigned int devfn,
			       int where, int size, u32 *val)
{
	struct pci_dn *pdn;
	struct pnv_phb *phb;
	int ret;

	*val = 0xFFFFFFFF;
	pdn = pci_get_pdn_by_devfn(bus, devfn);
	if (!pdn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (!pnv_pci_cfg_check(pdn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	ret = pnv_pci_cfg_read(pdn, where, size, val);
	phb = pdn->phb->private_data;
	if (phb->flags & PNV_PHB_FLAG_EEH && pdn->edev) {
		if (*val == EEH_IO_ERROR_VALUE(size) &&
		    eeh_dev_check_failure(pdn->edev))
                        return PCIBIOS_DEVICE_NOT_FOUND;
	} else {
		pnv_pci_config_check_eeh(pdn);
	}

	return ret;
}

static int pnv_pci_write_config(struct pci_bus *bus,
				unsigned int devfn,
				int where, int size, u32 val)
{
	struct pci_dn *pdn;
	struct pnv_phb *phb;
	int ret;

	pdn = pci_get_pdn_by_devfn(bus, devfn);
	if (!pdn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (!pnv_pci_cfg_check(pdn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	ret = pnv_pci_cfg_write(pdn, where, size, val);
	phb = pdn->phb->private_data;
	if (!(phb->flags & PNV_PHB_FLAG_EEH))
		pnv_pci_config_check_eeh(pdn);

	return ret;
}

struct pci_ops pnv_pci_ops = {
	.read  = pnv_pci_read_config,
	.write = pnv_pci_write_config,
};

struct iommu_table *pnv_pci_table_alloc(int nid)
{
	struct iommu_table *tbl;

	tbl = kzalloc_node(sizeof(struct iommu_table), GFP_KERNEL, nid);
	if (!tbl)
		return NULL;

	INIT_LIST_HEAD_RCU(&tbl->it_group_list);
	kref_init(&tbl->it_kref);

	return tbl;
}

void pnv_pci_dma_dev_setup(struct pci_dev *pdev)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
#ifdef CONFIG_PCI_IOV
	struct pnv_ioda_pe *pe;
	struct pci_dn *pdn;

	/* Fix the VF pdn PE number */
	if (pdev->is_virtfn) {
		pdn = pci_get_pdn(pdev);
		WARN_ON(pdn->pe_number != IODA_INVALID_PE);
		list_for_each_entry(pe, &phb->ioda.pe_list, list) {
			if (pe->rid == ((pdev->bus->number << 8) |
			    (pdev->devfn & 0xff))) {
				pdn->pe_number = pe->pe_number;
				pe->pdev = pdev;
				break;
			}
		}
	}
#endif /* CONFIG_PCI_IOV */

	if (phb && phb->dma_dev_setup)
		phb->dma_dev_setup(phb, pdev);
}

void pnv_pci_dma_bus_setup(struct pci_bus *bus)
{
	struct pci_controller *hose = bus->sysdata;
	struct pnv_phb *phb = hose->private_data;
	struct pnv_ioda_pe *pe;

	list_for_each_entry(pe, &phb->ioda.pe_list, list) {
		if (!(pe->flags & (PNV_IODA_PE_BUS | PNV_IODA_PE_BUS_ALL)))
			continue;

		if (!pe->pbus)
			continue;

		if (bus->number == ((pe->rid >> 8) & 0xFF)) {
			pe->pbus = bus;
			break;
		}
	}
}

int pnv_pci_set_p2p(struct pci_dev *initiator, struct pci_dev *target, u64 desc)
{
	struct pci_controller *hose;
	struct pnv_phb *phb_init, *phb_target;
	struct pnv_ioda_pe *pe_init;
	int rc;

	if (!opal_check_token(OPAL_PCI_SET_P2P))
		return -ENXIO;

	hose = pci_bus_to_host(initiator->bus);
	phb_init = hose->private_data;

	hose = pci_bus_to_host(target->bus);
	phb_target = hose->private_data;

	pe_init = pnv_ioda_get_pe(initiator);
	if (!pe_init)
		return -ENODEV;

	/*
	 * Configuring the initiator's PHB requires to adjust its
	 * TVE#1 setting. Since the same device can be an initiator
	 * several times for different target devices, we need to keep
	 * a reference count to know when we can restore the default
	 * bypass setting on its TVE#1 when disabling. Opal is not
	 * tracking PE states, so we add a reference count on the PE
	 * in linux.
	 *
	 * For the target, the configuration is per PHB, so we keep a
	 * target reference count on the PHB.
	 */
	mutex_lock(&p2p_mutex);

	if (desc & OPAL_PCI_P2P_ENABLE) {
		/* always go to opal to validate the configuration */
		rc = opal_pci_set_p2p(phb_init->opal_id, phb_target->opal_id,
				      desc, pe_init->pe_number);

		if (rc != OPAL_SUCCESS) {
			rc = -EIO;
			goto out;
		}

		pe_init->p2p_initiator_count++;
		phb_target->p2p_target_count++;
	} else {
		if (!pe_init->p2p_initiator_count ||
			!phb_target->p2p_target_count) {
			rc = -EINVAL;
			goto out;
		}

		if (--pe_init->p2p_initiator_count == 0)
			pnv_pci_ioda2_set_bypass(pe_init, true);

		if (--phb_target->p2p_target_count == 0) {
			rc = opal_pci_set_p2p(phb_init->opal_id,
					      phb_target->opal_id, desc,
					      pe_init->pe_number);
			if (rc != OPAL_SUCCESS) {
				rc = -EIO;
				goto out;
			}
		}
	}
	rc = 0;
out:
	mutex_unlock(&p2p_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(pnv_pci_set_p2p);

struct device_node *pnv_pci_get_phb_node(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);

	return of_node_get(hose->dn);
}
EXPORT_SYMBOL(pnv_pci_get_phb_node);

int pnv_pci_enable_tunnel(struct pci_dev *dev, u64 *asnind)
{
	struct device_node *np;
	const __be32 *prop;
	struct pnv_ioda_pe *pe;
	uint16_t window_id;
	int rc;

	if (!radix_enabled())
		return -ENXIO;

	if (!(np = pnv_pci_get_phb_node(dev)))
		return -ENXIO;

	prop = of_get_property(np, "ibm,phb-indications", NULL);
	of_node_put(np);

	if (!prop || !prop[1])
		return -ENXIO;

	*asnind = (u64)be32_to_cpu(prop[1]);
	pe = pnv_ioda_get_pe(dev);
	if (!pe)
		return -ENODEV;

	/* Increase real window size to accept as_notify messages. */
	window_id = (pe->pe_number << 1 ) + 1;
	rc = opal_pci_map_pe_dma_window_real(pe->phb->opal_id, pe->pe_number,
					     window_id, pe->tce_bypass_base,
					     (uint64_t)1 << 48);
	return opal_error_code(rc);
}
EXPORT_SYMBOL_GPL(pnv_pci_enable_tunnel);

int pnv_pci_disable_tunnel(struct pci_dev *dev)
{
	struct pnv_ioda_pe *pe;

	pe = pnv_ioda_get_pe(dev);
	if (!pe)
		return -ENODEV;

	/* Restore default real window size. */
	pnv_pci_ioda2_set_bypass(pe, true);
	return 0;
}
EXPORT_SYMBOL_GPL(pnv_pci_disable_tunnel);

int pnv_pci_set_tunnel_bar(struct pci_dev *dev, u64 addr, int enable)
{
	__be64 val;
	struct pci_controller *hose;
	struct pnv_phb *phb;
	u64 tunnel_bar;
	int rc;

	if (!opal_check_token(OPAL_PCI_GET_PBCQ_TUNNEL_BAR))
		return -ENXIO;
	if (!opal_check_token(OPAL_PCI_SET_PBCQ_TUNNEL_BAR))
		return -ENXIO;

	hose = pci_bus_to_host(dev->bus);
	phb = hose->private_data;

	mutex_lock(&tunnel_mutex);
	rc = opal_pci_get_pbcq_tunnel_bar(phb->opal_id, &val);
	if (rc != OPAL_SUCCESS) {
		rc = -EIO;
		goto out;
	}
	tunnel_bar = be64_to_cpu(val);
	if (enable) {
		/*
		* Only one device per PHB can use atomics.
		* Our policy is first-come, first-served.
		*/
		if (tunnel_bar) {
			if (tunnel_bar != addr)
				rc = -EBUSY;
			else
				rc = 0;	/* Setting same address twice is ok */
			goto out;
		}
	} else {
		/*
		* The device that owns atomics and wants to release
		* them must pass the same address with enable == 0.
		*/
		if (tunnel_bar != addr) {
			rc = -EPERM;
			goto out;
		}
		addr = 0x0ULL;
	}
	rc = opal_pci_set_pbcq_tunnel_bar(phb->opal_id, addr);
	rc = opal_error_code(rc);
out:
	mutex_unlock(&tunnel_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(pnv_pci_set_tunnel_bar);

#ifdef CONFIG_PPC64	/* for thread.tidr */
int pnv_pci_get_as_notify_info(struct task_struct *task, u32 *lpid, u32 *pid,
			       u32 *tid)
{
	struct mm_struct *mm = NULL;

	if (task == NULL)
		return -EINVAL;

	mm = get_task_mm(task);
	if (mm == NULL)
		return -EINVAL;

	*pid = mm->context.id;
	mmput(mm);

	*tid = task->thread.tidr;
	*lpid = mfspr(SPRN_LPID);
	return 0;
}
EXPORT_SYMBOL_GPL(pnv_pci_get_as_notify_info);
#endif

void pnv_pci_shutdown(void)
{
	struct pci_controller *hose;

	list_for_each_entry(hose, &hose_list, list_node)
		if (hose->controller_ops.shutdown)
			hose->controller_ops.shutdown(hose);
}

/* Fixup wrong class code in p7ioc and p8 root complex */
static void pnv_p7ioc_rc_quirk(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_IBM, 0x3b9, pnv_p7ioc_rc_quirk);

void __init pnv_pci_init(void)
{
	struct device_node *np;

	pci_add_flags(PCI_CAN_SKIP_ISA_ALIGN);

	/* If we don't have OPAL, eg. in sim, just skip PCI probe */
	if (!firmware_has_feature(FW_FEATURE_OPAL))
		return;

	/* Look for IODA IO-Hubs. */
	for_each_compatible_node(np, NULL, "ibm,ioda-hub") {
		pnv_pci_init_ioda_hub(np);
	}

	/* Look for ioda2 built-in PHB3's */
	for_each_compatible_node(np, NULL, "ibm,ioda2-phb")
		pnv_pci_init_ioda2_phb(np);

	/* Look for ioda3 built-in PHB4's, we treat them as IODA2 */
	for_each_compatible_node(np, NULL, "ibm,ioda3-phb")
		pnv_pci_init_ioda2_phb(np);

	/* Look for NPU PHBs */
	for_each_compatible_node(np, NULL, "ibm,ioda2-npu-phb")
		pnv_pci_init_npu_phb(np);

	/*
	 * Look for NPU2 PHBs which we treat mostly as NPU PHBs with
	 * the exception of TCE kill which requires an OPAL call.
	 */
	for_each_compatible_node(np, NULL, "ibm,ioda2-npu2-phb")
		pnv_pci_init_npu_phb(np);

	/* Look for NPU2 OpenCAPI PHBs */
	for_each_compatible_node(np, NULL, "ibm,ioda2-npu2-opencapi-phb")
		pnv_pci_init_npu2_opencapi_phb(np);

	/* Configure IOMMU DMA hooks */
	set_pci_dma_ops(&dma_iommu_ops);
}

static int pnv_tce_iommu_bus_notifier(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct device *dev = data;
	struct pci_dev *pdev;
	struct pci_dn *pdn;
	struct pnv_ioda_pe *pe;
	struct pci_controller *hose;
	struct pnv_phb *phb;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		pdev = to_pci_dev(dev);
		pdn = pci_get_pdn(pdev);
		hose = pci_bus_to_host(pdev->bus);
		phb = hose->private_data;

		WARN_ON_ONCE(!phb);
		if (!pdn || pdn->pe_number == IODA_INVALID_PE || !phb)
			return 0;

		pe = &phb->ioda.pe_array[pdn->pe_number];
		if (!pe->table_group.group)
			return 0;
		iommu_add_device(&pe->table_group, dev);
		return 0;
	case BUS_NOTIFY_DEL_DEVICE:
		iommu_del_device(dev);
		return 0;
	default:
		return 0;
	}
}

static struct notifier_block pnv_tce_iommu_bus_nb = {
	.notifier_call = pnv_tce_iommu_bus_notifier,
};

static int __init pnv_tce_iommu_bus_notifier_init(void)
{
	bus_register_notifier(&pci_bus_type, &pnv_tce_iommu_bus_nb);
	return 0;
}
machine_subsys_initcall_sync(powernv, pnv_tce_iommu_bus_notifier_init);
