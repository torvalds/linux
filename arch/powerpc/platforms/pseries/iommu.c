/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 *
 * Rewrite, cleanup:
 *
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 * Copyright (C) 2006 Olof Johansson <olof@lixom.net>
 *
 * Dynamic DMA mapping support, pSeries-specific parts, both SMP and LPAR.
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

#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include <asm/pSeries_reconfig.h>
#include <asm/firmware.h>
#include <asm/tce.h>
#include <asm/ppc-pci.h>
#include <asm/udbg.h>

#include "plpar_wrappers.h"

#define DBG(fmt...)

static void tce_build_pSeries(struct iommu_table *tbl, long index,
			      long npages, unsigned long uaddr,
			      enum dma_data_direction direction)
{
	u64 proto_tce;
	u64 *tcep;
	u64 rpn;

	proto_tce = TCE_PCI_READ; // Read allowed

	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	tcep = ((u64 *)tbl->it_base) + index;

	while (npages--) {
		/* can't move this out since we might cross LMB boundary */
		rpn = (virt_to_abs(uaddr)) >> TCE_SHIFT;
		*tcep = proto_tce | (rpn & TCE_RPN_MASK) << TCE_RPN_SHIFT;

		uaddr += TCE_PAGE_SIZE;
		tcep++;
	}
}


static void tce_free_pSeries(struct iommu_table *tbl, long index, long npages)
{
	u64 *tcep;

	tcep = ((u64 *)tbl->it_base) + index;

	while (npages--)
		*(tcep++) = 0;
}

static unsigned long tce_get_pseries(struct iommu_table *tbl, long index)
{
	u64 *tcep;

	tcep = ((u64 *)tbl->it_base) + index;

	return *tcep;
}

static void tce_build_pSeriesLP(struct iommu_table *tbl, long tcenum,
				long npages, unsigned long uaddr,
				enum dma_data_direction direction)
{
	u64 rc;
	u64 proto_tce, tce;
	u64 rpn;

	rpn = (virt_to_abs(uaddr)) >> TCE_SHIFT;
	proto_tce = TCE_PCI_READ;
	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	while (npages--) {
		tce = proto_tce | (rpn & TCE_RPN_MASK) << TCE_RPN_SHIFT;
		rc = plpar_tce_put((u64)tbl->it_index, (u64)tcenum << 12, tce);

		if (rc && printk_ratelimit()) {
			printk("tce_build_pSeriesLP: plpar_tce_put failed. rc=%ld\n", rc);
			printk("\tindex   = 0x%lx\n", (u64)tbl->it_index);
			printk("\ttcenum  = 0x%lx\n", (u64)tcenum);
			printk("\ttce val = 0x%lx\n", tce );
			show_stack(current, (unsigned long *)__get_SP());
		}

		tcenum++;
		rpn++;
	}
}

static DEFINE_PER_CPU(u64 *, tce_page) = NULL;

static void tce_buildmulti_pSeriesLP(struct iommu_table *tbl, long tcenum,
				     long npages, unsigned long uaddr,
				     enum dma_data_direction direction)
{
	u64 rc;
	u64 proto_tce;
	u64 *tcep;
	u64 rpn;
	long l, limit;

	if (npages == 1)
		return tce_build_pSeriesLP(tbl, tcenum, npages, uaddr,
					   direction);

	tcep = __get_cpu_var(tce_page);

	/* This is safe to do since interrupts are off when we're called
	 * from iommu_alloc{,_sg}()
	 */
	if (!tcep) {
		tcep = (u64 *)__get_free_page(GFP_ATOMIC);
		/* If allocation fails, fall back to the loop implementation */
		if (!tcep)
			return tce_build_pSeriesLP(tbl, tcenum, npages,
						   uaddr, direction);
		__get_cpu_var(tce_page) = tcep;
	}

