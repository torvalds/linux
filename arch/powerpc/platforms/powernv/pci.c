/*
 * Support PCI/PCIe on PowerNV platforms
 *
 * Currently supports only P5IOC2
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
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/msi.h>
#include <linux/iommu.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/msi_bitmap.h>
#include <asm/ppc-pci.h>
#include <asm/opal.h>
#include <asm/iommu.h>
#include <asm/tce.h>
#include <asm/firmware.h>
#include <asm/eeh_event.h>
#include <asm/eeh.h>

#include "powernv.h"
#include "pci.h"

/* Delay in usec */
#define PCI_RESET_DELAY_US	3000000

#define cfg_dbg(fmt...)	do { } while(0)
//#define cfg_dbg(fmt...)	printk(fmt)

#ifdef CONFIG_PCI_MSI
static int pnv_msi_check_device(struct pci_dev* pdev, int nvec, int type)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct pci_dn *pdn = pci_get_pdn(pdev);

	if (pdn && pdn->force_32bit_msi && !phb->msi32_support)
		return -ENODEV;

	return (phb && phb->msi_bmp.bitmap) ? 0 : -ENODEV;
}

static int pnv_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct msi_desc *entry;
	struct msi_msg msg;
	int hwirq;
	unsigned int virq;
	int rc;

	if (WARN_ON(!phb))
		return -ENODEV;

	list_for_each_entry(entry, &pdev->msi_list, list) {
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
		if (virq == NO_IRQ) {
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
		write_msi_msg(virq, &msg);
	}
	return 0;
}

static void pnv_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct msi_desc *entry;

	if (WARN_ON(!phb))
		return;

	list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;
		irq_set_msi_desc(entry->irq, NULL);
		msi_bitmap_free_hwirqs(&phb->msi_bmp,
			virq_to_hw(entry->irq) - phb->msi_base, 1);
		irq_dispose_mapping(entry->irq);
	}
}
#endif /* CONFIG_PCI_MSI */

static void pnv_pci_dump_p7ioc_diag_data(struct pci_controller *hose,
					 struct OpalIoPhbErrorCommon *common)
{
	struct OpalIoP7IOCPhbErrorData *data;
	int i;

	data = (struct OpalIoP7IOCPhbErrorData *)common;
	pr_info("P7IOC PHB#%d Diag-data (Version: %d)\n",
		hose->global_number, common->version);

	if (data->brdgCtl)
		pr_info("brdgCtl:     %08x\n",
			data->brdgCtl);
	if (data->portStatusReg || data->rootCmplxStatus ||
	    data->busAgentStatus)
		pr_info("UtlSts:      %08x %08x %08x\n",
			data->portStatusReg, data->rootCmplxStatus,
			data->busAgentStatus);
	if (data->deviceStatus || data->slotStatus   ||
	    data->linkStatus   || data->devCmdStatus ||
	    data->devSecStatus)
		pr_info("RootSts:     %08x %08x %08x %08x %08x\n",
			data->deviceStatus, data->slotStatus,
			data->linkStatus, data->devCmdStatus,
			data->devSecStatus);
	if (data->rootErrorStatus   || data->uncorrErrorStatus ||
	    data->corrErrorStatus)
		pr_info("RootErrSts:  %08x %08x %08x\n",
			data->rootErrorStatus, data->uncorrErrorStatus,
			data->corrErrorStatus);
	if (data->tlpHdr1 || data->tlpHdr2 ||
	    data->tlpHdr3 || data->tlpHdr4)
		pr_info("RootErrLog:  %08x %08x %08x %08x\n",
			data->tlpHdr1, data->tlpHdr2,
			data->tlpHdr3, data->tlpHdr4);
	if (data->sourceId || data->errorClass ||
	    data->correlator)
		pr_info("RootErrLog1: %08x %016llx %016llx\n",
			data->sourceId, data->errorClass,
			data->correlator);
	if (data->p7iocPlssr || data->p7iocCsr)
		pr_info("PhbSts:      %016llx %016llx\n",
			data->p7iocPlssr, data->p7iocCsr);
	if (data->lemFir)
		pr_info("Lem:         %016llx %016llx %016llx\n",
			data->lemFir, data->lemErrorMask,
			data->lemWOF);
	if (data->phbErrorStatus)
		pr_info("PhbErr:      %016llx %016llx %016llx %016llx\n",
			data->phbErrorStatus, data->phbFirstErrorStatus,
			data->phbErrorLog0, data->phbErrorLog1);
	if (data->mmioErrorStatus)
		pr_info("OutErr:      %016llx %016llx %016llx %016llx\n",
			data->mmioErrorStatus, data->mmioFirstErrorStatus,
			data->mmioErrorLog0, data->mmioErrorLog1);
	if (data->dma0ErrorStatus)
		pr_info("InAErr:      %016llx %016llx %016llx %016llx\n",
			data->dma0ErrorStatus, data->dma0FirstErrorStatus,
			data->dma0ErrorLog0, data->dma0ErrorLog1);
	if (data->dma1ErrorStatus)
		pr_info("InBErr:      %016llx %016llx %016llx %016llx\n",
			data->dma1ErrorStatus, data->dma1FirstErrorStatus,
			data->dma1ErrorLog0, data->dma1ErrorLog1);

