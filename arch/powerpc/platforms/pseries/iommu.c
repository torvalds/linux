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

#include <linux/config.h>
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

	index <<= TCE_PAGE_FACTOR;
	npages <<= TCE_PAGE_FACTOR;

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

	npages <<= TCE_PAGE_FACTOR;
	index <<= TCE_PAGE_FACTOR;

	tcep = ((u64 *)tbl->it_base) + index;

	while (npages--)
		*(tcep++) = 0;
}


static void tce_build_pSeriesLP(struct iommu_table *tbl, long tcenum,
				long npages, unsigned long uaddr,
				enum dma_data_direction direction)
{
	u64 rc;
	u64 proto_tce, tce;
	u64 rpn;

	tcenum <<= TCE_PAGE_FACTOR;
	npages <<= TCE_PAGE_FACTOR;

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

	if (TCE_PAGE_FACTOR == 0 && npages == 1)
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

	tcenum <<= TCE_PAGE_FACTOR;
	npages <<= TCE_PAGE_FACTOR;

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

	tcenum <<= TCE_PAGE_FACTOR;
	npages <<= TCE_PAGE_FACTOR;

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

	tcenum <<= TCE_PAGE_FACTOR;
	npages <<= TCE_PAGE_FACTOR;

	rc = plpar_tce_stuff((u64)tbl->it_index, (u64)tcenum << 12, 0, npages);

	if (rc && printk_ratelimit()) {
		printk("tce_freemulti_pSeriesLP: plpar_tce_stuff failed\n");
		printk("\trc      = %ld\n", rc);
		printk("\tindex   = 0x%lx\n", (u64)tbl->it_index);
		printk("\tnpages  = 0x%lx\n", (u64)npages);
		show_stack(current, (unsigned long *)__get_SP());
	}
}

static void iommu_table_setparms(struct pci_controller *phb,
				 struct device_node *dn,
				 struct iommu_table *tbl)
{
	struct device_node *node;
	unsigned long *basep;
	unsigned int *sizep;

	node = (struct device_node *)phb->arch_data;

	basep = (unsigned long *)get_property(node, "linux,tce-base", NULL);
	sizep = (unsigned int *)get_property(node, "linux,tce-size", NULL);
	if (basep == NULL || sizep == NULL) {
		printk(KERN_ERR "PCI_DMA: iommu_table_setparms: %s has "
				"missing tce entries !\n", dn->full_name);
		return;
	}

	tbl->it_base = (unsigned long)__va(*basep);
	memset((void *)tbl->it_base, 0, *sizep);

	tbl->it_busno = phb->bus->number;

	/* Units of tce entries */
	tbl->it_offset = phb->dma_window_base_cur >> PAGE_SHIFT;

	/* Test if we are going over 2GB of DMA space */
	if (phb->dma_window_base_cur + phb->dma_window_size > 0x80000000ul) {
		udbg_printf("PCI_DMA: Unexpected number of IOAs under this PHB.\n");
		panic("PCI_DMA: Unexpected number of IOAs under this PHB.\n");
	}

	phb->dma_window_base_cur += phb->dma_window_size;

	/* Set the tce table size - measured in entries */
	tbl->it_size = phb->dma_window_size >> PAGE_SHIFT;

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
				      unsigned char *dma_window)
{
	unsigned long offset, size;

	tbl->it_busno  = PCI_DN(dn)->bussubno;
	of_parse_dma_window(dn, dma_window, &tbl->it_index, &offset, &size);

	tbl->it_base   = 0;
	tbl->it_blocksize  = 16;
	tbl->it_type = TCE_PCI;
	tbl->it_offset = offset >> PAGE_SHIFT;
	tbl->it_size = size >> PAGE_SHIFT;
}

static void iommu_bus_setup_pSeries(struct pci_bus *bus)
{
	struct device_node *dn;
	struct iommu_table *tbl;
	struct device_node *isa_dn, *isa_dn_orig;
	struct device_node *tmp;
	struct pci_dn *pci;
	int children;

	DBG("iommu_bus_setup_pSeries, bus %p, bus->self %p\n", bus, bus->self);

	dn = pci_bus_to_OF_node(bus);
	pci = PCI_DN(dn);

	if (bus->self) {
		/* This is not a root bus, any setup will be done for the
		 * device-side of the bridge in iommu_dev_setup_pSeries().
		 */
		return;
	}

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


static void iommu_bus_setup_pSeriesLP(struct pci_bus *bus)
{
	struct iommu_table *tbl;
	struct device_node *dn, *pdn;
	struct pci_dn *ppci;
	unsigned char *dma_window = NULL;

	DBG("iommu_bus_setup_pSeriesLP, bus %p, bus->self %p\n", bus, bus->self);

	dn = pci_bus_to_OF_node(bus);

	/* Find nearest ibm,dma-window, walking up the device tree */
	for (pdn = dn; pdn != NULL; pdn = pdn->parent) {
		dma_window = get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window != NULL)
			break;
	}

	if (dma_window == NULL) {
		DBG("iommu_bus_setup_pSeriesLP: bus %s seems to have no ibm,dma-window property\n", dn->full_name);
		return;
	}

	ppci = PCI_DN(pdn);
	if (!ppci->iommu_table) {
		/* Bussubno hasn't been copied yet.
		 * Do it now because iommu_table_setparms_lpar needs it.
		 */

		ppci->bussubno = bus->number;

		tbl = kmalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
				   ppci->phb->node);

		iommu_table_setparms_lpar(ppci->phb, pdn, tbl, dma_window);

		ppci->iommu_table = iommu_init_table(tbl, ppci->phb->node);
	}

	if (pdn != dn)
		PCI_DN(dn)->iommu_table = ppci->iommu_table;
}


