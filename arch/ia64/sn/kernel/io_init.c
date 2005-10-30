/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/bootmem.h>
#include <linux/nodemask.h>
#include <asm/sn/types.h>
#include <asm/sn/addrs.h>
#include <asm/sn/geo.h>
#include <asm/sn/io.h>
#include <asm/sn/pcibr_provider.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/simulator.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/tioca_provider.h>
#include <asm/sn/tioce_provider.h>
#include "xtalk/hubdev.h"
#include "xtalk/xwidgetdev.h"

static struct list_head sn_sysdata_list;

/* sysdata list struct */
struct sysdata_el {
	struct list_head entry;
	void *sysdata;
};

struct slab_info {
	struct hubdev_info hubdev;
};

struct brick {
	moduleid_t id;		/* Module ID of this module        */
	struct slab_info slab_info[MAX_SLABS + 1];
};

int sn_ioif_inited = 0;		/* SN I/O infrastructure initialized? */

struct sn_pcibus_provider *sn_pci_provider[PCIIO_ASIC_MAX_TYPES];	/* indexed by asic type */

static int max_segment_number = 0; /* Default highest segment number */
static int max_pcibus_number = 255; /* Default highest pci bus number */

/*
 * Hooks and struct for unsupported pci providers
 */

static dma_addr_t
sn_default_pci_map(struct pci_dev *pdev, unsigned long paddr, size_t size)
{
	return 0;
}

static void
sn_default_pci_unmap(struct pci_dev *pdev, dma_addr_t addr, int direction)
{
	return;
}

static void *
sn_default_pci_bus_fixup(struct pcibus_bussoft *soft, struct pci_controller *controller)
{
	return NULL;
}

static struct sn_pcibus_provider sn_pci_default_provider = {
	.dma_map = sn_default_pci_map,
	.dma_map_consistent = sn_default_pci_map,
	.dma_unmap = sn_default_pci_unmap,
	.bus_fixup = sn_default_pci_bus_fixup,
};

/*
 * Retrieve the DMA Flush List given nasid.  This list is needed 
 * to implement the WAR - Flush DMA data on PIO Reads.
 */
static inline uint64_t
sal_get_widget_dmaflush_list(u64 nasid, u64 widget_num, u64 address)
{

	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff,
			(u64) SN_SAL_IOIF_GET_WIDGET_DMAFLUSH_LIST,
			(u64) nasid, (u64) widget_num, (u64) address, 0, 0, 0,
			0);
	return ret_stuff.v0;

}

/*
 * Retrieve the hub device info structure for the given nasid.
 */
static inline uint64_t sal_get_hubdev_info(u64 handle, u64 address)
{

	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff,
			(u64) SN_SAL_IOIF_GET_HUBDEV_INFO,
			(u64) handle, (u64) address, 0, 0, 0, 0, 0);
	return ret_stuff.v0;
}

/*
 * Retrieve the pci bus information given the bus number.
 */
static inline uint64_t sal_get_pcibus_info(u64 segment, u64 busnum, u64 address)
{

	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff,
			(u64) SN_SAL_IOIF_GET_PCIBUS_INFO,
			(u64) segment, (u64) busnum, (u64) address, 0, 0, 0, 0);
	return ret_stuff.v0;
}

/*
 * Retrieve the pci device information given the bus and device|function number.
 */
static inline uint64_t
sal_get_pcidev_info(u64 segment, u64 bus_number, u64 devfn, u64 pci_dev, 
			u64 sn_irq_info)
{
	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff,
			(u64) SN_SAL_IOIF_GET_PCIDEV_INFO,
			(u64) segment, (u64) bus_number, (u64) devfn, 
			(u64) pci_dev,
			sn_irq_info, 0, 0);
	return ret_stuff.v0;
}

/*
 * sn_fixup_ionodes() - This routine initializes the HUB data strcuture for 
 *	each node in the system.
 */