	for (i = 0; i < OPAL_P7IOC_NUM_PEST_REGS; i++) {
		if ((data->pestA[i] >> 63) == 0 &&
		    (data->pestB[i] >> 63) == 0)
			continue;

		pr_info("PE[%3d] A/B: %016llx %016llx\n",
			i, data->pestA[i], data->pestB[i]);
	}
}

static void pnv_pci_dump_phb3_diag_data(struct pci_controller *hose,
					struct OpalIoPhbErrorCommon *common)
{
	struct OpalIoPhb3ErrorData *data;
	int i;

	data = (struct OpalIoPhb3ErrorData*)common;
	pr_info("PHB3 PHB#%d Diag-data (Version: %d)\n",
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

	for (i = 0; i < OPAL_PHB3_NUM_PEST_REGS; i++) {
		if ((be64_to_cpu(data->pestA[i]) >> 63) == 0 &&
		    (be64_to_cpu(data->pestB[i]) >> 63) == 0)
			continue;

		pr_info("PE[%3d] A/B: %016llx %016llx\n",
				i, be64_to_cpu(data->pestA[i]),
				be64_to_cpu(data->pestB[i]));
	}
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
	default:
		pr_warn("%s: Unrecognized ioType %d\n",
			__func__, be32_to_cpu(common->ioType));
	}
}