	rpn = (virt_to_abs(uaddr)) >> TCE_SHIFT;
	proto_tce = TCE_PCI_READ;
	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	/* We can map max one pageful of TCEs at a time */
	do {
		/*
		 * Set up the page with TCE data, looping through and setting
		 * the values.
		 */
		limit = min_t(long, npages, 4096/TCE_ENTRY_SIZE);

		for (l = 0; l < limit; l++) {
			tcep[l] = proto_tce | (rpn & TCE_RPN_MASK) << TCE_RPN_SHIFT;
			rpn++;
		}

		rc = plpar_tce_put_indirect((u64)tbl->it_index,
					    (u64)tcenum << 12,
					    (u64)virt_to_abs(tcep),
					    limit);

		npages -= limit;
		tcenum += limit;
	} while (npages > 0 && !rc);

	if (rc && printk_ratelimit()) {
		printk("tce_buildmulti_pSeriesLP: plpar_tce_put failed. rc=%ld\n", rc);
		printk("\tindex   = 0x%lx\n", (u64)tbl->it_index);
		printk("\tnpages  = 0x%lx\n", (u64)npages);
		printk("\ttce[0] val = 0x%lx\n", tcep[0]);
		show_stack(current, (unsigned long *)__get_SP());
	}
}

static void tce_free_pSeriesLP(struct iommu_table *tbl, long tcenum, long npages)
{
	u64 rc;

	while (npages--) {
		rc = plpar_tce_put((u64)tbl->it_index, (u64)tcenum << 12, 0);

		if (rc && printk_ratelimit()) {
			printk("tce_free_pSeriesLP: plpar_tce_put failed. rc=%ld\n", rc);
			printk("\tindex   = 0x%lx\n", (u64)tbl->it_index);
			printk("\ttcenum  = 0x%lx\n", (u64)tcenum);
			show_stack(current, (unsigned long *)__get_SP());
		}

		tcenum++;
	}
}


static void tce_freemulti_pSeriesLP(struct iommu_table *tbl, long tcenum, long npages)
{
	u64 rc;

	rc = plpar_tce_stuff((u64)tbl->it_index, (u64)tcenum << 12, 0, npages);

	if (rc && printk_ratelimit()) {
		printk("tce_freemulti_pSeriesLP: plpar_tce_stuff failed\n");
		printk("\trc      = %ld\n", rc);
		printk("\tindex   = 0x%lx\n", (u64)tbl->it_index);
		printk("\tnpages  = 0x%lx\n", (u64)npages);
		show_stack(current, (unsigned long *)__get_SP());
	}
}

static unsigned long tce_get_pSeriesLP(struct iommu_table *tbl, long tcenum)
{
	u64 rc;
	unsigned long tce_ret;

	rc = plpar_tce_get((u64)tbl->it_index, (u64)tcenum << 12, &tce_ret);

	if (rc && printk_ratelimit()) {
		printk("tce_get_pSeriesLP: plpar_tce_get failed. rc=%ld\n",
			rc);
		printk("\tindex   = 0x%lx\n", (u64)tbl->it_index);
		printk("\ttcenum  = 0x%lx\n", (u64)tcenum);
		show_stack(current, (unsigned long *)__get_SP());
	}

	return tce_ret;
}

#ifdef CONFIG_PCI
static void iommu_table_setparms(struct pci_controller *phb,
				 struct device_node *dn,
				 struct iommu_table *tbl)
{
	struct device_node *node;
	const unsigned long *basep;
	const u32 *sizep;

	node = phb->dn;

	basep = of_get_property(node, "linux,tce-base", NULL);
	sizep = of_get_property(node, "linux,tce-size", NULL);
	if (basep == NULL || sizep == NULL) {
		printk(KERN_ERR "PCI_DMA: iommu_table_setparms: %s has "
				"missing tce entries !\n", dn->full_name);
		return;
	}

	tbl->it_base = (unsigned long)__va(*basep);

#ifndef CONFIG_CRASH_DUMP
	memset((void *)tbl->it_base, 0, *sizep);
#endif

	tbl->it_busno = phb->bus->number;