static void sn_fixup_ionodes(void)
{

	struct sn_flush_device_list *sn_flush_device_list;
	struct hubdev_info *hubdev;
	uint64_t status;
	uint64_t nasid;
	int i, widget;

	/*
	 * Get SGI Specific HUB chipset information.
	 * Inform Prom that this kernel can support domain bus numbering.
	 */
	for (i = 0; i < num_cnodes; i++) {
		hubdev = (struct hubdev_info *)(NODEPDA(i)->pdinfo);
		nasid = cnodeid_to_nasid(i);
		hubdev->max_segment_number = 0xffffffff;
		hubdev->max_pcibus_number = 0xff;
		status = sal_get_hubdev_info(nasid, (uint64_t) __pa(hubdev));
		if (status)
			continue;

		/* Save the largest Domain and pcibus numbers found. */
		if (hubdev->max_segment_number) {
			/*
			 * Dealing with a Prom that supports segments.
			 */
			max_segment_number = hubdev->max_segment_number;
			max_pcibus_number = hubdev->max_pcibus_number;
		}

		/* Attach the error interrupt handlers */
		if (nasid & 1)
			ice_error_init(hubdev);
		else
			hub_error_init(hubdev);

		for (widget = 0; widget <= HUB_WIDGET_ID_MAX; widget++)
			hubdev->hdi_xwidget_info[widget].xwi_hubinfo = hubdev;

		if (!hubdev->hdi_flush_nasid_list.widget_p)
			continue;

		hubdev->hdi_flush_nasid_list.widget_p =
		    kmalloc((HUB_WIDGET_ID_MAX + 1) *
			    sizeof(struct sn_flush_device_list *), GFP_KERNEL);

		memset(hubdev->hdi_flush_nasid_list.widget_p, 0x0,
		       (HUB_WIDGET_ID_MAX + 1) *
		       sizeof(struct sn_flush_device_list *));

		for (widget = 0; widget <= HUB_WIDGET_ID_MAX; widget++) {
			sn_flush_device_list = kmalloc(DEV_PER_WIDGET *
						       sizeof(struct
							      sn_flush_device_list),
						       GFP_KERNEL);
			memset(sn_flush_device_list, 0x0,
			       DEV_PER_WIDGET *
			       sizeof(struct sn_flush_device_list));

			status =
			    sal_get_widget_dmaflush_list(nasid, widget,
							 (uint64_t)
							 __pa
							 (sn_flush_device_list));
			if (status) {
				kfree(sn_flush_device_list);
				continue;
			}

			spin_lock_init(&sn_flush_device_list->sfdl_flush_lock);
			hubdev->hdi_flush_nasid_list.widget_p[widget] =
			    sn_flush_device_list;
		}

	}

}

void sn_pci_unfixup_slot(struct pci_dev *dev)
{
	struct pci_dev *host_pci_dev = SN_PCIDEV_INFO(dev)->host_pci_dev;

	sn_irq_unfixup(dev);
	pci_dev_put(host_pci_dev);
	pci_dev_put(dev);
}

/*
 * sn_pci_fixup_slot() - This routine sets up a slot's resources
 * consistent with the Linux PCI abstraction layer.  Resources acquired
 * from our PCI provider include PIO maps to BAR space and interrupt
 * objects.
 */
void sn_pci_fixup_slot(struct pci_dev *dev)
{
	int idx;
	int segment = pci_domain_nr(dev->bus);
	int status = 0;
	struct pcibus_bussoft *bs;
 	struct pci_bus *host_pci_bus;
 	struct pci_dev *host_pci_dev;
 	struct sn_irq_info *sn_irq_info;
 	unsigned long size;
 	unsigned int bus_no, devfn;

	pci_dev_get(dev); /* for the sysdata pointer */
	dev->sysdata = kmalloc(sizeof(struct pcidev_info), GFP_KERNEL);
	if (SN_PCIDEV_INFO(dev) <= 0)
		BUG();		/* Cannot afford to run out of memory */
	memset(SN_PCIDEV_INFO(dev), 0, sizeof(struct pcidev_info));

	sn_irq_info = kmalloc(sizeof(struct sn_irq_info), GFP_KERNEL);
	if (sn_irq_info <= 0)
		BUG();		/* Cannot afford to run out of memory */
	memset(sn_irq_info, 0, sizeof(struct sn_irq_info));

	/* Call to retrieve pci device information needed by kernel. */
	status = sal_get_pcidev_info((u64) segment, (u64) dev->bus->number, 
				     dev->devfn,
				     (u64) __pa(SN_PCIDEV_INFO(dev)),
				     (u64) __pa(sn_irq_info));
	if (status)
		BUG(); /* Cannot get platform pci device information */

	/* Copy over PIO Mapped Addresses */
	for (idx = 0; idx <= PCI_ROM_RESOURCE; idx++) {
		unsigned long start, end, addr;

		if (!SN_PCIDEV_INFO(dev)->pdi_pio_mapped_addr[idx])
			continue;

		start = dev->resource[idx].start;
		end = dev->resource[idx].end;
		size = end - start;
		addr = SN_PCIDEV_INFO(dev)->pdi_pio_mapped_addr[idx];
		addr = ((addr << 4) >> 4) | __IA64_UNCACHED_OFFSET;
		dev->resource[idx].start = addr;
		dev->resource[idx].end = addr + size;
		if (dev->resource[idx].flags & IORESOURCE_IO)
			dev->resource[idx].parent = &ioport_resource;
		else
			dev->resource[idx].parent = &iomem_resource;
	}

	/*
	 * Using the PROMs values for the PCI host bus, get the Linux
 	 * PCI host_pci_dev struct and set up host bus linkages
 	 */

 	bus_no = (SN_PCIDEV_INFO(dev)->pdi_slot_host_handle >> 32) & 0xff;
 	devfn = SN_PCIDEV_INFO(dev)->pdi_slot_host_handle & 0xffffffff;
 	host_pci_bus = pci_find_bus(segment, bus_no);
 	host_pci_dev = pci_get_slot(host_pci_bus, devfn);

	SN_PCIDEV_INFO(dev)->host_pci_dev = host_pci_dev;
	SN_PCIDEV_INFO(dev)->pdi_host_pcidev_info =
	    					SN_PCIDEV_INFO(host_pci_dev);
	SN_PCIDEV_INFO(dev)->pdi_linux_pcidev = dev;
	bs = SN_PCIBUS_BUSSOFT(dev->bus);
	SN_PCIDEV_INFO(dev)->pdi_pcibus_info = bs;

	if (bs && bs->bs_asic_type < PCIIO_ASIC_MAX_TYPES) {
		SN_PCIDEV_BUSPROVIDER(dev) = sn_pci_provider[bs->bs_asic_type];
	} else {
		SN_PCIDEV_BUSPROVIDER(dev) = &sn_pci_default_provider;
	}

	/* Only set up IRQ stuff if this device has a host bus context */
	if (bs && sn_irq_info->irq_irq) {
		SN_PCIDEV_INFO(dev)->pdi_sn_irq_info = sn_irq_info;
		dev->irq = SN_PCIDEV_INFO(dev)->pdi_sn_irq_info->irq_irq;
		sn_irq_fixup(dev, sn_irq_info);
	} else {
		SN_PCIDEV_INFO(dev)->pdi_sn_irq_info = NULL;
		kfree(sn_irq_info);
	}
}