static void pnv_pci_handle_eeh_config(struct pnv_phb *phb, u32 pe_no)
{
	unsigned long flags, rc;
	int has_diag;

	spin_lock_irqsave(&phb->lock, flags);

	rc = opal_pci_get_phb_diag_data2(phb->opal_id, phb->diag.blob,
					 PNV_PCI_DIAG_BUF_SIZE);
	has_diag = (rc == OPAL_SUCCESS);

	rc = opal_pci_eeh_freeze_clear(phb->opal_id, pe_no,
				       OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
	if (rc) {
		pr_warning("PCI %d: Failed to clear EEH freeze state"
			   " for PE#%d, err %ld\n",
			   phb->hose->global_number, pe_no, rc);

		/* For now, let's only display the diag buffer when we fail to clear
		 * the EEH status. We'll do more sensible things later when we have
		 * proper EEH support. We need to make sure we don't pollute ourselves
		 * with the normal errors generated when probing empty slots
		 */
		if (has_diag)
			pnv_pci_dump_phb_diag_data(phb->hose, phb->diag.blob);
		else
			pr_warning("PCI %d: No diag data available\n",
				   phb->hose->global_number);
	}

	spin_unlock_irqrestore(&phb->lock, flags);
}

static void pnv_pci_config_check_eeh(struct pnv_phb *phb,
				     struct device_node *dn)
{
	s64	rc;
	u8	fstate;
	__be16	pcierr;
	u32	pe_no;

	/*
	 * Get the PE#. During the PCI probe stage, we might not
	 * setup that yet. So all ER errors should be mapped to
	 * reserved PE.
	 */
	pe_no = PCI_DN(dn)->pe_number;
	if (pe_no == IODA_INVALID_PE) {
		if (phb->type == PNV_PHB_P5IOC2)
			pe_no = 0;
		else
			pe_no = phb->ioda.reserved_pe;
	}

	/* Read freeze status */
	rc = opal_pci_eeh_freeze_status(phb->opal_id, pe_no, &fstate, &pcierr,
					NULL);
	if (rc) {
		pr_warning("%s: Can't read EEH status (PE#%d) for "
			   "%s, err %lld\n",
			   __func__, pe_no, dn->full_name, rc);
		return;
	}
	cfg_dbg(" -> EEH check, bdfn=%04x PE#%d fstate=%x\n",
		(PCI_DN(dn)->busno << 8) | (PCI_DN(dn)->devfn),
		pe_no, fstate);
	if (fstate != 0)
		pnv_pci_handle_eeh_config(phb, pe_no);
}

int pnv_pci_cfg_read(struct device_node *dn,
		     int where, int size, u32 *val)
{
	struct pci_dn *pdn = PCI_DN(dn);
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

	cfg_dbg("%s: bus: %x devfn: %x +%x/%x -> %08x\n",
		__func__, pdn->busno, pdn->devfn, where, size, *val);
	return PCIBIOS_SUCCESSFUL;
}

int pnv_pci_cfg_write(struct device_node *dn,
		      int where, int size, u32 val)
{
	struct pci_dn *pdn = PCI_DN(dn);
	struct pnv_phb *phb = pdn->phb->private_data;
	u32 bdfn = (pdn->busno << 8) | pdn->devfn;

	cfg_dbg("%s: bus: %x devfn: %x +%x/%x -> %08x\n",
		pdn->busno, pdn->devfn, where, size, val);
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
static bool pnv_pci_cfg_check(struct pci_controller *hose,
			      struct device_node *dn)
{
	struct eeh_dev *edev = NULL;
	struct pnv_phb *phb = hose->private_data;

	/* EEH not enabled ? */
	if (!(phb->flags & PNV_PHB_FLAG_EEH))
		return true;

	/* PE reset or device removed ? */
	edev = of_node_to_eeh_dev(dn);
	if (edev) {
		if (edev->pe &&
		    (edev->pe->state & EEH_PE_RESET))
			return false;

		if (edev->mode & EEH_DEV_REMOVED)
			return false;
	}

	return true;
}
#else
static inline pnv_pci_cfg_check(struct pci_controller *hose,
				struct device_node *dn)
{
	return true;
}
#endif /* CONFIG_EEH */

static int pnv_pci_read_config(struct pci_bus *bus,
			       unsigned int devfn,
			       int where, int size, u32 *val)
{
	struct device_node *dn, *busdn = pci_bus_to_OF_node(bus);
	struct pci_dn *pdn;
	struct pnv_phb *phb;
	bool found = false;
	int ret;

	*val = 0xFFFFFFFF;
	for (dn = busdn->child; dn; dn = dn->sibling) {
		pdn = PCI_DN(dn);
		if (pdn && pdn->devfn == devfn) {
			phb = pdn->phb->private_data;
			found = true;
			break;
		}
	}

	if (!found || !pnv_pci_cfg_check(pdn->phb, dn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	ret = pnv_pci_cfg_read(dn, where, size, val);
	if (phb->flags & PNV_PHB_FLAG_EEH) {
		if (*val == EEH_IO_ERROR_VALUE(size) &&
		    eeh_dev_check_failure(of_node_to_eeh_dev(dn)))
                        return PCIBIOS_DEVICE_NOT_FOUND;
	} else {
		pnv_pci_config_check_eeh(phb, dn);
	}

	return ret;
}

static int pnv_pci_write_config(struct pci_bus *bus,
				unsigned int devfn,
				int where, int size, u32 val)
{
	struct device_node *dn, *busdn = pci_bus_to_OF_node(bus);
	struct pci_dn *pdn;
	struct pnv_phb *phb;
	bool found = false;
	int ret;

	for (dn = busdn->child; dn; dn = dn->sibling) {
		pdn = PCI_DN(dn);
		if (pdn && pdn->devfn == devfn) {
			phb = pdn->phb->private_data;
			found = true;
			break;
		}
	}

	if (!found || !pnv_pci_cfg_check(pdn->phb, dn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	ret = pnv_pci_cfg_write(dn, where, size, val);
	if (!(phb->flags & PNV_PHB_FLAG_EEH))
		pnv_pci_config_check_eeh(phb, dn);

	return ret;
}

struct pci_ops pnv_pci_ops = {
	.read  = pnv_pci_read_config,
	.write = pnv_pci_write_config,
};

static int pnv_tce_build(struct iommu_table *tbl, long index, long npages,
			 unsigned long uaddr, enum dma_data_direction direction,
			 struct dma_attrs *attrs, bool rm)
{
	u64 proto_tce;
	__be64 *tcep, *tces;
	u64 rpn;

	proto_tce = TCE_PCI_READ; // Read allowed

	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	tces = tcep = ((__be64 *)tbl->it_base) + index - tbl->it_offset;
	rpn = __pa(uaddr) >> tbl->it_page_shift;

	while (npages--)
		*(tcep++) = cpu_to_be64(proto_tce |
				(rpn++ << tbl->it_page_shift));

	/* Some implementations won't cache invalid TCEs and thus may not
	 * need that flush. We'll probably turn it_type into a bit mask
	 * of flags if that becomes the case
	 */
	if (tbl->it_type & TCE_PCI_SWINV_CREATE)
		pnv_pci_ioda_tce_invalidate(tbl, tces, tcep - 1, rm);

	return 0;
}

static int pnv_tce_build_vm(struct iommu_table *tbl, long index, long npages,
			    unsigned long uaddr,
			    enum dma_data_direction direction,
			    struct dma_attrs *attrs)
{
	return pnv_tce_build(tbl, index, npages, uaddr, direction, attrs,
			false);
}

static void pnv_tce_free(struct iommu_table *tbl, long index, long npages,
		bool rm)
{
	__be64 *tcep, *tces;

	tces = tcep = ((__be64 *)tbl->it_base) + index - tbl->it_offset;

	while (npages--)
		*(tcep++) = cpu_to_be64(0);

	if (tbl->it_type & TCE_PCI_SWINV_FREE)
		pnv_pci_ioda_tce_invalidate(tbl, tces, tcep - 1, rm);
}

static void pnv_tce_free_vm(struct iommu_table *tbl, long index, long npages)
{
	pnv_tce_free(tbl, index, npages, false);
}

static unsigned long pnv_tce_get(struct iommu_table *tbl, long index)
{
	return ((u64 *)tbl->it_base)[index - tbl->it_offset];
}

static int pnv_tce_build_rm(struct iommu_table *tbl, long index, long npages,
			    unsigned long uaddr,
			    enum dma_data_direction direction,
			    struct dma_attrs *attrs)
{
	return pnv_tce_build(tbl, index, npages, uaddr, direction, attrs, true);
}

static void pnv_tce_free_rm(struct iommu_table *tbl, long index, long npages)
{
	pnv_tce_free(tbl, index, npages, true);
}

void pnv_pci_setup_iommu_table(struct iommu_table *tbl,
			       void *tce_mem, u64 tce_size,
			       u64 dma_offset, unsigned page_shift)
{
	tbl->it_blocksize = 16;
	tbl->it_base = (unsigned long)tce_mem;
	tbl->it_page_shift = page_shift;
	tbl->it_offset = dma_offset >> tbl->it_page_shift;
	tbl->it_index = 0;
	tbl->it_size = tce_size >> 3;
	tbl->it_busno = 0;
	tbl->it_type = TCE_PCI;
}

static struct iommu_table *pnv_pci_setup_bml_iommu(struct pci_controller *hose)
{
	struct iommu_table *tbl;
	const __be64 *basep, *swinvp;
	const __be32 *sizep;

	basep = of_get_property(hose->dn, "linux,tce-base", NULL);
	sizep = of_get_property(hose->dn, "linux,tce-size", NULL);
	if (basep == NULL || sizep == NULL) {
		pr_err("PCI: %s has missing tce entries !\n",
		       hose->dn->full_name);
		return NULL;
	}
	tbl = kzalloc_node(sizeof(struct iommu_table), GFP_KERNEL, hose->node);
	if (WARN_ON(!tbl))
		return NULL;
	pnv_pci_setup_iommu_table(tbl, __va(be64_to_cpup(basep)),
				  be32_to_cpup(sizep), 0, IOMMU_PAGE_SHIFT_4K);
	iommu_init_table(tbl, hose->node);
	iommu_register_group(tbl, pci_domain_nr(hose->bus), 0);

	/* Deal with SW invalidated TCEs when needed (BML way) */
	swinvp = of_get_property(hose->dn, "linux,tce-sw-invalidate-info",
				 NULL);
	if (swinvp) {
		tbl->it_busno = be64_to_cpu(swinvp[1]);
		tbl->it_index = (unsigned long)ioremap(be64_to_cpup(swinvp), 8);
		tbl->it_type = TCE_PCI_SWINV_CREATE | TCE_PCI_SWINV_FREE;
	}
	return tbl;
}

static void pnv_pci_dma_fallback_setup(struct pci_controller *hose,
				       struct pci_dev *pdev)
{
	struct device_node *np = pci_bus_to_OF_node(hose->bus);
	struct pci_dn *pdn;

	if (np == NULL)
		return;
	pdn = PCI_DN(np);
	if (!pdn->iommu_table)
		pdn->iommu_table = pnv_pci_setup_bml_iommu(hose);
	if (!pdn->iommu_table)
		return;
	set_iommu_table_base_and_group(&pdev->dev, pdn->iommu_table);
}

static void pnv_pci_dma_dev_setup(struct pci_dev *pdev)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;

	/* If we have no phb structure, try to setup a fallback based on
	 * the device-tree (RTAS PCI for example)
	 */
	if (phb && phb->dma_dev_setup)
		phb->dma_dev_setup(phb, pdev);
	else
		pnv_pci_dma_fallback_setup(hose, pdev);
}

int pnv_pci_dma_set_mask(struct pci_dev *pdev, u64 dma_mask)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;

	if (phb && phb->dma_set_mask)
		return phb->dma_set_mask(phb, pdev, dma_mask);
	return __dma_set_mask(&pdev->dev, dma_mask);
}

void pnv_pci_shutdown(void)
{
	struct pci_controller *hose;

	list_for_each_entry(hose, &hose_list, list_node) {
		struct pnv_phb *phb = hose->private_data;

		if (phb && phb->shutdown)
			phb->shutdown(phb);
	}
}

/* Fixup wrong class code in p7ioc and p8 root complex */
static void pnv_p7ioc_rc_quirk(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_IBM, 0x3b9, pnv_p7ioc_rc_quirk);

static int pnv_pci_probe_mode(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	const __be64 *tstamp;
	u64 now, target;


	/* We hijack this as a way to ensure we have waited long
	 * enough since the reset was lifted on the PCI bus
	 */
	if (bus != hose->bus)
		return PCI_PROBE_NORMAL;
	tstamp = of_get_property(hose->dn, "reset-clear-timestamp", NULL);
	if (!tstamp || !*tstamp)
		return PCI_PROBE_NORMAL;

	now = mftb() / tb_ticks_per_usec;
	target = (be64_to_cpup(tstamp) / tb_ticks_per_usec)
		+ PCI_RESET_DELAY_US;

	pr_devel("pci %04d: Reset target: 0x%llx now: 0x%llx\n",
		 hose->global_number, target, now);

	if (now < target)
		msleep((target - now + 999) / 1000);

	return PCI_PROBE_NORMAL;
}

void __init pnv_pci_init(void)
{
	struct device_node *np;

	pci_add_flags(PCI_CAN_SKIP_ISA_ALIGN);

	/* OPAL absent, try POPAL first then RTAS detection of PHBs */
	if (!firmware_has_feature(FW_FEATURE_OPAL)) {
#ifdef CONFIG_PPC_POWERNV_RTAS
		init_pci_config_tokens();
		find_and_init_phbs();
#endif /* CONFIG_PPC_POWERNV_RTAS */
	}
	/* OPAL is here, do our normal stuff */
	else {
		int found_ioda = 0;

		/* Look for IODA IO-Hubs. We don't support mixing IODA
		 * and p5ioc2 due to the need to change some global
		 * probing flags
		 */
		for_each_compatible_node(np, NULL, "ibm,ioda-hub") {
			pnv_pci_init_ioda_hub(np);
			found_ioda = 1;
		}

		/* Look for p5ioc2 IO-Hubs */
		if (!found_ioda)
			for_each_compatible_node(np, NULL, "ibm,p5ioc2")
				pnv_pci_init_p5ioc2_hub(np);

		/* Look for ioda2 built-in PHB3's */
		for_each_compatible_node(np, NULL, "ibm,ioda2-phb")
			pnv_pci_init_ioda2_phb(np);
	}

	/* Setup the linkage between OF nodes and PHBs */
	pci_devs_phb_init();

	/* Configure IOMMU DMA hooks */
	ppc_md.pci_dma_dev_setup = pnv_pci_dma_dev_setup;
	ppc_md.tce_build = pnv_tce_build_vm;
	ppc_md.tce_free = pnv_tce_free_vm;
	ppc_md.tce_build_rm = pnv_tce_build_rm;
	ppc_md.tce_free_rm = pnv_tce_free_rm;
	ppc_md.tce_get = pnv_tce_get;
	ppc_md.pci_probe_mode = pnv_pci_probe_mode;
	set_pci_dma_ops(&dma_iommu_ops);

	/* Configure MSIs */
#ifdef CONFIG_PCI_MSI
	ppc_md.msi_check_device = pnv_msi_check_device;
	ppc_md.setup_msi_irqs = pnv_setup_msi_irqs;
	ppc_md.teardown_msi_irqs = pnv_teardown_msi_irqs;
#endif
}

static int tce_iommu_bus_notifier(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct device *dev = data;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		return iommu_add_device(dev);
	case BUS_NOTIFY_DEL_DEVICE:
		if (dev->iommu_group)
			iommu_del_device(dev);
		return 0;
	default:
		return 0;
	}
}

static struct notifier_block tce_iommu_bus_nb = {
	.notifier_call = tce_iommu_bus_notifier,
};

static int __init tce_iommu_bus_notifier_init(void)
{
	bus_register_notifier(&pci_bus_type, &tce_iommu_bus_nb);
	return 0;
}

subsys_initcall_sync(tce_iommu_bus_notifier_init);
