/*
 * Copyright 2010 Ben Herrenschmidt, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>
#include <asm/iommu.h>
#include <asm/io-workarounds.h>
#include <asm/debug.h>

#include "wsp.h"
#include "wsp_pci.h"
#include "msi.h"


/* Max number of TVTs for one table. Only 32-bit tables can use
 * multiple TVTs and so the max currently supported is thus 8
 * since only 2G of DMA space is supported
 */
#define MAX_TABLE_TVT_COUNT		8

struct wsp_dma_table {
	struct list_head	link;
	struct iommu_table	table;
	struct wsp_phb	*phb;
	struct page		*tces[MAX_TABLE_TVT_COUNT];
};

/* We support DMA regions from 0...2G in 32bit space (no support for
 * 64-bit DMA just yet). Each device gets a separate TCE table (TVT
 * entry) with validation enabled (though not supported by SimiCS
 * just yet).
 *
 * To simplify things, we divide this 2G space into N regions based
 * on the constant below which could be turned into a tunable eventually
 *
 * We then assign dynamically those regions to devices as they show up.
 *
 * We use a bitmap as an allocator for these.
 *
 * Tables are allocated/created dynamically as devices are discovered,
 * multiple TVT entries are used if needed
 *
 * When 64-bit DMA support is added we should simply use a separate set
 * of larger regions (the HW supports 64 TVT entries). We can
 * additionally create a bypass region in 64-bit space for performances
 * though that would have a cost in term of security.
 *
 * If you set NUM_DMA32_REGIONS to 1, then a single table is shared
 * for all devices and bus/dev/fn validation is disabled
 *
 * Note that a DMA32 region cannot be smaller than 256M so the max
 * supported here for now is 8. We don't yet support sharing regions
 * between multiple devices so the max number of devices supported
 * is MAX_TABLE_TVT_COUNT.
 */
#define NUM_DMA32_REGIONS	1

struct wsp_phb {
	struct pci_controller	*hose;

	/* Lock controlling access to the list of dma tables.
	 * It does -not- protect against dma_* operations on
	 * those tables, those should be stopped before an entry
	 * is removed from the list.
	 *
	 * The lock is also used for error handling operations
	 */
	spinlock_t		lock;
	struct list_head	dma_tables;
	unsigned long		dma32_map;
	unsigned long		dma32_base;
	unsigned int		dma32_num_regions;
	unsigned long		dma32_region_size;

	/* Debugfs stuff */
	struct dentry		*ddir;

	struct list_head	all;
};
static LIST_HEAD(wsp_phbs);

//#define cfg_debug(fmt...)	pr_debug(fmt)
#define cfg_debug(fmt...)


