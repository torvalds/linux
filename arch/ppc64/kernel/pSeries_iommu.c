/*
 * arch/ppc64/kernel/pSeries_iommu.c
 *
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 *
 * Rewrite, cleanup: 
 *
 * Copyright (C) 2004 Olof Johansson <olof@austin.ibm.com>, IBM Corporation
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
#include <asm/ppcdebug.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include <asm/plpar_wrappers.h>
#include <asm/pSeries_reconfig.h>
#include <asm/systemcfg.h>
#include "pci.h"

#define DBG(fmt...)

extern int is_python(struct device_node *);

static void tce_build_pSeries(struct iommu_table *tbl, long index, 
			      long npages, unsigned long uaddr, 
			      enum dma_data_direction direction)
{
	union tce_entry t;
	union tce_entry *tp;

	t.te_word = 0;
	t.te_rdwr = 1; // Read allowed 

	if (direction != DMA_TO_DEVICE)
		t.te_pciwr = 1;

	tp = ((union tce_entry *)tbl->it_base) + index;

	while (npages--) {
		/* can't move this out since we might cross LMB boundary */
		t.te_rpn = (virt_to_abs(uaddr)) >> PAGE_SHIFT;
	
		tp->te_word = t.te_word;

		uaddr += PAGE_SIZE;
		tp++;
	}
}


static void tce_free_pSeries(struct iommu_table *tbl, long index, long npages)
{
	union tce_entry t;
	union tce_entry *tp;

	t.te_word = 0;
	tp  = ((union tce_entry *)tbl->it_base) + index;
		
	while (npages--) {
		tp->te_word = t.te_word;
		
		tp++;
	}
}


static void tce_build_pSeriesLP(struct iommu_table *tbl, long tcenum,
				long npages, unsigned long uaddr,
				enum dma_data_direction direction)
{
	u64 rc;
	union tce_entry tce;

	tce.te_word = 0;
	tce.te_rpn = (virt_to_abs(uaddr)) >> PAGE_SHIFT;
	tce.te_rdwr = 1;
	if (direction != DMA_TO_DEVICE)
		tce.te_pciwr = 1;

	while (npages--) {
		rc = plpar_tce_put((u64)tbl->it_index, 
				   (u64)tcenum << 12, 
				   tce.te_word );
		
		if (rc && printk_ratelimit()) {
			printk("tce_build_pSeriesLP: plpar_tce_put failed. rc=%ld\n", rc);
			printk("\tindex   = 0x%lx\n", (u64)tbl->it_index);
			printk("\ttcenum  = 0x%lx\n", (u64)tcenum);
			printk("\ttce val = 0x%lx\n", tce.te_word );
			show_stack(current, (unsigned long *)__get_SP());
		}
			
		tcenum++;
		tce.te_rpn++;
	}
}

static DEFINE_PER_CPU(void *, tce_page) = NULL;

static void tce_buildmulti_pSeriesLP(struct iommu_table *tbl, long tcenum,
				     long npages, unsigned long uaddr,
				     enum dma_data_direction direction)
{
	u64 rc;
	union tce_entry tce, *tcep;
	long l, limit;

	if (npages == 1)
		return tce_build_pSeriesLP(tbl, tcenum, npages, uaddr,
					   direction);

	tcep = __get_cpu_var(tce_page);

	/* This is safe to do since interrupts are off when we're called
	 * from iommu_alloc{,_sg}()
	 */
	if (!tcep) {
		tcep = (void *)__get_free_page(GFP_ATOMIC);
		/* If allocation fails, fall back to the loop implementation */
		if (!tcep)
			return tce_build_pSeriesLP(tbl, tcenum, npages,
						   uaddr, direction);
		__get_cpu_var(tce_page) = tcep;
	}

	tce.te_word = 0;
	tce.te_rpn = (virt_to_abs(uaddr)) >> PAGE_SHIFT;
	tce.te_rdwr = 1;
	if (direction != DMA_TO_DEVICE)
		tce.te_pciwr = 1;