static void iommu_dev_setup_pSeries(struct pci_dev *dev)
{
	struct device_node *dn, *mydn;
	struct iommu_table *tbl;

	DBG("iommu_dev_setup_pSeries, dev %p (%s)\n", dev, pci_name(dev));

	mydn = dn = pci_device_to_OF_node(dev);

	/* If we're the direct child of a root bus, then we need to allocate
	 * an iommu table ourselves. The bus setup code should have setup
	 * the window sizes already.
	 */
	if (!dev->bus->self) {
		DBG(" --> first child, no bridge. Allocating iommu table.\n");
		tbl = kmalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
				   PCI_DN(dn)->phb->node);
		iommu_table_setparms(PCI_DN(dn)->phb, dn, tbl);
		PCI_DN(dn)->iommu_table = iommu_init_table(tbl,
						PCI_DN(dn)->phb->node);

		return;
	}

	/* If this device is further down the bus tree, search upwards until
	 * an already allocated iommu table is found and use that.
	 */

	while (dn && PCI_DN(dn) && PCI_DN(dn)->iommu_table == NULL)
		dn = dn->parent;

	if (dn && PCI_DN(dn)) {
		PCI_DN(mydn)->iommu_table = PCI_DN(dn)->iommu_table;
	} else {
		DBG("iommu_dev_setup_pSeries, dev %p (%s) has no iommu table\n", dev, pci_name(dev));
	}
}

static int iommu_reconfig_notifier(struct notifier_block *nb, unsigned long action, void *node)
{
	int err = NOTIFY_OK;
	struct device_node *np = node;
	struct pci_dn *pci = PCI_DN(np);

	switch (action) {
	case PSERIES_RECONFIG_REMOVE:
		if (pci && pci->iommu_table &&
		    get_property(np, "ibm,dma-window", NULL))
			iommu_free_table(np);
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

static void iommu_dev_setup_pSeriesLP(struct pci_dev *dev)
{
	struct device_node *pdn, *dn;
	struct iommu_table *tbl;
	unsigned char *dma_window = NULL;
	struct pci_dn *pci;

	DBG("iommu_dev_setup_pSeriesLP, dev %p (%s)\n", dev, pci_name(dev));

	/* dev setup for LPAR is a little tricky, since the device tree might
	 * contain the dma-window properties per-device and not neccesarily
	 * for the bus. So we need to search upwards in the tree until we
	 * either hit a dma-window property, OR find a parent with a table
	 * already allocated.
	 */
	dn = pci_device_to_OF_node(dev);

	for (pdn = dn; pdn && PCI_DN(pdn) && !PCI_DN(pdn)->iommu_table;
	     pdn = pdn->parent) {
		dma_window = get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window)
			break;
	}

	/* Check for parent == NULL so we don't try to setup the empty EADS
	 * slots on POWER4 machines.
	 */
	if (dma_window == NULL || pdn->parent == NULL) {
		DBG("No dma window for device, linking to parent\n");
		PCI_DN(dn)->iommu_table = PCI_DN(pdn)->iommu_table;
		return;
	} else {
		DBG("Found DMA window, allocating table\n");
	}

	pci = PCI_DN(pdn);
	if (!pci->iommu_table) {
		/* iommu_table_setparms_lpar needs bussubno. */
		pci->bussubno = pci->phb->bus->number;

		tbl = kmalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
				   pci->phb->node);

		iommu_table_setparms_lpar(pci->phb, pdn, tbl, dma_window);

		pci->iommu_table = iommu_init_table(tbl, pci->phb->node);
	}

	if (pdn != dn)
		PCI_DN(dn)->iommu_table = pci->iommu_table;
}

static void iommu_bus_setup_null(struct pci_bus *b) { }
static void iommu_dev_setup_null(struct pci_dev *d) { }

/* These are called very early. */
void iommu_init_early_pSeries(void)
{
	if (of_chosen && get_property(of_chosen, "linux,iommu-off", NULL)) {
		/* Direct I/O, IOMMU off */
		ppc_md.iommu_dev_setup = iommu_dev_setup_null;
		ppc_md.iommu_bus_setup = iommu_bus_setup_null;
		pci_direct_iommu_init();

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
		ppc_md.iommu_bus_setup = iommu_bus_setup_pSeriesLP;
		ppc_md.iommu_dev_setup = iommu_dev_setup_pSeriesLP;
	} else {
		ppc_md.tce_build = tce_build_pSeries;
		ppc_md.tce_free  = tce_free_pSeries;
		ppc_md.iommu_bus_setup = iommu_bus_setup_pSeries;
		ppc_md.iommu_dev_setup = iommu_dev_setup_pSeries;
	}


	pSeries_reconfig_notifier_register(&iommu_reconfig_nb);

	pci_iommu_init();
}

