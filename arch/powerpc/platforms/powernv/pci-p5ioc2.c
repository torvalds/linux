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

#include "powernv.h"
#include "pci.h"

/* For now, use a fixed amount of TCE memory for each p5ioc2
 * hub, 16M will do
 */
#define P5IOC2_TCE_MEMORY	0x01000000

#ifdef CONFIG_PCI_MSI
static int pnv_pci_p5ioc2_msi_setup(struct pnv_phb *phb, struct pci_dev *dev,
				    unsigned int hwirq, unsigned int is_64,
				    struct msi_msg *msg)
{
	if (WARN_ON(!is_64))
		return -ENXIO;
	msg->data = hwirq - phb->msi_base;
	msg->address_hi = 0x10000000;
	msg->address_lo = 0;

	return 0;
}

static void pnv_pci_init_p5ioc2_msis(struct pnv_phb *phb)
{
	unsigned int count;
	const __be32 *prop = of_get_property(phb->hose->dn,
					     "ibm,opal-msi-ranges", NULL);
	if (!prop)
		return;

	/* Don't do MSI's on p5ioc2 PCI-X are they are not properly
	 * verified in HW
	 */
	if (of_device_is_compatible(phb->hose->dn, "ibm,p5ioc2-pcix"))
		return;
	phb->msi_base = be32_to_cpup(prop);
	count = be32_to_cpup(prop + 1);
	if (msi_bitmap_alloc(&phb->msi_bmp, count, phb->hose->dn)) {
		pr_err("PCI %d: Failed to allocate MSI bitmap !\n",
		       phb->hose->global_number);
		return;
	}
	phb->msi_setup = pnv_pci_p5ioc2_msi_setup;
	phb->msi32_support = 0;
	pr_info(" Allocated bitmap for %d MSIs (base IRQ 0x%x)\n",
		count, phb->msi_base);
}
#else
static void pnv_pci_init_p5ioc2_msis(struct pnv_phb *phb) { }
#endif /* CONFIG_PCI_MSI */

static void pnv_pci_p5ioc2_dma_dev_setup(struct pnv_phb *phb,
					 struct pci_dev *pdev)
{
	if (phb->p5ioc2.iommu_table.it_map == NULL)
		iommu_init_table(&phb->p5ioc2.iommu_table, phb->hose->node);

	set_iommu_table_base(&pdev->dev, &phb->p5ioc2.iommu_table);
}

static void __init pnv_pci_init_p5ioc2_phb(struct device_node *np,
					   void *tce_mem, u64 tce_size)
{
	struct pnv_phb *phb;
	const u64 *prop64;
	u64 phb_id;
	int64_t rc;
	static int primary = 1;

	pr_info(" Initializing p5ioc2 PHB %s\n", np->full_name);

	prop64 = of_get_property(np, "ibm,opal-phbid", NULL);
	if (!prop64) {
		pr_err("  Missing \"ibm,opal-phbid\" property !\n");
		return;
	}
	phb_id = be64_to_cpup(prop64);
	pr_devel("  PHB-ID  : 0x%016llx\n", phb_id);
	pr_devel("  TCE AT  : 0x%016lx\n", __pa(tce_mem));
	pr_devel("  TCE SZ  : 0x%016llx\n", tce_size);

	rc = opal_pci_set_phb_tce_memory(phb_id, __pa(tce_mem), tce_size);
	if (rc != OPAL_SUCCESS) {
		pr_err("  Failed to set TCE memory, OPAL error %lld\n", rc);
		return;
	}

	phb = alloc_bootmem(sizeof(struct pnv_phb));
	if (phb) {
		memset(phb, 0, sizeof(struct pnv_phb));
		phb->hose = pcibios_alloc_controller(np);
	}
	if (!phb || !phb->hose) {
		pr_err("  Failed to allocate PCI controller\n");
		return;
	}

	spin_lock_init(&phb->lock);
	phb->hose->first_busno = 0;
	phb->hose->last_busno = 0xff;
	phb->hose->private_data = phb;
	phb->opal_id = phb_id;
	phb->type = PNV_PHB_P5IOC2;
	phb->model = PNV_PHB_MODEL_P5IOC2;

	phb->regs = of_iomap(np, 0);

	if (phb->regs == NULL)
		pr_err("  Failed to map registers !\n");
	else {
		pr_devel("  P_BUID     = 0x%08x\n", in_be32(phb->regs + 0x100));
		pr_devel("  P_IOSZ     = 0x%08x\n", in_be32(phb->regs + 0x1b0));
		pr_devel("  P_IO_ST    = 0x%08x\n", in_be32(phb->regs + 0x1e0));
		pr_devel("  P_MEM1_H   = 0x%08x\n", in_be32(phb->regs + 0x1a0));
		pr_devel("  P_MEM1_L   = 0x%08x\n", in_be32(phb->regs + 0x190));
		pr_devel("  P_MSZ1_L   = 0x%08x\n", in_be32(phb->regs + 0x1c0));
		pr_devel("  P_MEM_ST   = 0x%08x\n", in_be32(phb->regs + 0x1d0));
		pr_devel("  P_MEM2_H   = 0x%08x\n", in_be32(phb->regs + 0x2c0));
		pr_devel("  P_MEM2_L   = 0x%08x\n", in_be32(phb->regs + 0x2b0));
		pr_devel("  P_MSZ2_H   = 0x%08x\n", in_be32(phb->regs + 0x2d0));
		pr_devel("  P_MSZ2_L   = 0x%08x\n", in_be32(phb->regs + 0x2e0));
	}

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(phb->hose, np, primary);
	primary = 0;

	phb->hose->ops = &pnv_pci_ops;

	/* Setup MSI support */
	pnv_pci_init_p5ioc2_msis(phb);

	/* Setup TCEs */
	phb->dma_dev_setup = pnv_pci_p5ioc2_dma_dev_setup;
	pnv_pci_setup_iommu_table(&phb->p5ioc2.iommu_table,
				  tce_mem, tce_size, 0);
}

