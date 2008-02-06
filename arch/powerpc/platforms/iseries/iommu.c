/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 *
 * Rewrite, cleanup:
 *
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 * Copyright (C) 2006 Olof Johansson <olof@lixom.net>
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
#include <linux/pci.h>
#include <linux/module.h>

#include <asm/iommu.h>
#include <asm/vio.h>
#include <asm/tce.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/iseries/hv_call_xm.h>
#include <asm/iseries/hv_call_event.h>
#include <asm/iseries/iommu.h>

static void tce_build_iSeries(struct iommu_table *tbl, long index, long npages,
		unsigned long uaddr, enum dma_data_direction direction)
{
	u64 rc;
	u64 tce, rpn;

	while (npages--) {
		rpn = virt_to_abs(uaddr) >> TCE_SHIFT;
		tce = (rpn & TCE_RPN_MASK) << TCE_RPN_SHIFT;

		if (tbl->it_type == TCE_VB) {
			/* Virtual Bus */
			tce |= TCE_VALID|TCE_ALLIO;
			if (direction != DMA_TO_DEVICE)
				tce |= TCE_VB_WRITE;
		} else {
			/* PCI Bus */
			tce |= TCE_PCI_READ; /* Read allowed */
			if (direction != DMA_TO_DEVICE)
				tce |= TCE_PCI_WRITE;
		}

		rc = HvCallXm_setTce((u64)tbl->it_index, (u64)index, tce);
		if (rc)
			panic("PCI_DMA: HvCallXm_setTce failed, Rc: 0x%lx\n",
					rc);
		index++;
		uaddr += TCE_PAGE_SIZE;
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

/*
 * Structure passed to HvCallXm_getTceTableParms
 */
struct iommu_table_cb {
	unsigned long	itc_busno;	/* Bus number for this tce table */
	unsigned long	itc_start;	/* Will be NULL for secondary */
	unsigned long	itc_totalsize;	/* Size (in pages) of whole table */
	unsigned long	itc_offset;	/* Index into real tce table of the
					   start of our section */
	unsigned long	itc_size;	/* Size (in pages) of our section */
	unsigned long	itc_index;	/* Index of this tce table */
	unsigned short	itc_maxtables;	/* Max num of tables for partition */
	unsigned char	itc_virtbus;	/* Flag to indicate virtual bus */
	unsigned char	itc_slotno;	/* IOA Tce Slot Index */
	unsigned char	itc_rsvd[4];
};

/*
 * Call Hv with the architected data structure to get TCE table info.
 * info. Put the returned data into the Linux representation of the
 * TCE table data.
 * The Hardware Tce table comes in three flavors.
 * 1. TCE table shared between Buses.
 * 2. TCE table per Bus.
 * 3. TCE Table per IOA.
 */
void iommu_table_getparms_iSeries(unsigned long busno,
				  unsigned char slotno,
				  unsigned char virtbus,
				  struct iommu_table* tbl)
{
	struct iommu_table_cb *parms;

	parms = kzalloc(sizeof(*parms), GFP_KERNEL);
	if (parms == NULL)
		panic("PCI_DMA: TCE Table Allocation failed.");

	parms->itc_busno = busno;
	parms->itc_slotno = slotno;
	parms->itc_virtbus = virtbus;

	HvCallXm_getTceTableParms(iseries_hv_addr(parms));

	if (parms->itc_size == 0)
		panic("PCI_DMA: parms->size is zero, parms is 0x%p", parms);

	/* itc_size is in pages worth of table, it_size is in # of entries */
	tbl->it_size = (parms->itc_size * TCE_PAGE_SIZE) / TCE_ENTRY_SIZE;
	tbl->it_busno = parms->itc_busno;
	tbl->it_offset = parms->itc_offset;
	tbl->it_index = parms->itc_index;
	tbl->it_blocksize = 1;
	tbl->it_type = virtbus ? TCE_VB : TCE_PCI;

	kfree(parms);
}


#ifdef CONFIG_PCI
/*
 * This function compares the known tables to find an iommu_table
 * that has already been built for hardware TCEs.
 */
static struct iommu_table *iommu_table_find(struct iommu_table * tbl)
{
	struct device_node *node;

	for (node = NULL; (node = of_find_all_nodes(node)); ) {
		struct pci_dn *pdn = PCI_DN(node);
		struct iommu_table *it;

		if (pdn == NULL)
			continue;
		it = pdn->iommu_table;
		if ((it != NULL) &&
		    (it->it_type == TCE_PCI) &&
		    (it->it_offset == tbl->it_offset) &&
		    (it->it_index == tbl->it_index) &&
		    (it->it_size == tbl->it_size)) {
			of_node_put(node);
			return it;
		}
	}
	return NULL;
}


void iommu_devnode_init_iSeries(struct pci_dev *pdev, struct device_node *dn)
{
	struct iommu_table *tbl;
	struct pci_dn *pdn = PCI_DN(dn);
	const u32 *lsn = of_get_property(dn, "linux,logical-slot-number", NULL);

	BUG_ON(lsn == NULL);

	tbl = kmalloc(sizeof(struct iommu_table), GFP_KERNEL);

	iommu_table_getparms_iSeries(pdn->busno, *lsn, 0, tbl);

	/* Look for existing tce table */
	pdn->iommu_table = iommu_table_find(tbl);
	if (pdn->iommu_table == NULL)
		pdn->iommu_table = iommu_init_table(tbl, -1);
	else
		kfree(tbl);
	pdev->dev.archdata.dma_data = pdn->iommu_table;
}
#endif

static struct iommu_table veth_iommu_table;
static struct iommu_table vio_iommu_table;

void *iseries_hv_alloc(size_t size, dma_addr_t *dma_handle, gfp_t flag)
{
	return iommu_alloc_coherent(NULL, &vio_iommu_table, size, dma_handle,
				DMA_32BIT_MASK, flag, -1);
}
EXPORT_SYMBOL_GPL(iseries_hv_alloc);

void iseries_hv_free(size_t size, void *vaddr, dma_addr_t dma_handle)
{
	iommu_free_coherent(&vio_iommu_table, size, vaddr, dma_handle);
}
EXPORT_SYMBOL_GPL(iseries_hv_free);

dma_addr_t iseries_hv_map(void *vaddr, size_t size,
			enum dma_data_direction direction)
{
	return iommu_map_single(NULL, &vio_iommu_table, vaddr, size,
				DMA_32BIT_MASK, direction);
}

void iseries_hv_unmap(dma_addr_t dma_handle, size_t size,
			enum dma_data_direction direction)
{
	iommu_unmap_single(&vio_iommu_table, dma_handle, size, direction);
}

void __init iommu_vio_init(void)
{
	iommu_table_getparms_iSeries(255, 0, 0xff, &veth_iommu_table);
	veth_iommu_table.it_size /= 2;
	vio_iommu_table = veth_iommu_table;
	vio_iommu_table.it_offset += veth_iommu_table.it_size;

	if (!iommu_init_table(&veth_iommu_table, -1))
		printk("Virtual Bus VETH TCE table failed.\n");
	if (!iommu_init_table(&vio_iommu_table, -1))
		printk("Virtual Bus VIO TCE table failed.\n");
}

struct iommu_table *vio_build_iommu_table_iseries(struct vio_dev *dev)
{
	if (strcmp(dev->type, "network") == 0)
		return &veth_iommu_table;
	return &vio_iommu_table;
}

void iommu_init_early_iSeries(void)
{
	ppc_md.tce_build = tce_build_iSeries;
	ppc_md.tce_free  = tce_free_iSeries;

	set_pci_dma_ops(&dma_iommu_ops);
}