	/* Units of tce entries */
	tbl->it_offset = phb->dma_window_base_cur >> IOMMU_PAGE_SHIFT;

	/* Test if we are going over 2GB of DMA space */
	if (phb->dma_window_base_cur + phb->dma_window_size > 0x80000000ul) {
		udbg_printf("PCI_DMA: Unexpected number of IOAs under this PHB.\n");
		panic("PCI_DMA: Unexpected number of IOAs under this PHB.\n");
	}

	phb->dma_window_base_cur += phb->dma_window_size;

	/* Set the tce table size - measured in entries */
	tbl->it_size = phb->dma_window_size >> IOMMU_PAGE_SHIFT;

	tbl->it_index = 0;
	tbl->it_blocksize = 16;
	tbl->it_type = TCE_PCI;
}

/*
 * iommu_table_setparms_lpar
 *
 * Function: On pSeries LPAR systems, return TCE table info, given a pci bus.
 */
static void iommu_table_setparms_lpar(struct pci_controller *phb,
				      struct device_node *dn,
				      struct iommu_table *tbl,
				      const void *dma_window,
				      int bussubno)
{
	unsigned long offset, size;

	tbl->it_busno  = bussubno;
	of_parse_dma_window(dn, dma_window, &tbl->it_index, &offset, &size);

	tbl->it_base   = 0;
	tbl->it_blocksize  = 16;
	tbl->it_type = TCE_PCI;
	tbl->it_offset = offset >> IOMMU_PAGE_SHIFT;
	tbl->it_size = size >> IOMMU_PAGE_SHIFT;
}

static void pci_dma_bus_setup_pSeries(struct pci_bus *bus)
{
	struct device_node *dn;
	struct iommu_table *tbl;
	struct device_node *isa_dn, *isa_dn_orig;
	struct device_node *tmp;
	struct pci_dn *pci;
	int children;

	dn = pci_bus_to_OF_node(bus);

	DBG("pci_dma_bus_setup_pSeries: setting up bus %s\n", dn->full_name);

	if (bus->self) {
		/* This is not a root bus, any setup will be done for the
		 * device-side of the bridge in iommu_dev_setup_pSeries().
		 */
		return;
	}
	pci = PCI_DN(dn);

	/* Check if the ISA bus on the system is under
	 * this PHB.
	 */
	isa_dn = isa_dn_orig = of_find_node_by_type(NULL, "isa");

	while (isa_dn && isa_dn != dn)
		isa_dn = isa_dn->parent;

	if (isa_dn_orig)
		of_node_put(isa_dn_orig);

	/* Count number of direct PCI children of the PHB. */
	for (children = 0, tmp = dn->child; tmp; tmp = tmp->sibling)
		children++;

	DBG("Children: %d\n", children);

	/* Calculate amount of DMA window per slot. Each window must be
	 * a power of two (due to pci_alloc_consistent requirements).
	 *
	 * Keep 256MB aside for PHBs with ISA.
	 */

	if (!isa_dn) {
		/* No ISA/IDE - just set window size and return */
		pci->phb->dma_window_size = 0x80000000ul; /* To be divided */

		while (pci->phb->dma_window_size * children > 0x80000000ul)
			pci->phb->dma_window_size >>= 1;
		DBG("No ISA/IDE, window size is 0x%lx\n",
			pci->phb->dma_window_size);
		pci->phb->dma_window_base_cur = 0;

		return;
	}

	/* If we have ISA, then we probably have an IDE
	 * controller too. Allocate a 128MB table but
	 * skip the first 128MB to avoid stepping on ISA
	 * space.
	 */
	pci->phb->dma_window_size = 0x8000000ul;
	pci->phb->dma_window_base_cur = 0x8000000ul;

	tbl = kmalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
			   pci->phb->node);

	iommu_table_setparms(pci->phb, dn, tbl);
	pci->iommu_table = iommu_init_table(tbl, pci->phb->node);

	/* Divide the rest (1.75GB) among the children */
	pci->phb->dma_window_size = 0x80000000ul;
	while (pci->phb->dma_window_size * children > 0x70000000ul)
		pci->phb->dma_window_size >>= 1;

	DBG("ISA/IDE, window size is 0x%lx\n", pci->phb->dma_window_size);

}