void __init pnv_pci_init_p5ioc2_hub(struct device_node *np)
{
	struct device_node *phbn;
	const u64 *prop64;
	u64 hub_id;
	void *tce_mem;
	uint64_t tce_per_phb;
	int64_t rc;
	int phb_count = 0;

	pr_info("Probing p5ioc2 IO-Hub %s\n", np->full_name);

	prop64 = of_get_property(np, "ibm,opal-hubid", NULL);
	if (!prop64) {
		pr_err(" Missing \"ibm,opal-hubid\" property !\n");
		return;
	}
	hub_id = be64_to_cpup(prop64);
	pr_info(" HUB-ID : 0x%016llx\n", hub_id);

	/* Currently allocate 16M of TCE memory for every Hub
	 *
	 * XXX TODO: Make it chip local if possible
	 */
	tce_mem = __alloc_bootmem(P5IOC2_TCE_MEMORY, P5IOC2_TCE_MEMORY,
				  __pa(MAX_DMA_ADDRESS));
	if (!tce_mem) {
		pr_err(" Failed to allocate TCE Memory !\n");
		return;
	}
	pr_debug(" TCE    : 0x%016lx..0x%016lx\n",
		__pa(tce_mem), __pa(tce_mem) + P5IOC2_TCE_MEMORY - 1);
	rc = opal_pci_set_hub_tce_memory(hub_id, __pa(tce_mem),
					P5IOC2_TCE_MEMORY);
	if (rc != OPAL_SUCCESS) {
		pr_err(" Failed to allocate TCE memory, OPAL error %lld\n", rc);
		return;
	}

	/* Count child PHBs */
	for_each_child_of_node(np, phbn) {
		if (of_device_is_compatible(phbn, "ibm,p5ioc2-pcix") ||
		    of_device_is_compatible(phbn, "ibm,p5ioc2-pciex"))
			phb_count++;
	}

	/* Calculate how much TCE space we can give per PHB */
	tce_per_phb = __rounddown_pow_of_two(P5IOC2_TCE_MEMORY / phb_count);
	pr_info(" Allocating %lld MB of TCE memory per PHB\n",
		tce_per_phb >> 20);

	/* Initialize PHBs */
	for_each_child_of_node(np, phbn) {
		if (of_device_is_compatible(phbn, "ibm,p5ioc2-pcix") ||
		    of_device_is_compatible(phbn, "ibm,p5ioc2-pciex")) {
			pnv_pci_init_p5ioc2_phb(phbn, tce_mem, tce_per_phb);
			tce_mem += tce_per_phb;
		}
	}
}