/*
 * sn_pci_controller_fixup() - This routine sets up a bus's resources
 * consistent with the Linux PCI abstraction layer.
 */
void sn_pci_controller_fixup(int segment, int busnum, struct pci_bus *bus)
{
	int status = 0;
	int nasid, cnode;
	struct pci_controller *controller;
	struct pcibus_bussoft *prom_bussoft_ptr;
	struct hubdev_info *hubdev_info;
	void *provider_soft = NULL;
	struct sn_pcibus_provider *provider;

 	status = sal_get_pcibus_info((u64) segment, (u64) busnum,
 				     (u64) ia64_tpa(&prom_bussoft_ptr));
 	if (status > 0)
		return;		/*bus # does not exist */
	prom_bussoft_ptr = __va(prom_bussoft_ptr);

 	controller = kcalloc(1,sizeof(struct pci_controller), GFP_KERNEL);
	controller->segment = segment;
 	if (!controller)
 		BUG();

	if (bus == NULL) {
 		bus = pci_scan_bus(busnum, &pci_root_ops, controller);
 		if (bus == NULL)
 			goto error_return; /* error, or bus already scanned */
 		bus->sysdata = NULL;
	}

	if (bus->sysdata)
		goto error_return; /* sysdata already alloc'd */

	/*
	 * Per-provider fixup.  Copies the contents from prom to local
	 * area and links SN_PCIBUS_BUSSOFT().
	 */

	if (prom_bussoft_ptr->bs_asic_type >= PCIIO_ASIC_MAX_TYPES)
		goto error_return; /* unsupported asic type */

	if (prom_bussoft_ptr->bs_asic_type == PCIIO_ASIC_TYPE_PPB)
		goto error_return; /* no further fixup necessary */

	provider = sn_pci_provider[prom_bussoft_ptr->bs_asic_type];
	if (provider == NULL)
		goto error_return; /* no provider registerd for this asic */

	bus->sysdata = controller;
	if (provider->bus_fixup)
		provider_soft = (*provider->bus_fixup) (prom_bussoft_ptr, controller);

	if (provider_soft == NULL) {
		/* fixup failed or not applicable */
		bus->sysdata = NULL;
		goto error_return;
	}

	/*
	 * Generic bus fixup goes here.  Don't reference prom_bussoft_ptr
	 * after this point.
	 */

	PCI_CONTROLLER(bus)->platform_data = provider_soft;
	nasid = NASID_GET(SN_PCIBUS_BUSSOFT(bus)->bs_base);
	cnode = nasid_to_cnodeid(nasid);
	hubdev_info = (struct hubdev_info *)(NODEPDA(cnode)->pdinfo);
	SN_PCIBUS_BUSSOFT(bus)->bs_xwidget_info =
	    &(hubdev_info->hdi_xwidget_info[SN_PCIBUS_BUSSOFT(bus)->bs_xid]);

	/*
	 * If the node information we obtained during the fixup phase is invalid
	 * then set controller->node to -1 (undetermined)
	 */
	if (controller->node >= num_online_nodes()) {
		struct pcibus_bussoft *b = SN_PCIBUS_BUSSOFT(bus);

		printk(KERN_WARNING "Device ASIC=%u XID=%u PBUSNUM=%u"
				    "L_IO=%lx L_MEM=%lx BASE=%lx\n",
			b->bs_asic_type, b->bs_xid, b->bs_persist_busnum,
			b->bs_legacy_io, b->bs_legacy_mem, b->bs_base);
		printk(KERN_WARNING "on node %d but only %d nodes online."
			"Association set to undetermined.\n",
			controller->node, num_online_nodes());
		controller->node = -1;
	}
	return;

error_return:

	kfree(controller);
	return;
}