static void pci_dma_bus_setup_pSeriesLP(struct pci_bus *bus)
{
	struct iommu_table *tbl;
	struct device_node *dn, *pdn;
	struct pci_dn *ppci;
	const void *dma_window = NULL;

	dn = pci_bus_to_OF_node(bus);

	DBG("pci_dma_bus_setup_pSeriesLP: setting up bus %s\n", dn->full_name);

	/* Find nearest ibm,dma-window, walking up the device tree */
	for (pdn = dn; pdn != NULL; pdn = pdn->parent) {
		dma_window = of_get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window != NULL)
			break;
	}

	if (dma_window == NULL) {
		DBG("  no ibm,dma-window property !\n");
		return;
	}

	ppci = PCI_DN(pdn);

	DBG("  parent is %s, iommu_table: 0x%p\n",
	    pdn->full_name, ppci->iommu_table);

	if (!ppci->iommu_table) {
		tbl = kmalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
				   ppci->phb->node);
		iommu_table_setparms_lpar(ppci->phb, pdn, tbl, dma_window,
			bus->number);
		ppci->iommu_table = iommu_init_table(tbl, ppci->phb->node);
		DBG("  created table: %p\n", ppci->iommu_table);
	}

	if (pdn != dn)
		PCI_DN(dn)->iommu_table = ppci->iommu_table;
}


static void pci_dma_dev_setup_pSeries(struct pci_dev *dev)
{
	struct device_node *dn;
	struct iommu_table *tbl;

	DBG("pci_dma_dev_setup_pSeries: %s\n", pci_name(dev));

	dn = dev->dev.archdata.of_node;

	/* If we're the direct child of a root bus, then we need to allocate
	 * an iommu table ourselves. The bus setup code should have setup
	 * the window sizes already.
	 */
	if (!dev->bus->self) {
		struct pci_controller *phb = PCI_DN(dn)->phb;

		DBG(" --> first child, no bridge. Allocating iommu table.\n");
		tbl = kmalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
				   phb->node);
		iommu_table_setparms(phb, dn, tbl);
		PCI_DN(dn)->iommu_table = iommu_init_table(tbl, phb->node);
		dev->dev.archdata.dma_data = PCI_DN(dn)->iommu_table;
		return;
	}

	/* If this device is further down the bus tree, search upwards until
	 * an already allocated iommu table is found and use that.
	 */

	while (dn && PCI_DN(dn) && PCI_DN(dn)->iommu_table == NULL)
		dn = dn->parent;

	if (dn && PCI_DN(dn))
		dev->dev.archdata.dma_data = PCI_DN(dn)->iommu_table;
	else
		printk(KERN_WARNING "iommu: Device %s has no iommu table\n",
		       pci_name(dev));
}

