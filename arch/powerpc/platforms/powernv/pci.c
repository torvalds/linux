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

static void pnv_pci_dump_p7ioc_diag_data(struct pnv_phb *phb)
{
	struct OpalIoP7IOCPhbErrorData *data = &phb->diag.p7ioc;
	int i;

	pr_info("PHB %d diagnostic data:\n", phb->hose->global_number);

	pr_info("  brdgCtl              = 0x%08x\n", data->brdgCtl);

	pr_info("  portStatusReg        = 0x%08x\n", data->portStatusReg);
	pr_info("  rootCmplxStatus      = 0x%08x\n", data->rootCmplxStatus);
	pr_info("  busAgentStatus       = 0x%08x\n", data->busAgentStatus);

	pr_info("  deviceStatus         = 0x%08x\n", data->deviceStatus);
	pr_info("  slotStatus           = 0x%08x\n", data->slotStatus);
	pr_info("  linkStatus           = 0x%08x\n", data->linkStatus);
	pr_info("  devCmdStatus         = 0x%08x\n", data->devCmdStatus);
	pr_info("  devSecStatus         = 0x%08x\n", data->devSecStatus);

	pr_info("  rootErrorStatus      = 0x%08x\n", data->rootErrorStatus);
	pr_info("  uncorrErrorStatus    = 0x%08x\n", data->uncorrErrorStatus);
	pr_info("  corrErrorStatus      = 0x%08x\n", data->corrErrorStatus);
	pr_info("  tlpHdr1              = 0x%08x\n", data->tlpHdr1);
	pr_info("  tlpHdr2              = 0x%08x\n", data->tlpHdr2);
	pr_info("  tlpHdr3              = 0x%08x\n", data->tlpHdr3);
	pr_info("  tlpHdr4              = 0x%08x\n", data->tlpHdr4);
	pr_info("  sourceId             = 0x%08x\n", data->sourceId);

	pr_info("  errorClass           = 0x%016llx\n", data->errorClass);
	pr_info("  correlator           = 0x%016llx\n", data->correlator);

	pr_info("  p7iocPlssr           = 0x%016llx\n", data->p7iocPlssr);
	pr_info("  p7iocCsr             = 0x%016llx\n", data->p7iocCsr);
	pr_info("  lemFir               = 0x%016llx\n", data->lemFir);
	pr_info("  lemErrorMask         = 0x%016llx\n", data->lemErrorMask);
	pr_info("  lemWOF               = 0x%016llx\n", data->lemWOF);
	pr_info("  phbErrorStatus       = 0x%016llx\n", data->phbErrorStatus);
	pr_info("  phbFirstErrorStatus  = 0x%016llx\n", data->phbFirstErrorStatus);
	pr_info("  phbErrorLog0         = 0x%016llx\n", data->phbErrorLog0);
	pr_info("  phbErrorLog1         = 0x%016llx\n", data->phbErrorLog1);
	pr_info("  mmioErrorStatus      = 0x%016llx\n", data->mmioErrorStatus);
	pr_info("  mmioFirstErrorStatus = 0x%016llx\n", data->mmioFirstErrorStatus);
	pr_info("  mmioErrorLog0        = 0x%016llx\n", data->mmioErrorLog0);
	pr_info("  mmioErrorLog1        = 0x%016llx\n", data->mmioErrorLog1);
	pr_info("  dma0ErrorStatus      = 0x%016llx\n", data->dma0ErrorStatus);
	pr_info("  dma0FirstErrorStatus = 0x%016llx\n", data->dma0FirstErrorStatus);
	pr_info("  dma0ErrorLog0        = 0x%016llx\n", data->dma0ErrorLog0);
	pr_info("  dma0ErrorLog1        = 0x%016llx\n", data->dma0ErrorLog1);
	pr_info("  dma1ErrorStatus      = 0x%016llx\n", data->dma1ErrorStatus);
	pr_info("  dma1FirstErrorStatus = 0x%016llx\n", data->dma1FirstErrorStatus);
	pr_info("  dma1ErrorLog0        = 0x%016llx\n", data->dma1ErrorLog0);
	pr_info("  dma1ErrorLog1        = 0x%016llx\n", data->dma1ErrorLog1);

	for (i = 0; i < OPAL_P7IOC_NUM_PEST_REGS; i++) {
		if ((data->pestA[i] >> 63) == 0 &&
		    (data->pestB[i] >> 63) == 0)
			continue;
		pr_info("  PE[%3d] PESTA        = 0x%016llx\n", i, data->pestA[i]);
		pr_info("          PESTB        = 0x%016llx\n", data->pestB[i]);
	}
}

static void pnv_pci_dump_phb_diag_data(struct pnv_phb *phb)
{
	switch(phb->model) {
	case PNV_PHB_MODEL_P7IOC:
		pnv_pci_dump_p7ioc_diag_data(phb);
		break;
	default:
		pr_warning("PCI %d: Can't decode this PHB diag data\n",
			   phb->hose->global_number);
	}
}

static void pnv_pci_handle_eeh_config(struct pnv_phb *phb, u32 pe_no)
{
	unsigned long flags, rc;
	int has_diag;

	spin_lock_irqsave(&phb->lock, flags);

	rc = opal_pci_get_phb_diag_data(phb->opal_id, phb->diag.blob, PNV_PCI_DIAG_BUF_SIZE);
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
			pnv_pci_dump_phb_diag_data(phb);
		else
			pr_warning("PCI %d: No diag data available\n",
				   phb->hose->global_number);
	}

	spin_unlock_irqrestore(&phb->lock, flags);
}