void sn_bus_store_sysdata(struct pci_dev *dev)
{
	struct sysdata_el *element;

	element = kzalloc(sizeof(struct sysdata_el), GFP_KERNEL);
	if (!element) {
		dev_dbg(dev, "%s: out of memory!\n", __FUNCTION__);
		return;
	}
	element->sysdata = dev->sysdata;
	list_add(&element->entry, &sn_sysdata_list);
}

void sn_bus_free_sysdata(void)
{
	struct sysdata_el *element;
	struct list_head *list;

sn_sysdata_free_start:
	list_for_each(list, &sn_sysdata_list) {
		element = list_entry(list, struct sysdata_el, entry);
		list_del(&element->entry);
		kfree(element->sysdata);
		kfree(element);
		goto sn_sysdata_free_start;
	}
	return;
}

/*
 * Ugly hack to get PCI setup until we have a proper ACPI namespace.
 */

#define PCI_BUSES_TO_SCAN 256

static int __init sn_pci_init(void)
{
	int i = 0;
	int j = 0;
	struct pci_dev *pci_dev = NULL;
	extern void sn_init_cpei_timer(void);
#ifdef CONFIG_PROC_FS
	extern void register_sn_procfs(void);
#endif

	if (!ia64_platform_is("sn2") || IS_RUNNING_ON_FAKE_PROM())
		return 0;

	/*
	 * prime sn_pci_provider[].  Individial provider init routines will
	 * override their respective default entries.
	 */

	for (i = 0; i < PCIIO_ASIC_MAX_TYPES; i++)
		sn_pci_provider[i] = &sn_pci_default_provider;

	pcibr_init_provider();
	tioca_init_provider();
	tioce_init_provider();

	/*
	 * This is needed to avoid bounce limit checks in the blk layer
	 */
	ia64_max_iommu_merge_mask = ~PAGE_MASK;
	sn_fixup_ionodes();
	sn_irq_lh_init();
	INIT_LIST_HEAD(&sn_sysdata_list);
	sn_init_cpei_timer();

#ifdef CONFIG_PROC_FS
	register_sn_procfs();
#endif

	/* busses are not known yet ... */
	for (i = 0; i <= max_segment_number; i++)
		for (j = 0; j <= max_pcibus_number; j++)
			sn_pci_controller_fixup(i, j, NULL);

	/*
	 * Generic Linux PCI Layer has created the pci_bus and pci_dev 
	 * structures - time for us to add our SN PLatform specific 
	 * information.
	 */

	while ((pci_dev =
		pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pci_dev)) != NULL)
		sn_pci_fixup_slot(pci_dev);

	sn_ioif_inited = 1;	/* sn I/O infrastructure now initialized */

	return 0;
}

/*
 * hubdev_init_node() - Creates the HUB data structure and link them to it's 
 *	own NODE specific data area.
 */
void hubdev_init_node(nodepda_t * npda, cnodeid_t node)
{

	struct hubdev_info *hubdev_info;

	if (node >= num_online_nodes())	/* Headless/memless IO nodes */
		hubdev_info =
		    (struct hubdev_info *)alloc_bootmem_node(NODE_DATA(0),
							     sizeof(struct
								    hubdev_info));
	else
		hubdev_info =
		    (struct hubdev_info *)alloc_bootmem_node(NODE_DATA(node),
							     sizeof(struct
								    hubdev_info));
	npda->pdinfo = (void *)hubdev_info;

}

geoid_t
cnodeid_get_geoid(cnodeid_t cnode)
{

	struct hubdev_info *hubdev;

	hubdev = (struct hubdev_info *)(NODEPDA(cnode)->pdinfo);
	return hubdev->hdi_geoid;

}

subsys_initcall(sn_pci_init);
EXPORT_SYMBOL(sn_pci_fixup_slot);
EXPORT_SYMBOL(sn_pci_unfixup_slot);
EXPORT_SYMBOL(sn_pci_controller_fixup);
EXPORT_SYMBOL(sn_bus_store_sysdata);
EXPORT_SYMBOL(sn_bus_free_sysdata);