static int wsp_pcie_read_config(struct pci_bus *bus, unsigned int devfn,
				  int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	int suboff;
	u64 addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= 0x1000)
		return  PCIBIOS_BAD_REGISTER_NUMBER;
	addr = PCIE_REG_CA_ENABLE |
		((u64)bus->number) << PCIE_REG_CA_BUS_SHIFT |
		((u64)devfn) << PCIE_REG_CA_FUNC_SHIFT |
		((u64)offset & ~3) << PCIE_REG_CA_REG_SHIFT;
	suboff = offset & 3;

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */

	switch (len) {
	case 1:
		addr |= (0x8ul >> suboff) << PCIE_REG_CA_BE_SHIFT;
		out_be64(hose->cfg_data + PCIE_REG_CONFIG_ADDRESS, addr);
		*val = (in_le32(hose->cfg_data + PCIE_REG_CONFIG_DATA)
			>> (suboff << 3)) & 0xff;
		cfg_debug("read 1 %02x:%02x:%02x + %02x/%x addr=0x%llx val=%02x\n",
			  bus->number, devfn >> 3, devfn & 7,
			  offset, suboff, addr, *val);
		break;
	case 2:
		addr |= (0xcul >> suboff) << PCIE_REG_CA_BE_SHIFT;
		out_be64(hose->cfg_data + PCIE_REG_CONFIG_ADDRESS, addr);
		*val = (in_le32(hose->cfg_data + PCIE_REG_CONFIG_DATA)
			>> (suboff << 3)) & 0xffff;
		cfg_debug("read 2 %02x:%02x:%02x + %02x/%x addr=0x%llx val=%04x\n",
			  bus->number, devfn >> 3, devfn & 7,
			  offset, suboff, addr, *val);
		break;
	default:
		addr |= 0xful << PCIE_REG_CA_BE_SHIFT;
		out_be64(hose->cfg_data + PCIE_REG_CONFIG_ADDRESS, addr);
		*val = in_le32(hose->cfg_data + PCIE_REG_CONFIG_DATA);
		cfg_debug("read 4 %02x:%02x:%02x + %02x/%x addr=0x%llx val=%08x\n",
			  bus->number, devfn >> 3, devfn & 7,
			  offset, suboff, addr, *val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int wsp_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
				   int offset, int len, u32 val)
{
	struct pci_controller *hose;
	int suboff;
	u64 addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= 0x1000)
		return  PCIBIOS_BAD_REGISTER_NUMBER;
	addr = PCIE_REG_CA_ENABLE |
		((u64)bus->number) << PCIE_REG_CA_BUS_SHIFT |
		((u64)devfn) << PCIE_REG_CA_FUNC_SHIFT |
		((u64)offset & ~3) << PCIE_REG_CA_REG_SHIFT;
	suboff = offset & 3;

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		addr |= (0x8ul >> suboff) << PCIE_REG_CA_BE_SHIFT;
		val <<= suboff << 3;
		out_be64(hose->cfg_data + PCIE_REG_CONFIG_ADDRESS, addr);
		out_le32(hose->cfg_data + PCIE_REG_CONFIG_DATA, val);
		cfg_debug("write 1 %02x:%02x:%02x + %02x/%x addr=0x%llx val=%02x\n",
			  bus->number, devfn >> 3, devfn & 7,
			  offset, suboff, addr, val);
		break;
	case 2:
		addr |= (0xcul >> suboff) << PCIE_REG_CA_BE_SHIFT;
		val <<= suboff << 3;
		out_be64(hose->cfg_data + PCIE_REG_CONFIG_ADDRESS, addr);
		out_le32(hose->cfg_data + PCIE_REG_CONFIG_DATA, val);
		cfg_debug("write 2 %02x:%02x:%02x + %02x/%x addr=0x%llx val=%04x\n",
			  bus->number, devfn >> 3, devfn & 7,
			  offset, suboff, addr, val);
		break;
	default:
		addr |= 0xful << PCIE_REG_CA_BE_SHIFT;
		out_be64(hose->cfg_data + PCIE_REG_CONFIG_ADDRESS, addr);
		out_le32(hose->cfg_data + PCIE_REG_CONFIG_DATA, val);
		cfg_debug("write 4 %02x:%02x:%02x + %02x/%x addr=0x%llx val=%08x\n",
			  bus->number, devfn >> 3, devfn & 7,
			  offset, suboff, addr, val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops wsp_pcie_pci_ops =
{
	.read = wsp_pcie_read_config,
	.write = wsp_pcie_write_config,
};

#define TCE_SHIFT		12
#define TCE_PAGE_SIZE		(1 << TCE_SHIFT)
#define TCE_PCI_WRITE		0x2		 /* write from PCI allowed */
#define TCE_PCI_READ		0x1	 	 /* read from PCI allowed */
#define TCE_RPN_MASK		0x3fffffffffful  /* 42-bit RPN (4K pages) */
#define TCE_RPN_SHIFT		12

//#define dma_debug(fmt...)	pr_debug(fmt)
#define dma_debug(fmt...)

static int tce_build_wsp(struct iommu_table *tbl, long index, long npages,
			   unsigned long uaddr, enum dma_data_direction direction,
			   struct dma_attrs *attrs)
{
	struct wsp_dma_table *ptbl = container_of(tbl,
						    struct wsp_dma_table,
						    table);
	u64 proto_tce;
	u64 *tcep;
	u64 rpn;

	proto_tce = TCE_PCI_READ;
#ifdef CONFIG_WSP_DD1_WORKAROUND_DD1_TCE_BUGS
	proto_tce |= TCE_PCI_WRITE;
#else
	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;
#endif

	/* XXX Make this faster by factoring out the page address for
	 * within a TCE table
	 */
	while (npages--) {
		/* We don't use it->base as the table can be scattered */
		tcep = (u64 *)page_address(ptbl->tces[index >> 16]);
		tcep += (index & 0xffff);

		/* can't move this out since we might cross LMB boundary */
		rpn = __pa(uaddr) >> TCE_SHIFT;
		*tcep = proto_tce | (rpn & TCE_RPN_MASK) << TCE_RPN_SHIFT;

		dma_debug("[DMA] TCE %p set to 0x%016llx (dma addr: 0x%lx)\n",
			  tcep, *tcep, (tbl->it_offset + index) << IOMMU_PAGE_SHIFT);

		uaddr += TCE_PAGE_SIZE;
		index++;
	}
	return 0;
}

static void tce_free_wsp(struct iommu_table *tbl, long index, long npages)
{
	struct wsp_dma_table *ptbl = container_of(tbl,
						    struct wsp_dma_table,
						    table);
#ifndef CONFIG_WSP_DD1_WORKAROUND_DD1_TCE_BUGS
	struct pci_controller *hose = ptbl->phb->hose;
#endif
	u64 *tcep;

	/* XXX Make this faster by factoring out the page address for
	 * within a TCE table. Also use line-kill option to kill multiple
	 * TCEs at once
	 */
	while (npages--) {
		/* We don't use it->base as the table can be scattered */
		tcep = (u64 *)page_address(ptbl->tces[index >> 16]);
		tcep += (index & 0xffff);
		dma_debug("[DMA] TCE %p cleared\n", tcep);
		*tcep = 0;
#ifndef CONFIG_WSP_DD1_WORKAROUND_DD1_TCE_BUGS
		/* Don't write there since it would pollute other MMIO accesses */
		out_be64(hose->cfg_data + PCIE_REG_TCE_KILL,
			 PCIE_REG_TCEKILL_SINGLE | PCIE_REG_TCEKILL_PS_4K |
			 (__pa(tcep) & PCIE_REG_TCEKILL_ADDR_MASK));
#endif
		index++;
	}
}

static struct wsp_dma_table *wsp_pci_create_dma32_table(struct wsp_phb *phb,
							    unsigned int region,
							    struct pci_dev *validate)
{
	struct pci_controller *hose = phb->hose;
	unsigned long size = phb->dma32_region_size;
	unsigned long addr = phb->dma32_region_size * region + phb->dma32_base;
	struct wsp_dma_table *tbl;
	int tvts_per_table, i, tvt, nid;
	unsigned long flags;

	nid = of_node_to_nid(phb->hose->dn);

	/* Calculate how many TVTs are needed */
	tvts_per_table = size / 0x10000000;
	if (tvts_per_table == 0)
		tvts_per_table = 1;

	/* Calculate the base TVT index. We know all tables have the same
	 * size so we just do a simple multiply here
	 */
	tvt = region * tvts_per_table;

	pr_debug("         Region : %d\n", region);
	pr_debug("      DMA range : 0x%08lx..0x%08lx\n", addr, addr + size - 1);
	pr_debug(" Number of TVTs : %d\n", tvts_per_table);
	pr_debug("       Base TVT : %d\n", tvt);
	pr_debug("         Node   : %d\n", nid);

	tbl = kzalloc_node(sizeof(struct wsp_dma_table), GFP_KERNEL, nid);
	if (!tbl)
		return ERR_PTR(-ENOMEM);
	tbl->phb = phb;

	/* Create as many TVTs as needed, each represents 256M at most */
	for (i = 0; i < tvts_per_table; i++) {
		u64 tvt_data1, tvt_data0;

		/* Allocate table. We use a 4K TCE size for now always so
		 * one table is always 8 * (258M / 4K) == 512K
		 */
		tbl->tces[i] = alloc_pages_node(nid, GFP_KERNEL, get_order(0x80000));
		if (tbl->tces[i] == NULL)
			goto fail;
		memset(page_address(tbl->tces[i]), 0, 0x80000);

		pr_debug(" TCE table %d at : %p\n", i, page_address(tbl->tces[i]));

		/* Table size. We currently set it to be the whole 256M region */
		tvt_data0 = 2ull << IODA_TVT0_TCE_TABLE_SIZE_SHIFT;
		/* IO page size set to 4K */
		tvt_data1 = 1ull << IODA_TVT1_IO_PAGE_SIZE_SHIFT;
		/* Shift in the address */
		tvt_data0 |= __pa(page_address(tbl->tces[i])) << IODA_TVT0_TTA_SHIFT;

		/* Validation stuff. We only validate fully bus/dev/fn for now
		 * one day maybe we can group devices but that isn't the case
		 * at the moment
		 */
		if (validate) {
			tvt_data0 |= IODA_TVT0_BUSNUM_VALID_MASK;
			tvt_data0 |= validate->bus->number;
			tvt_data1 |= IODA_TVT1_DEVNUM_VALID;
			tvt_data1 |= ((u64)PCI_SLOT(validate->devfn))
				<< IODA_TVT1_DEVNUM_VALUE_SHIFT;
			tvt_data1 |= IODA_TVT1_FUNCNUM_VALID;
			tvt_data1 |= ((u64)PCI_FUNC(validate->devfn))
				<< IODA_TVT1_FUNCNUM_VALUE_SHIFT;
		}

		/* XX PE number is always 0 for now */

		/* Program the values using the PHB lock */
		spin_lock_irqsave(&phb->lock, flags);
		out_be64(hose->cfg_data + PCIE_REG_IODA_ADDR,
			 (tvt + i) | PCIE_REG_IODA_AD_TBL_TVT);
		out_be64(hose->cfg_data + PCIE_REG_IODA_DATA1, tvt_data1);
		out_be64(hose->cfg_data + PCIE_REG_IODA_DATA0, tvt_data0);
		spin_unlock_irqrestore(&phb->lock, flags);
	}

	/* Init bits and pieces */
	tbl->table.it_blocksize = 16;
	tbl->table.it_offset = addr >> IOMMU_PAGE_SHIFT;
	tbl->table.it_size = size >> IOMMU_PAGE_SHIFT;

	/*
	 * It's already blank but we clear it anyway.
	 * Consider an aditiona interface that makes cleaing optional
	 */
	iommu_init_table(&tbl->table, nid);

	list_add(&tbl->link, &phb->dma_tables);
	return tbl;

 fail:
	pr_debug("  Failed to allocate a 256M TCE table !\n");
	for (i = 0; i < tvts_per_table; i++)
		if (tbl->tces[i])
			__free_pages(tbl->tces[i], get_order(0x80000));
	kfree(tbl);
	return ERR_PTR(-ENOMEM);
}

static void __devinit wsp_pci_dma_dev_setup(struct pci_dev *pdev)
{
	struct dev_archdata *archdata = &pdev->dev.archdata;
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct wsp_phb *phb = hose->private_data;
	struct wsp_dma_table *table = NULL;
	unsigned long flags;
	int i;

	/* Don't assign an iommu table to a bridge */
	if (pdev->hdr_type == PCI_HEADER_TYPE_BRIDGE)
		return;

	pr_debug("%s: Setting up DMA...\n", pci_name(pdev));

	spin_lock_irqsave(&phb->lock, flags);

	/* If only one region, check if it already exist */
	if (phb->dma32_num_regions == 1) {
		spin_unlock_irqrestore(&phb->lock, flags);
		if (list_empty(&phb->dma_tables))
			table = wsp_pci_create_dma32_table(phb, 0, NULL);
		else
			table = list_first_entry(&phb->dma_tables,
						 struct wsp_dma_table,
						 link);
	} else {
		/* else find a free region */
		for (i = 0; i < phb->dma32_num_regions && !table; i++) {
			if (__test_and_set_bit(i, &phb->dma32_map))
				continue;
			spin_unlock_irqrestore(&phb->lock, flags);
			table = wsp_pci_create_dma32_table(phb, i, pdev);
		}
	}

	/* Check if we got an error */
	if (IS_ERR(table)) {
		pr_err("%s: Failed to create DMA table, err %ld !\n",
		       pci_name(pdev), PTR_ERR(table));
		return;
	}

	/* Or a valid table */
	if (table) {
		pr_info("%s: Setup iommu: 32-bit DMA region 0x%08lx..0x%08lx\n",
			pci_name(pdev),
			table->table.it_offset << IOMMU_PAGE_SHIFT,
			(table->table.it_offset << IOMMU_PAGE_SHIFT)
			+ phb->dma32_region_size - 1);
		archdata->dma_data.iommu_table_base = &table->table;
		return;
	}

	/* Or no room */
	spin_unlock_irqrestore(&phb->lock, flags);
	pr_err("%s: Out of DMA space !\n", pci_name(pdev));
}

static void __init wsp_pcie_configure_hw(struct pci_controller *hose)
{
	u64 val;
	int i;

#define DUMP_REG(x) \
	pr_debug("%-30s : 0x%016llx\n", #x, in_be64(hose->cfg_data + x))

	/*
	 * Some WSP variants  has a bogus class code by default in the PCI-E
	 * root complex's built-in P2P bridge
	 */
	val = in_be64(hose->cfg_data + PCIE_REG_SYS_CFG1);
	pr_debug("PCI-E SYS_CFG1 : 0x%llx\n", val);
	out_be64(hose->cfg_data + PCIE_REG_SYS_CFG1,
		 (val & ~PCIE_REG_SYS_CFG1_CLASS_CODE) | (PCI_CLASS_BRIDGE_PCI << 8));
	pr_debug("PCI-E SYS_CFG1 : 0x%llx\n", in_be64(hose->cfg_data + PCIE_REG_SYS_CFG1));

#ifdef CONFIG_WSP_DD1_WORKAROUND_DD1_TCE_BUGS
	/* XXX Disable TCE caching, it doesn't work on DD1 */
	out_be64(hose->cfg_data + 0xe50,
		 in_be64(hose->cfg_data + 0xe50) | (3ull << 62));
	printk("PCI-E DEBUG CONTROL 5 = 0x%llx\n", in_be64(hose->cfg_data + 0xe50));
#endif

	/* Configure M32A and IO. IO is hard wired to be 1M for now */
	out_be64(hose->cfg_data + PCIE_REG_IO_BASE_ADDR, hose->io_base_phys);
	out_be64(hose->cfg_data + PCIE_REG_IO_BASE_MASK,
		 (~(hose->io_resource.end - hose->io_resource.start)) &
		 0x3fffffff000ul);
	out_be64(hose->cfg_data + PCIE_REG_IO_START_ADDR, 0 | 1);

	out_be64(hose->cfg_data + PCIE_REG_M32A_BASE_ADDR,
		 hose->mem_resources[0].start);
	printk("Want to write to M32A_BASE_MASK : 0x%llx\n",
		 (~(hose->mem_resources[0].end -
		    hose->mem_resources[0].start)) & 0x3ffffff0000ul);
	out_be64(hose->cfg_data + PCIE_REG_M32A_BASE_MASK,
		 (~(hose->mem_resources[0].end -
		    hose->mem_resources[0].start)) & 0x3ffffff0000ul);
	out_be64(hose->cfg_data + PCIE_REG_M32A_START_ADDR,
		 (hose->mem_resources[0].start - hose->pci_mem_offset) | 1);

	/* Clear all TVT entries
	 *
	 * XX Might get TVT count from device-tree
	 */
	for (i = 0; i < IODA_TVT_COUNT; i++) {
		out_be64(hose->cfg_data + PCIE_REG_IODA_ADDR,
			 PCIE_REG_IODA_AD_TBL_TVT | i);
		out_be64(hose->cfg_data + PCIE_REG_IODA_DATA1, 0);
		out_be64(hose->cfg_data + PCIE_REG_IODA_DATA0, 0);
	}

	/* Kill the TCE cache */
	out_be64(hose->cfg_data + PCIE_REG_PHB_CONFIG,
		 in_be64(hose->cfg_data + PCIE_REG_PHB_CONFIG) |
		 PCIE_REG_PHBC_64B_TCE_EN);

	/* Enable 32 & 64-bit MSIs, IO space and M32A */
	val = PCIE_REG_PHBC_32BIT_MSI_EN |
	      PCIE_REG_PHBC_IO_EN |
	      PCIE_REG_PHBC_64BIT_MSI_EN |
	      PCIE_REG_PHBC_M32A_EN;
	if (iommu_is_off)
		val |= PCIE_REG_PHBC_DMA_XLATE_BYPASS;
	pr_debug("Will write config: 0x%llx\n", val);
	out_be64(hose->cfg_data + PCIE_REG_PHB_CONFIG, val);

	/* Enable error reporting */
	out_be64(hose->cfg_data + 0xe00,
		 in_be64(hose->cfg_data + 0xe00) | 0x0008000000000000ull);

	/* Mask an error that's generated when doing config space probe
	 *
	 * XXX Maybe we should only mask it around config space cycles... that or
	 * ignore it when we know we had a config space cycle recently ?
	 */
	out_be64(hose->cfg_data + PCIE_REG_DMA_ERR_STATUS_MASK, 0x8000000000000000ull);
	out_be64(hose->cfg_data + PCIE_REG_DMA_ERR1_STATUS_MASK, 0x8000000000000000ull);

	/* Enable UTL errors, for now, all of them got to UTL irq 1
	 *
	 * We similarily mask one UTL error caused apparently during normal
	 * probing. We also mask the link up error
	 */
	out_be64(hose->cfg_data + PCIE_UTL_SYS_BUS_AGENT_ERR_SEV, 0);
	out_be64(hose->cfg_data + PCIE_UTL_RC_ERR_SEVERITY, 0);
	out_be64(hose->cfg_data + PCIE_UTL_PCIE_PORT_ERROR_SEV, 0);
	out_be64(hose->cfg_data + PCIE_UTL_SYS_BUS_AGENT_IRQ_EN, 0xffffffff00000000ull);
	out_be64(hose->cfg_data + PCIE_UTL_PCIE_PORT_IRQ_EN, 0xff5fffff00000000ull);
	out_be64(hose->cfg_data + PCIE_UTL_EP_ERR_IRQ_EN, 0xffffffff00000000ull);

	DUMP_REG(PCIE_REG_IO_BASE_ADDR);
	DUMP_REG(PCIE_REG_IO_BASE_MASK);
	DUMP_REG(PCIE_REG_IO_START_ADDR);
	DUMP_REG(PCIE_REG_M32A_BASE_ADDR);
	DUMP_REG(PCIE_REG_M32A_BASE_MASK);
	DUMP_REG(PCIE_REG_M32A_START_ADDR);
	DUMP_REG(PCIE_REG_M32B_BASE_ADDR);
	DUMP_REG(PCIE_REG_M32B_BASE_MASK);
	DUMP_REG(PCIE_REG_M32B_START_ADDR);
	DUMP_REG(PCIE_REG_M64_BASE_ADDR);
	DUMP_REG(PCIE_REG_M64_BASE_MASK);
	DUMP_REG(PCIE_REG_M64_START_ADDR);
	DUMP_REG(PCIE_REG_PHB_CONFIG);
}

static void wsp_pci_wait_io_idle(struct wsp_phb *phb, unsigned long port)
{
	u64 val;
	int i;

	for (i = 0; i < 10000; i++) {
		val = in_be64(phb->hose->cfg_data + 0xe08);
		if ((val & 0x1900000000000000ull) == 0x0100000000000000ull)
			return;
		udelay(1);
	}
	pr_warning("PCI IO timeout on domain %d port 0x%lx\n",
		   phb->hose->global_number, port);
}

#define DEF_PCI_AC_RET_pio(name, ret, at, al, aa)		\
static ret wsp_pci_##name at					\
{								\
	struct iowa_bus *bus;					\
	struct wsp_phb *phb;					\
	unsigned long flags;					\
	ret rval;						\
	bus = iowa_pio_find_bus(aa);				\
	WARN_ON(!bus);						\
	phb = bus->private;					\
	spin_lock_irqsave(&phb->lock, flags);			\
	wsp_pci_wait_io_idle(phb, aa);				\
	rval = __do_##name al;					\
	spin_unlock_irqrestore(&phb->lock, flags);		\
	return rval;						\
}

#define DEF_PCI_AC_NORET_pio(name, at, al, aa)			\
static void wsp_pci_##name at					\
{								\
	struct iowa_bus *bus;					\
	struct wsp_phb *phb;					\
	unsigned long flags;					\
	bus = iowa_pio_find_bus(aa);				\
	WARN_ON(!bus);						\
	phb = bus->private;					\
	spin_lock_irqsave(&phb->lock, flags);			\
	wsp_pci_wait_io_idle(phb, aa);				\
	__do_##name al;						\
	spin_unlock_irqrestore(&phb->lock, flags);		\
}

#define DEF_PCI_AC_RET_mem(name, ret, at, al, aa)
#define DEF_PCI_AC_NORET_mem(name, at, al, aa)

#define DEF_PCI_AC_RET(name, ret, at, al, space, aa)		\
	DEF_PCI_AC_RET_##space(name, ret, at, al, aa)

#define DEF_PCI_AC_NORET(name, at, al, space, aa)		\
	DEF_PCI_AC_NORET_##space(name, at, al, aa)		\


#include <asm/io-defs.h>

#undef DEF_PCI_AC_RET
#undef DEF_PCI_AC_NORET

static struct ppc_pci_io wsp_pci_iops = {
	.inb = wsp_pci_inb,
	.inw = wsp_pci_inw,
	.inl = wsp_pci_inl,
	.outb = wsp_pci_outb,
	.outw = wsp_pci_outw,
	.outl = wsp_pci_outl,
	.insb = wsp_pci_insb,
	.insw = wsp_pci_insw,
	.insl = wsp_pci_insl,
	.outsb = wsp_pci_outsb,
	.outsw = wsp_pci_outsw,
	.outsl = wsp_pci_outsl,
};

static int __init wsp_setup_one_phb(struct device_node *np)
{
	struct pci_controller *hose;
	struct wsp_phb *phb;

	pr_info("PCI: Setting up PCIe host bridge 0x%s\n", np->full_name);

	phb = zalloc_maybe_bootmem(sizeof(struct wsp_phb), GFP_KERNEL);
	if (!phb)
		return -ENOMEM;
	hose = pcibios_alloc_controller(np);
	if (!hose) {
		/* Can't really free the phb */
		return -ENOMEM;
	}
	hose->private_data = phb;
	phb->hose = hose;

	INIT_LIST_HEAD(&phb->dma_tables);
	spin_lock_init(&phb->lock);

	/* XXX Use bus-range property ? */
	hose->first_busno = 0;
	hose->last_busno = 0xff;

	/* We use cfg_data as the address for the whole bridge MMIO space
	 */
	hose->cfg_data = of_iomap(hose->dn, 0);

	pr_debug("PCIe registers mapped at 0x%p\n", hose->cfg_data);

	/* Get the ranges of the device-tree */
	pci_process_bridge_OF_ranges(hose, np, 0);

	/* XXX Force re-assigning of everything for now */
	pci_add_flags(PCI_REASSIGN_ALL_BUS | PCI_REASSIGN_ALL_RSRC |
		      PCI_ENABLE_PROC_DOMAINS);

	/* Calculate how the TCE space is divided */
	phb->dma32_base		= 0;
	phb->dma32_num_regions	= NUM_DMA32_REGIONS;
	if (phb->dma32_num_regions > MAX_TABLE_TVT_COUNT) {
		pr_warning("IOMMU: Clamped to %d DMA32 regions\n",
			   MAX_TABLE_TVT_COUNT);
		phb->dma32_num_regions = MAX_TABLE_TVT_COUNT;
	}
	phb->dma32_region_size	= 0x80000000 / phb->dma32_num_regions;

	BUG_ON(!is_power_of_2(phb->dma32_region_size));

	/* Setup config ops */
	hose->ops = &wsp_pcie_pci_ops;

	/* Configure the HW */
	wsp_pcie_configure_hw(hose);

	/* Instanciate IO workarounds */
	iowa_register_bus(hose, &wsp_pci_iops, NULL, phb);
#ifdef CONFIG_PCI_MSI
	wsp_setup_phb_msi(hose);
#endif

	/* Add to global list */
	list_add(&phb->all, &wsp_phbs);

	return 0;
}

void __init wsp_setup_pci(void)
{
	struct device_node *np;
	int rc;

	/* Find host bridges */
	for_each_compatible_node(np, "pciex", PCIE_COMPATIBLE) {
		rc = wsp_setup_one_phb(np);
		if (rc)
			pr_err("Failed to setup PCIe bridge %s, rc=%d\n",
			       np->full_name, rc);
	}

	/* Establish device-tree linkage */
	pci_devs_phb_init();

	/* Set DMA ops to use TCEs */
	if (iommu_is_off) {
		pr_info("PCI-E: Disabled TCEs, using direct DMA\n");
		set_pci_dma_ops(&dma_direct_ops);
	} else {
		ppc_md.pci_dma_dev_setup = wsp_pci_dma_dev_setup;
		ppc_md.tce_build = tce_build_wsp;
		ppc_md.tce_free = tce_free_wsp;
		set_pci_dma_ops(&dma_iommu_ops);
	}
}

#define err_debug(fmt...)	pr_debug(fmt)
//#define err_debug(fmt...)

static int __init wsp_pci_get_err_irq_no_dt(struct device_node *np)
{
	const u32 *prop;
	int hw_irq;

	/* Ok, no interrupts property, let's try to find our child P2P */
	np = of_get_next_child(np, NULL);
	if (np == NULL)
		return 0;

	/* Grab it's interrupt map */
	prop = of_get_property(np, "interrupt-map", NULL);
	if (prop == NULL)
		return 0;

	/* Grab one of the interrupts in there, keep the low 4 bits */
	hw_irq = prop[5] & 0xf;

	/* 0..4 for PHB 0 and 5..9 for PHB 1 */
	if (hw_irq < 5)
		hw_irq = 4;
	else
		hw_irq = 9;
	hw_irq |= prop[5] & ~0xf;

	err_debug("PCI: Using 0x%x as error IRQ for %s\n",
		  hw_irq, np->parent->full_name);
	return irq_create_mapping(NULL, hw_irq);
}

static const struct {
	u32 offset;
	const char *name;
} wsp_pci_regs[] = {
#define DREG(x) { PCIE_REG_##x, #x }
#define DUTL(x) { PCIE_UTL_##x, "UTL_" #x }
	/* Architected registers except CONFIG_ and IODA
         * to avoid side effects
	 */
	DREG(DMA_CHAN_STATUS),
	DREG(CPU_LOADSTORE_STATUS),
	DREG(LOCK0),
	DREG(LOCK1),
	DREG(PHB_CONFIG),
	DREG(IO_BASE_ADDR),
	DREG(IO_BASE_MASK),
	DREG(IO_START_ADDR),
	DREG(M32A_BASE_ADDR),
	DREG(M32A_BASE_MASK),
	DREG(M32A_START_ADDR),
	DREG(M32B_BASE_ADDR),
	DREG(M32B_BASE_MASK),
	DREG(M32B_START_ADDR),
	DREG(M64_BASE_ADDR),
	DREG(M64_BASE_MASK),
	DREG(M64_START_ADDR),
	DREG(TCE_KILL),
	DREG(LOCK2),
	DREG(PHB_GEN_CAP),
	DREG(PHB_TCE_CAP),
	DREG(PHB_IRQ_CAP),
	DREG(PHB_EEH_CAP),
	DREG(PAPR_ERR_INJ_CONTROL),
	DREG(PAPR_ERR_INJ_ADDR),
	DREG(PAPR_ERR_INJ_MASK),

	/* UTL core regs */
	DUTL(SYS_BUS_CONTROL),
	DUTL(STATUS),
	DUTL(SYS_BUS_AGENT_STATUS),
	DUTL(SYS_BUS_AGENT_ERR_SEV),
	DUTL(SYS_BUS_AGENT_IRQ_EN),
	DUTL(SYS_BUS_BURST_SZ_CONF),
	DUTL(REVISION_ID),
	DUTL(OUT_POST_HDR_BUF_ALLOC),
	DUTL(OUT_POST_DAT_BUF_ALLOC),
	DUTL(IN_POST_HDR_BUF_ALLOC),
	DUTL(IN_POST_DAT_BUF_ALLOC),
	DUTL(OUT_NP_BUF_ALLOC),
	DUTL(IN_NP_BUF_ALLOC),
	DUTL(PCIE_TAGS_ALLOC),
	DUTL(GBIF_READ_TAGS_ALLOC),

	DUTL(PCIE_PORT_CONTROL),
	DUTL(PCIE_PORT_STATUS),
	DUTL(PCIE_PORT_ERROR_SEV),
	DUTL(PCIE_PORT_IRQ_EN),
	DUTL(RC_STATUS),
	DUTL(RC_ERR_SEVERITY),
	DUTL(RC_IRQ_EN),
	DUTL(EP_STATUS),
	DUTL(EP_ERR_SEVERITY),
	DUTL(EP_ERR_IRQ_EN),
	DUTL(PCI_PM_CTRL1),
	DUTL(PCI_PM_CTRL2),

	/* PCIe stack regs */
	DREG(SYSTEM_CONFIG1),
	DREG(SYSTEM_CONFIG2),
	DREG(EP_SYSTEM_CONFIG),
	DREG(EP_FLR),
	DREG(EP_BAR_CONFIG),
	DREG(LINK_CONFIG),
	DREG(PM_CONFIG),
	DREG(DLP_CONTROL),
	DREG(DLP_STATUS),
	DREG(ERR_REPORT_CONTROL),
	DREG(SLOT_CONTROL1),
	DREG(SLOT_CONTROL2),
	DREG(UTL_CONFIG),
	DREG(BUFFERS_CONFIG),
	DREG(ERROR_INJECT),
	DREG(SRIOV_CONFIG),
	DREG(PF0_SRIOV_STATUS),
	DREG(PF1_SRIOV_STATUS),
	DREG(PORT_NUMBER),
	DREG(POR_SYSTEM_CONFIG),

	/* Internal logic regs */
	DREG(PHB_VERSION),
	DREG(RESET),
	DREG(PHB_CONTROL),
	DREG(PHB_TIMEOUT_CONTROL1),
	DREG(PHB_QUIESCE_DMA),
	DREG(PHB_DMA_READ_TAG_ACTV),
	DREG(PHB_TCE_READ_TAG_ACTV),

	/* FIR registers */
	DREG(LEM_FIR_ACCUM),
	DREG(LEM_FIR_AND_MASK),
	DREG(LEM_FIR_OR_MASK),
	DREG(LEM_ACTION0),
	DREG(LEM_ACTION1),
	DREG(LEM_ERROR_MASK),
	DREG(LEM_ERROR_AND_MASK),
	DREG(LEM_ERROR_OR_MASK),

	/* Error traps registers */
	DREG(PHB_ERR_STATUS),
	DREG(PHB_ERR_STATUS),
	DREG(PHB_ERR1_STATUS),
	DREG(PHB_ERR_INJECT),
	DREG(PHB_ERR_LEM_ENABLE),
	DREG(PHB_ERR_IRQ_ENABLE),
	DREG(PHB_ERR_FREEZE_ENABLE),
	DREG(PHB_ERR_SIDE_ENABLE),
	DREG(PHB_ERR_LOG_0),
	DREG(PHB_ERR_LOG_1),
	DREG(PHB_ERR_STATUS_MASK),
	DREG(PHB_ERR1_STATUS_MASK),
	DREG(MMIO_ERR_STATUS),
	DREG(MMIO_ERR1_STATUS),
	DREG(MMIO_ERR_INJECT),
	DREG(MMIO_ERR_LEM_ENABLE),
	DREG(MMIO_ERR_IRQ_ENABLE),
	DREG(MMIO_ERR_FREEZE_ENABLE),
	DREG(MMIO_ERR_SIDE_ENABLE),
	DREG(MMIO_ERR_LOG_0),
	DREG(MMIO_ERR_LOG_1),
	DREG(MMIO_ERR_STATUS_MASK),
	DREG(MMIO_ERR1_STATUS_MASK),
	DREG(DMA_ERR_STATUS),
	DREG(DMA_ERR1_STATUS),
	DREG(DMA_ERR_INJECT),
	DREG(DMA_ERR_LEM_ENABLE),
	DREG(DMA_ERR_IRQ_ENABLE),
	DREG(DMA_ERR_FREEZE_ENABLE),
	DREG(DMA_ERR_SIDE_ENABLE),
	DREG(DMA_ERR_LOG_0),
	DREG(DMA_ERR_LOG_1),
	DREG(DMA_ERR_STATUS_MASK),
	DREG(DMA_ERR1_STATUS_MASK),

	/* Debug and Trace registers */
	DREG(PHB_DEBUG_CONTROL0),
	DREG(PHB_DEBUG_STATUS0),
	DREG(PHB_DEBUG_CONTROL1),
	DREG(PHB_DEBUG_STATUS1),
	DREG(PHB_DEBUG_CONTROL2),
	DREG(PHB_DEBUG_STATUS2),
	DREG(PHB_DEBUG_CONTROL3),
	DREG(PHB_DEBUG_STATUS3),
	DREG(PHB_DEBUG_CONTROL4),
	DREG(PHB_DEBUG_STATUS4),
	DREG(PHB_DEBUG_CONTROL5),
	DREG(PHB_DEBUG_STATUS5),

	/* Don't seem to exist ...
	DREG(PHB_DEBUG_CONTROL6),
	DREG(PHB_DEBUG_STATUS6),
	*/
};

static int wsp_pci_regs_show(struct seq_file *m, void *private)
{
	struct wsp_phb *phb = m->private;
	struct pci_controller *hose = phb->hose;
	int i;

	for (i = 0; i < ARRAY_SIZE(wsp_pci_regs); i++) {
		/* Skip write-only regs */
		if (wsp_pci_regs[i].offset == 0xc08 ||
		    wsp_pci_regs[i].offset == 0xc10 ||
		    wsp_pci_regs[i].offset == 0xc38 ||
		    wsp_pci_regs[i].offset == 0xc40)
			continue;
		seq_printf(m, "0x%03x: 0x%016llx %s\n",
			   wsp_pci_regs[i].offset,
			   in_be64(hose->cfg_data + wsp_pci_regs[i].offset),
			   wsp_pci_regs[i].name);
	}
	return 0;
}

static int wsp_pci_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, wsp_pci_regs_show, inode->i_private);
}

static const struct file_operations wsp_pci_regs_fops = {
	.open = wsp_pci_regs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int wsp_pci_reg_set(void *data, u64 val)
{
	out_be64((void __iomem *)data, val);
	return 0;
}

static int wsp_pci_reg_get(void *data, u64 *val)
{
	*val = in_be64((void __iomem *)data);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(wsp_pci_reg_fops, wsp_pci_reg_get, wsp_pci_reg_set, "0x%llx\n");

static irqreturn_t wsp_pci_err_irq(int irq, void *dev_id)
{
	struct wsp_phb *phb = dev_id;
	struct pci_controller *hose = phb->hose;
	irqreturn_t handled = IRQ_NONE;
	struct wsp_pcie_err_log_data ed;

	pr_err("PCI: Error interrupt on %s (PHB %d)\n",
	       hose->dn->full_name, hose->global_number);
 again:
	memset(&ed, 0, sizeof(ed));

	/* Read and clear UTL errors */
	ed.utl_sys_err = in_be64(hose->cfg_data + PCIE_UTL_SYS_BUS_AGENT_STATUS);
	if (ed.utl_sys_err)
		out_be64(hose->cfg_data + PCIE_UTL_SYS_BUS_AGENT_STATUS, ed.utl_sys_err);
	ed.utl_port_err = in_be64(hose->cfg_data + PCIE_UTL_PCIE_PORT_STATUS);
	if (ed.utl_port_err)
		out_be64(hose->cfg_data + PCIE_UTL_PCIE_PORT_STATUS, ed.utl_port_err);
	ed.utl_rc_err = in_be64(hose->cfg_data + PCIE_UTL_RC_STATUS);
	if (ed.utl_rc_err)
		out_be64(hose->cfg_data + PCIE_UTL_RC_STATUS, ed.utl_rc_err);

	/* Read and clear main trap errors */
	ed.phb_err = in_be64(hose->cfg_data + PCIE_REG_PHB_ERR_STATUS);
	if (ed.phb_err) {
		ed.phb_err1 = in_be64(hose->cfg_data + PCIE_REG_PHB_ERR1_STATUS);
		ed.phb_log0 = in_be64(hose->cfg_data + PCIE_REG_PHB_ERR_LOG_0);
		ed.phb_log1 = in_be64(hose->cfg_data + PCIE_REG_PHB_ERR_LOG_1);
		out_be64(hose->cfg_data + PCIE_REG_PHB_ERR1_STATUS, 0);
		out_be64(hose->cfg_data + PCIE_REG_PHB_ERR_STATUS, 0);
	}
	ed.mmio_err = in_be64(hose->cfg_data + PCIE_REG_MMIO_ERR_STATUS);
	if (ed.mmio_err) {
		ed.mmio_err1 = in_be64(hose->cfg_data + PCIE_REG_MMIO_ERR1_STATUS);
		ed.mmio_log0 = in_be64(hose->cfg_data + PCIE_REG_MMIO_ERR_LOG_0);
		ed.mmio_log1 = in_be64(hose->cfg_data + PCIE_REG_MMIO_ERR_LOG_1);
		out_be64(hose->cfg_data + PCIE_REG_MMIO_ERR1_STATUS, 0);
		out_be64(hose->cfg_data + PCIE_REG_MMIO_ERR_STATUS, 0);
	}
	ed.dma_err = in_be64(hose->cfg_data + PCIE_REG_DMA_ERR_STATUS);
	if (ed.dma_err) {
		ed.dma_err1 = in_be64(hose->cfg_data + PCIE_REG_DMA_ERR1_STATUS);
		ed.dma_log0 = in_be64(hose->cfg_data + PCIE_REG_DMA_ERR_LOG_0);
		ed.dma_log1 = in_be64(hose->cfg_data + PCIE_REG_DMA_ERR_LOG_1);
		out_be64(hose->cfg_data + PCIE_REG_DMA_ERR1_STATUS, 0);
		out_be64(hose->cfg_data + PCIE_REG_DMA_ERR_STATUS, 0);
	}

	/* Now print things out */
	if (ed.phb_err) {
		pr_err("   PHB Error Status      : 0x%016llx\n", ed.phb_err);
		pr_err("   PHB First Error Status: 0x%016llx\n", ed.phb_err1);
		pr_err("   PHB Error Log 0       : 0x%016llx\n", ed.phb_log0);
		pr_err("   PHB Error Log 1       : 0x%016llx\n", ed.phb_log1);
	}
	if (ed.mmio_err) {
		pr_err("  MMIO Error Status      : 0x%016llx\n", ed.mmio_err);
		pr_err("  MMIO First Error Status: 0x%016llx\n", ed.mmio_err1);
		pr_err("  MMIO Error Log 0       : 0x%016llx\n", ed.mmio_log0);
		pr_err("  MMIO Error Log 1       : 0x%016llx\n", ed.mmio_log1);
	}
	if (ed.dma_err) {
		pr_err("   DMA Error Status      : 0x%016llx\n", ed.dma_err);
		pr_err("   DMA First Error Status: 0x%016llx\n", ed.dma_err1);
		pr_err("   DMA Error Log 0       : 0x%016llx\n", ed.dma_log0);
		pr_err("   DMA Error Log 1       : 0x%016llx\n", ed.dma_log1);
	}
	if (ed.utl_sys_err)
		pr_err("   UTL Sys Error Status  : 0x%016llx\n", ed.utl_sys_err);
	if (ed.utl_port_err)
		pr_err("   UTL Port Error Status : 0x%016llx\n", ed.utl_port_err);
	if (ed.utl_rc_err)
		pr_err("   UTL RC Error Status   : 0x%016llx\n", ed.utl_rc_err);

	/* Interrupts are caused by the error traps. If we had any error there
	 * we loop again in case the UTL buffered some new stuff between
	 * going there and going to the traps
	 */
	if (ed.dma_err || ed.mmio_err || ed.phb_err) {
		handled = IRQ_HANDLED;
		goto again;
	}
	return handled;
}

static void __init wsp_setup_pci_err_reporting(struct wsp_phb *phb)
{
	struct pci_controller *hose = phb->hose;
	int err_irq, i, rc;
	char fname[16];

	/* Create a debugfs file for that PHB */
	sprintf(fname, "phb%d", phb->hose->global_number);
	phb->ddir = debugfs_create_dir(fname, powerpc_debugfs_root);

	/* Some useful debug output */
	if (phb->ddir) {
		struct dentry *d = debugfs_create_dir("regs", phb->ddir);
		char tmp[64];

		for (i = 0; i < ARRAY_SIZE(wsp_pci_regs); i++) {
			sprintf(tmp, "%03x_%s", wsp_pci_regs[i].offset,
				wsp_pci_regs[i].name);
			debugfs_create_file(tmp, 0600, d,
					    hose->cfg_data + wsp_pci_regs[i].offset,
					    &wsp_pci_reg_fops);
		}
		debugfs_create_file("all_regs", 0600, phb->ddir, phb, &wsp_pci_regs_fops);
	}

	/* Find the IRQ number for that PHB */
	err_irq = irq_of_parse_and_map(hose->dn, 0);
	if (err_irq == 0)
		/* XXX Error IRQ lacking from device-tree */
		err_irq = wsp_pci_get_err_irq_no_dt(hose->dn);
	if (err_irq == 0) {
		pr_err("PCI: Failed to fetch error interrupt for %s\n",
		       hose->dn->full_name);
		return;
	}
	/* Request it */
	rc = request_irq(err_irq, wsp_pci_err_irq, 0, "wsp_pci error", phb);
	if (rc) {
		pr_err("PCI: Failed to request interrupt for %s\n",
		       hose->dn->full_name);
	}
	/* Enable interrupts for all errors for now */
	out_be64(hose->cfg_data + PCIE_REG_PHB_ERR_IRQ_ENABLE, 0xffffffffffffffffull);
	out_be64(hose->cfg_data + PCIE_REG_MMIO_ERR_IRQ_ENABLE, 0xffffffffffffffffull);
	out_be64(hose->cfg_data + PCIE_REG_DMA_ERR_IRQ_ENABLE, 0xffffffffffffffffull);
}

/*
 * This is called later to hookup with the error interrupt
 */
static int __init wsp_setup_pci_late(void)
{
	struct wsp_phb *phb;

	list_for_each_entry(phb, &wsp_phbs, all)
		wsp_setup_pci_err_reporting(phb);

	return 0;
}
arch_initcall(wsp_setup_pci_late);