static void pnv_pci_config_check_eeh(struct pnv_phb *phb, struct pci_bus *bus,
				     u32 bdfn)
{
	s64	rc;
	u8	fstate;
	u16	pcierr;
	u32	pe_no;

	/* Get PE# if we support IODA */
	pe_no = phb->bdfn_to_pe ? phb->bdfn_to_pe(phb, bus, bdfn & 0xff) : 0;

	/* Read freeze status */
	rc = opal_pci_eeh_freeze_status(phb->opal_id, pe_no, &fstate, &pcierr,
					NULL);
	if (rc) {
		pr_warning("PCI %d: Failed to read EEH status for PE#%d,"
			   " err %lld\n", phb->hose->global_number, pe_no, rc);
		return;
	}
	cfg_dbg(" -> EEH check, bdfn=%04x PE%d fstate=%x\n",
		bdfn, pe_no, fstate);
	if (fstate != 0)
		pnv_pci_handle_eeh_config(phb, pe_no);
}

static int pnv_pci_read_config(struct pci_bus *bus,
			       unsigned int devfn,
			       int where, int size, u32 *val)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pnv_phb *phb = hose->private_data;
	u32 bdfn = (((uint64_t)bus->number) << 8) | devfn;
	s64 rc;

	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1: {
		u8 v8;
		rc = opal_pci_config_read_byte(phb->opal_id, bdfn, where, &v8);
		*val = (rc == OPAL_SUCCESS) ? v8 : 0xff;
		break;
	}
	case 2: {
		u16 v16;
		rc = opal_pci_config_read_half_word(phb->opal_id, bdfn, where,
						   &v16);
		*val = (rc == OPAL_SUCCESS) ? v16 : 0xffff;
		break;
	}
	case 4: {
		u32 v32;
		rc = opal_pci_config_read_word(phb->opal_id, bdfn, where, &v32);
		*val = (rc == OPAL_SUCCESS) ? v32 : 0xffffffff;
		break;
	}
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}
	cfg_dbg("pnv_pci_read_config bus: %x devfn: %x +%x/%x -> %08x\n",
		bus->number, devfn, where, size, *val);

	/* Check if the PHB got frozen due to an error (no response) */
	pnv_pci_config_check_eeh(phb, bus, bdfn);

	return PCIBIOS_SUCCESSFUL;
}

static int pnv_pci_write_config(struct pci_bus *bus,
				unsigned int devfn,
				int where, int size, u32 val)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pnv_phb *phb = hose->private_data;
	u32 bdfn = (((uint64_t)bus->number) << 8) | devfn;

	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	cfg_dbg("pnv_pci_write_config bus: %x devfn: %x +%x/%x -> %08x\n",
		bus->number, devfn, where, size, val);
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
	/* Check if the PHB got frozen due to an error (no response) */
	pnv_pci_config_check_eeh(phb, bus, bdfn);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops pnv_pci_ops = {
	.read = pnv_pci_read_config,
	.write = pnv_pci_write_config,
};

static int pnv_tce_build(struct iommu_table *tbl, long index, long npages,
			 unsigned long uaddr, enum dma_data_direction direction,
			 struct dma_attrs *attrs)
{
	u64 proto_tce;
	u64 *tcep, *tces;
	u64 rpn;

	proto_tce = TCE_PCI_READ; // Read allowed

	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	tces = tcep = ((u64 *)tbl->it_base) + index - tbl->it_offset;
	rpn = __pa(uaddr) >> TCE_SHIFT;

	while (npages--)
		*(tcep++) = proto_tce | (rpn++ << TCE_RPN_SHIFT);

	/* Some implementations won't cache invalid TCEs and thus may not
	 * need that flush. We'll probably turn it_type into a bit mask
	 * of flags if that becomes the case
	 */
	if (tbl->it_type & TCE_PCI_SWINV_CREATE)
		pnv_pci_ioda_tce_invalidate(tbl, tces, tcep - 1);

	return 0;
}

static void pnv_tce_free(struct iommu_table *tbl, long index, long npages)
{
	u64 *tcep, *tces;

	tces = tcep = ((u64 *)tbl->it_base) + index - tbl->it_offset;

	while (npages--)
		*(tcep++) = 0;

	if (tbl->it_type & TCE_PCI_SWINV_FREE)
		pnv_pci_ioda_tce_invalidate(tbl, tces, tcep - 1);
}

static unsigned long pnv_tce_get(struct iommu_table *tbl, long index)
{
	return ((u64 *)tbl->it_base)[index - tbl->it_offset];
}

void pnv_pci_setup_iommu_table(struct iommu_table *tbl,
			       void *tce_mem, u64 tce_size,
			       u64 dma_offset)
{
	tbl->it_blocksize = 16;
	tbl->it_base = (unsigned long)tce_mem;
	tbl->it_offset = dma_offset >> IOMMU_PAGE_SHIFT;
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
				  be32_to_cpup(sizep), 0);
	iommu_init_table(tbl, hose->node);

	/* Deal with SW invalidated TCEs when needed (BML way) */
	swinvp = of_get_property(hose->dn, "linux,tce-sw-invalidate-info",
				 NULL);
	if (swinvp) {
		tbl->it_busno = swinvp[1];
		tbl->it_index = (unsigned long)ioremap(swinvp[0], 8);
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
	set_iommu_table_base(&pdev->dev, pdn->iommu_table);
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
	ppc_md.tce_build = pnv_tce_build;
	ppc_md.tce_free = pnv_tce_free;
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