	/* We can map max one pageful of TCEs at a time */
	do {
		/*
		 * Set up the page with TCE data, looping through and setting
		 * the values.
		 */
		limit = min_t(long, npages, PAGE_SIZE/sizeof(union tce_entry));

		for (l = 0; l < limit; l++) {
			tcep[l] = tce;
			tce.te_rpn++;
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
		printk("\ttce[0] val = 0x%lx\n", tcep[0].te_word);
		show_stack(current, (unsigned long *)__get_SP());
	}
}

static void tce_free_pSeriesLP(struct iommu_table *tbl, long tcenum, long npages)
{
	u64 rc;
	union tce_entry tce;

	tce.te_word = 0;

	while (npages--) {
		rc = plpar_tce_put((u64)tbl->it_index,
				   (u64)tcenum << 12,
				   tce.te_word);

		if (rc && printk_ratelimit()) {
			printk("tce_free_pSeriesLP: plpar_tce_put failed. rc=%ld\n", rc);
			printk("\tindex   = 0x%lx\n", (u64)tbl->it_index);
			printk("\ttcenum  = 0x%lx\n", (u64)tcenum);
			printk("\ttce val = 0x%lx\n", tce.te_word );
			show_stack(current, (unsigned long *)__get_SP());
		}

		tcenum++;
	}
}


static void tce_freemulti_pSeriesLP(struct iommu_table *tbl, long tcenum, long npages)
{
	u64 rc;
	union tce_entry tce;

	tce.te_word = 0;

	rc = plpar_tce_stuff((u64)tbl->it_index,
			   (u64)tcenum << 12,
			   tce.te_word,
			   npages);

	if (rc && printk_ratelimit()) {
		printk("tce_freemulti_pSeriesLP: plpar_tce_stuff failed\n");
		printk("\trc      = %ld\n", rc);
		printk("\tindex   = 0x%lx\n", (u64)tbl->it_index);
		printk("\tnpages  = 0x%lx\n", (u64)npages);
		printk("\ttce val = 0x%lx\n", tce.te_word );
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
	if (phb->dma_window_base_cur + phb->dma_window_size > (1L << 31))
		panic("PCI_DMA: Unexpected number of IOAs under this PHB.\n"); 
	
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
 *
 * ToDo: properly interpret the ibm,dma-window property.  The definition is:
 *	logical-bus-number	(1 word)
 *	phys-address		(#address-cells words)
 *	size			(#cell-size words)
 *
 * Currently we hard code these sizes (more or less).
 */
static void iommu_table_setparms_lpar(struct pci_controller *phb,
				      struct device_node *dn,
				      struct iommu_table *tbl,
				      unsigned int *dma_window)
{
	tbl->it_busno  = dn->bussubno;

	/* TODO: Parse field size properties properly. */
	tbl->it_size   = (((unsigned long)dma_window[4] << 32) |
			   (unsigned long)dma_window[5]) >> PAGE_SHIFT;
	tbl->it_offset = (((unsigned long)dma_window[2] << 32) |
			   (unsigned long)dma_window[3]) >> PAGE_SHIFT;
	tbl->it_base   = 0;
	tbl->it_index  = dma_window[0];
	tbl->it_blocksize  = 16;
	tbl->it_type = TCE_PCI;
}

static void iommu_bus_setup_pSeries(struct pci_bus *bus)
{
	struct device_node *dn, *pdn;
	struct iommu_table *tbl;

	DBG("iommu_bus_setup_pSeries, bus %p, bus->self %p\n", bus, bus->self);

	/* For each (root) bus, we carve up the available DMA space in 256MB
	 * pieces. Since each piece is used by one (sub) bus/device, that would
	 * give a maximum of 7 devices per PHB. In most cases, this is plenty.
	 *
	 * The exception is on Python PHBs (pre-POWER4). Here we don't have EADS
	 * bridges below the PHB to allocate the sectioned tables to, so instead
	 * we allocate a 1GB table at the PHB level.
	 */

	dn = pci_bus_to_OF_node(bus);

	if (!bus->self) {
		/* Root bus */
		if (is_python(dn)) {
			unsigned int *iohole;

			DBG("Python root bus %s\n", bus->name);

			iohole = (unsigned int *)get_property(dn, "io-hole", 0);

			if (iohole) {
				/* On first bus we need to leave room for the
				 * ISA address space. Just skip the first 256MB
				 * alltogether. This leaves 768MB for the window.
				 */
				DBG("PHB has io-hole, reserving 256MB\n");
				dn->phb->dma_window_size = 3 << 28;
				dn->phb->dma_window_base_cur = 1 << 28;
			} else {
				/* 1GB window by default */
				dn->phb->dma_window_size = 1 << 30;
				dn->phb->dma_window_base_cur = 0;
			}

			tbl = kmalloc(sizeof(struct iommu_table), GFP_KERNEL);

			iommu_table_setparms(dn->phb, dn, tbl);
			dn->iommu_table = iommu_init_table(tbl);
		} else {
			/* Do a 128MB table at root. This is used for the IDE
			 * controller on some SMP-mode POWER4 machines. It
			 * doesn't hurt to allocate it on other machines
			 * -- it'll just be unused since new tables are
			 * allocated on the EADS level.
			 *
			 * Allocate at offset 128MB to avoid having to deal
			 * with ISA holes; 128MB table for IDE is plenty.
			 */
			dn->phb->dma_window_size = 1 << 27;
			dn->phb->dma_window_base_cur = 1 << 27;

			tbl = kmalloc(sizeof(struct iommu_table), GFP_KERNEL);

			iommu_table_setparms(dn->phb, dn, tbl);
			dn->iommu_table = iommu_init_table(tbl);

			/* All child buses have 256MB tables */
			dn->phb->dma_window_size = 1 << 28;
		}
	} else {
		pdn = pci_bus_to_OF_node(bus->parent);

		if (!bus->parent->self && !is_python(pdn)) {
			struct iommu_table *tbl;
			/* First child and not python means this is the EADS
			 * level. Allocate new table for this slot with 256MB
			 * window.
			 */

			tbl = kmalloc(sizeof(struct iommu_table), GFP_KERNEL);

			iommu_table_setparms(dn->phb, dn, tbl);

			dn->iommu_table = iommu_init_table(tbl);
		} else {
			/* Lower than first child or under python, use parent table */
			dn->iommu_table = pdn->iommu_table;
		}
	}
}


static void iommu_bus_setup_pSeriesLP(struct pci_bus *bus)
{
	struct iommu_table *tbl;
	struct device_node *dn, *pdn;
	unsigned int *dma_window = NULL;

	DBG("iommu_bus_setup_pSeriesLP, bus %p, bus->self %p\n", bus, bus->self);

	dn = pci_bus_to_OF_node(bus);

	/* Find nearest ibm,dma-window, walking up the device tree */
	for (pdn = dn; pdn != NULL; pdn = pdn->parent) {
		dma_window = (unsigned int *)get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window != NULL)
			break;
	}

	if (dma_window == NULL) {
		DBG("iommu_bus_setup_pSeriesLP: bus %s seems to have no ibm,dma-window property\n", dn->full_name);
		return;
	}

	if (!pdn->iommu_table) {
		/* Bussubno hasn't been copied yet.
		 * Do it now because iommu_table_setparms_lpar needs it.
		 */
		pdn->bussubno = bus->number;

		tbl = (struct iommu_table *)kmalloc(sizeof(struct iommu_table),
						    GFP_KERNEL);
	
		iommu_table_setparms_lpar(pdn->phb, pdn, tbl, dma_window);

		pdn->iommu_table = iommu_init_table(tbl);
	}

	if (pdn != dn)
		dn->iommu_table = pdn->iommu_table;
}


static void iommu_dev_setup_pSeries(struct pci_dev *dev)
{
	struct device_node *dn, *mydn;

	DBG("iommu_dev_setup_pSeries, dev %p (%s)\n", dev, dev->pretty_name);
	/* Now copy the iommu_table ptr from the bus device down to the
	 * pci device_node.  This means get_iommu_table() won't need to search
	 * up the device tree to find it.
	 */
	mydn = dn = pci_device_to_OF_node(dev);

	while (dn && dn->iommu_table == NULL)
		dn = dn->parent;

	if (dn) {
		mydn->iommu_table = dn->iommu_table;
	} else {
		DBG("iommu_dev_setup_pSeries, dev %p (%s) has no iommu table\n", dev, dev->pretty_name);
	}
}

static int iommu_reconfig_notifier(struct notifier_block *nb, unsigned long action, void *node)
{
	int err = NOTIFY_OK;
	struct device_node *np = node;

	switch (action) {
	case PSERIES_RECONFIG_REMOVE:
		if (np->iommu_table &&
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
	int *dma_window = NULL;

	DBG("iommu_dev_setup_pSeriesLP, dev %p (%s)\n", dev, dev->pretty_name);

	/* dev setup for LPAR is a little tricky, since the device tree might
	 * contain the dma-window properties per-device and not neccesarily
	 * for the bus. So we need to search upwards in the tree until we
	 * either hit a dma-window property, OR find a parent with a table
	 * already allocated.
	 */
	dn = pci_device_to_OF_node(dev);

	for (pdn = dn; pdn && !pdn->iommu_table; pdn = pdn->parent) {
		dma_window = (unsigned int *)get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window)
			break;
	}

	/* Check for parent == NULL so we don't try to setup the empty EADS
	 * slots on POWER4 machines.
	 */
	if (dma_window == NULL || pdn->parent == NULL) {
		/* Fall back to regular (non-LPAR) dev setup */
		DBG("No dma window for device, falling back to regular setup\n");
		iommu_dev_setup_pSeries(dev);
		return;
	} else {
		DBG("Found DMA window, allocating table\n");
	}

	if (!pdn->iommu_table) {
		/* iommu_table_setparms_lpar needs bussubno. */
		pdn->bussubno = pdn->phb->bus->number;

		tbl = (struct iommu_table *)kmalloc(sizeof(struct iommu_table),
						    GFP_KERNEL);

		iommu_table_setparms_lpar(pdn->phb, pdn, tbl, dma_window);

		pdn->iommu_table = iommu_init_table(tbl);
	}

	if (pdn != dn)
		dn->iommu_table = pdn->iommu_table;
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

	if (systemcfg->platform & PLATFORM_LPAR) {
		if (cur_cpu_spec->firmware_features & FW_FEATURE_MULTITCE) {
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