static void pci_dma_dev_setup_pSeriesLP(struct pci_dev *dev)
{
	struct device_node *pdn, *dn;
	struct iommu_table *tbl;
	const void *dma_window = NULL;
	struct pci_dn *pci;

	DBG("pci_dma_dev_setup_pSeriesLP: %s\n", pci_name(dev));

	/* dev setup for LPAR is a little tricky, since the device tree might
	 * contain the dma-window properties per-device and not neccesarily
	 * for the bus. So we need to search upwards in the tree until we
	 * either hit a dma-window property, OR find a parent with a table
	 * already allocated.
	 */
	dn = pci_device_to_OF_node(dev);
	DBG("  node is %s\n", dn->full_name);

	for (pdn = dn; pdn && PCI_DN(pdn) && !PCI_DN(pdn)->iommu_table;
	     pdn = pdn->parent) {
		dma_window = of_get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window)
			break;
	}

	if (!pdn || !PCI_DN(pdn)) {
		printk(KERN_WARNING "pci_dma_dev_setup_pSeriesLP: "
		       "no DMA window found for pci dev=%s dn=%s\n",
				 pci_name(dev), dn? dn->full_name : "<null>");
		return;
	}
	DBG("  parent is %s\n", pdn->full_name);

	/* Check for parent == NULL so we don't try to setup the empty EADS
	 * slots on POWER4 machines.
	 */
	if (dma_window == NULL || pdn->parent == NULL) {
		DBG("  no dma window for device, linking to parent\n");
		dev->dev.archdata.dma_data = PCI_DN(pdn)->iommu_table;
		return;
	}

	pci = PCI_DN(pdn);
	if (!pci->iommu_table) {
		tbl = kmalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
				   pci->phb->node);
		iommu_table_setparms_lpar(pci->phb, pdn, tbl, dma_window,
			pci->phb->bus->number);
		pci->iommu_table = iommu_init_table(tbl, pci->phb->node);
		DBG("  created table: %p\n", pci->iommu_table);
	} else {
		DBG("  found DMA window, table: %p\n", pci->iommu_table);
	}

	dev->dev.archdata.dma_data = pci->iommu_table;
}
#else  /* CONFIG_PCI */
#define pci_dma_bus_setup_pSeries	NULL
#define pci_dma_dev_setup_pSeries	NULL
#define pci_dma_bus_setup_pSeriesLP	NULL
#define pci_dma_dev_setup_pSeriesLP	NULL
#endif /* !CONFIG_PCI */

static int iommu_reconfig_notifier(struct notifier_block *nb, unsigned long action, void *node)
{
	int err = NOTIFY_OK;
	struct device_node *np = node;
	struct pci_dn *pci = PCI_DN(np);

	switch (action) {
	case PSERIES_RECONFIG_REMOVE:
		if (pci && pci->iommu_table &&
		    of_get_property(np, "ibm,dma-window", NULL))
			iommu_free_table(pci->iommu_table, np->full_name);
		break;
	default:
		err = NOTIFY_DONE;
		break;
	}
	return err;
}

static struct notifier_block iommu_reconfig_nb = {
	.notifier_call = iommu_reconfig_notifier,
};

/* These are called very early. */
void iommu_init_early_pSeries(void)
{
	if (of_chosen && of_get_property(of_chosen, "linux,iommu-off", NULL)) {
		/* Direct I/O, IOMMU off */
		ppc_md.pci_dma_dev_setup = NULL;
		ppc_md.pci_dma_bus_setup = NULL;
		set_pci_dma_ops(&dma_direct_ops);
		return;
	}

	if (firmware_has_feature(FW_FEATURE_LPAR)) {
		if (firmware_has_feature(FW_FEATURE_MULTITCE)) {
			ppc_md.tce_build = tce_buildmulti_pSeriesLP;
			ppc_md.tce_free	 = tce_freemulti_pSeriesLP;
		} else {
			ppc_md.tce_build = tce_build_pSeriesLP;
			ppc_md.tce_free	 = tce_free_pSeriesLP;
		}
		ppc_md.tce_get   = tce_get_pSeriesLP;
		ppc_md.pci_dma_bus_setup = pci_dma_bus_setup_pSeriesLP;
		ppc_md.pci_dma_dev_setup = pci_dma_dev_setup_pSeriesLP;
	} else {
		ppc_md.tce_build = tce_build_pSeries;
		ppc_md.tce_free  = tce_free_pSeries;
		ppc_md.tce_get   = tce_get_pseries;
		ppc_md.pci_dma_bus_setup = pci_dma_bus_setup_pSeries;
		ppc_md.pci_dma_dev_setup = pci_dma_dev_setup_pSeries;
	}


	pSeries_reconfig_notifier_register(&iommu_reconfig_nb);

	set_pci_dma_ops(&dma_iommu_ops);
}

