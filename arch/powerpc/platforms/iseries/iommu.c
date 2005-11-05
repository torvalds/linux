/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 *
 * Rewrite, cleanup:
 *
 * Copyright (C) 2004 Olof Johansson <olof@austin.ibm.com>, IBM Corporation
 *
 * Dynamic DMA mapping support, iSeries-specific parts.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>

#include <asm/iommu.h>
#include <asm/tce.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include <asm/pci-bridge.h>
#include <asm/iseries/hv_call_xm.h>

extern struct list_head iSeries_Global_Device_List;


static void tce_build_iSeries(struct iommu_table *tbl, long index, long npages,
		unsigned long uaddr, enum dma_data_direction direction)
{
	u64 rc;
	union tce_entry tce;

	while (npages--) {
		tce.te_word = 0;
		tce.te_bits.tb_rpn = virt_to_abs(uaddr) >> PAGE_SHIFT;

		if (tbl->it_type == TCE_VB) {
			/* Virtual Bus */
			tce.te_bits.tb_valid = 1;
			tce.te_bits.tb_allio = 1;
			if (direction != DMA_TO_DEVICE)
				tce.te_bits.tb_rdwr = 1;
		} else {
			/* PCI Bus */
			tce.te_bits.tb_rdwr = 1; /* Read allowed */
			if (direction != DMA_TO_DEVICE)
				tce.te_bits.tb_pciwr = 1;
		}

		rc = HvCallXm_setTce((u64)tbl->it_index, (u64)index,
				tce.te_word);
		if (rc)
			panic("PCI_DMA: HvCallXm_setTce failed, Rc: 0x%lx\n",
					rc);
		index++;
		uaddr += PAGE_SIZE;
	}
}

static void tce_free_iSeries(struct iommu_table *tbl, long index, long npages)
{
	u64 rc;

	while (npages--) {
		rc = HvCallXm_setTce((u64)tbl->it_index, (u64)index, 0);
		if (rc)
			panic("PCI_DMA: HvCallXm_setTce failed, Rc: 0x%lx\n",
					rc);
		index++;
	}
}

#ifdef CONFIG_PCI
/*
 * This function compares the known tables to find an iommu_table
 * that has already been built for hardware TCEs.
 */
static struct iommu_table *iommu_table_find(struct iommu_table * tbl)
{
	struct pci_dn *pdn;

	list_for_each_entry(pdn, &iSeries_Global_Device_List, Device_List) {
		struct iommu_table *it = pdn->iommu_table;
		if ((it != NULL) &&
		    (it->it_type == TCE_PCI) &&
		    (it->it_offset == tbl->it_offset) &&
		    (it->it_index == tbl->it_index) &&
		    (it->it_size == tbl->it_size))
			return it;
	}
	return NULL;
}

/*
 * Call Hv with the architected data structure to get TCE table info.
 * info. Put the returned data into the Linux representation of the
 * TCE table data.
 * The Hardware Tce table comes in three flavors.
 * 1. TCE table shared between Buses.
 * 2. TCE table per Bus.
 * 3. TCE Table per IOA.
 */
static void iommu_table_getparms(struct pci_dn *pdn,
				 struct iommu_table* tbl)
{
	struct iommu_table_cb *parms;

	parms = kmalloc(sizeof(*parms), GFP_KERNEL);
	if (parms == NULL)
		panic("PCI_DMA: TCE Table Allocation failed.");

	memset(parms, 0, sizeof(*parms));

	parms->itc_busno = pdn->busno;
	parms->itc_slotno = pdn->LogicalSlot;
	parms->itc_virtbus = 0;

	HvCallXm_getTceTableParms(iseries_hv_addr(parms));

	if (parms->itc_size == 0)
		panic("PCI_DMA: parms->size is zero, parms is 0x%p", parms);

	/* itc_size is in pages worth of table, it_size is in # of entries */
	tbl->it_size = (parms->itc_size * PAGE_SIZE) / sizeof(union tce_entry);
	tbl->it_busno = parms->itc_busno;
	tbl->it_offset = parms->itc_offset;
	tbl->it_index = parms->itc_index;
	tbl->it_blocksize = 1;
	tbl->it_type = TCE_PCI;

	kfree(parms);
}


void iommu_devnode_init_iSeries(struct device_node *dn)
{
	struct iommu_table *tbl;
	struct pci_dn *pdn = PCI_DN(dn);

	tbl = kmalloc(sizeof(struct iommu_table), GFP_KERNEL);

	iommu_table_getparms(pdn, tbl);

	/* Look for existing tce table */
	pdn->iommu_table = iommu_table_find(tbl);
	if (pdn->iommu_table == NULL)
		pdn->iommu_table = iommu_init_table(tbl);
	else
		kfree(tbl);
}
#endif

static void iommu_dev_setup_iSeries(struct pci_dev *dev) { }
static void iommu_bus_setup_iSeries(struct pci_bus *bus) { }

void iommu_init_early_iSeries(void)
{
	ppc_md.tce_build = tce_build_iSeries;
	ppc_md.tce_free  = tce_free_iSeries;

	ppc_md.iommu_dev_setup = iommu_dev_setup_iSeries;
	ppc_md.iommu_bus_setup = iommu_bus_setup_iSeries;

	pci_iommu_init();
}
