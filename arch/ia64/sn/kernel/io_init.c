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
#include <asm/sn/sn_sal.h>
#include <asm/sn/addrs.h>
#include "pci/pcibus_provider_defs.h"
#include "pci/pcidev.h"
#include "pci/pcibr_provider.h"
#include "xtalk/xwidgetdev.h"
#include <asm/sn/geo.h>
#include "xtalk/hubdev.h"
#include <asm/sn/io.h>
#include <asm/sn/simulator.h>

char master_baseio_wid;
nasid_t master_nasid = INVALID_NASID;	/* Partition Master */

struct slab_info {
	struct hubdev_info hubdev;
};

struct brick {
	moduleid_t id;		/* Module ID of this module        */
	struct slab_info slab_info[MAX_SLABS + 1];
};

int sn_ioif_inited = 0;		/* SN I/O infrastructure initialized? */

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
 * sn_alloc_pci_sysdata() - This routine allocates a pci controller
 *	which is expected as the pci_dev and pci_bus sysdata by the Linux
 *	PCI infrastructure.
 */
static inline struct pci_controller *sn_alloc_pci_sysdata(void)
{
	struct pci_controller *pci_sysdata;

	pci_sysdata = kmalloc(sizeof(*pci_sysdata), GFP_KERNEL);
	if (!pci_sysdata)
		BUG();

	memset(pci_sysdata, 0, sizeof(*pci_sysdata));
	return pci_sysdata;
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

	for (i = 0; i < numionodes; i++) {
		hubdev = (struct hubdev_info *)(NODEPDA(i)->pdinfo);
		nasid = cnodeid_to_nasid(i);
		status = sal_get_hubdev_info(nasid, (uint64_t) __pa(hubdev));
		if (status)
			continue;

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

			hubdev->hdi_flush_nasid_list.widget_p[widget] =
			    sn_flush_device_list;
		}

		if (!(i & 1))
			hub_error_init(hubdev);
		else
			ice_error_init(hubdev);
	}

}

/*
 * sn_pci_fixup_slot() - This routine sets up a slot's resources
 * consistent with the Linux PCI abstraction layer.  Resources acquired
 * from our PCI provider include PIO maps to BAR space and interrupt
 * objects.
 */
static void sn_pci_fixup_slot(struct pci_dev *dev)
{
	int idx;
	int segment = 0;
	uint64_t size;
	struct sn_irq_info *sn_irq_info;
	struct pci_dev *host_pci_dev;
	int status = 0;

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
		BUG();		/* Cannot get platform pci device information information */

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

	/* set up host bus linkages */
	host_pci_dev =
	    pci_find_slot(SN_PCIDEV_INFO(dev)->pdi_slot_host_handle >> 32,
			  SN_PCIDEV_INFO(dev)->
			  pdi_slot_host_handle & 0xffffffff);
	SN_PCIDEV_INFO(dev)->pdi_host_pcidev_info =
	    SN_PCIDEV_INFO(host_pci_dev);
	SN_PCIDEV_INFO(dev)->pdi_linux_pcidev = dev;
	SN_PCIDEV_INFO(dev)->pdi_pcibus_info = SN_PCIBUS_BUSSOFT(dev->bus);

	/* Only set up IRQ stuff if this device has a host bus context */
	if (SN_PCIDEV_BUSSOFT(dev) && sn_irq_info->irq_irq) {
		SN_PCIDEV_INFO(dev)->pdi_sn_irq_info = sn_irq_info;
		dev->irq = SN_PCIDEV_INFO(dev)->pdi_sn_irq_info->irq_irq;
		sn_irq_fixup(dev, sn_irq_info);
	}
}

/*
 * sn_pci_controller_fixup() - This routine sets up a bus's resources
 * consistent with the Linux PCI abstraction layer.
 */
static void sn_pci_controller_fixup(int segment, int busnum)
{
	int status = 0;
	int nasid, cnode;
	struct pci_bus *bus;
	struct pci_controller *controller;
	struct pcibus_bussoft *prom_bussoft_ptr;
	struct hubdev_info *hubdev_info;
	void *provider_soft;

	status =
	    sal_get_pcibus_info((u64) segment, (u64) busnum,
				(u64) ia64_tpa(&prom_bussoft_ptr));
	if (status > 0) {
		return;		/* bus # does not exist */
	}

	prom_bussoft_ptr = __va(prom_bussoft_ptr);
	controller = sn_alloc_pci_sysdata();
	/* controller non-zero is BUG'd in sn_alloc_pci_sysdata */

	bus = pci_scan_bus(busnum, &pci_root_ops, controller);
	if (bus == NULL) {
		return;		/* error, or bus already scanned */
	}

	/*
	 * Per-provider fixup.  Copies the contents from prom to local
	 * area and links SN_PCIBUS_BUSSOFT().
	 *
	 * Note:  Provider is responsible for ensuring that prom_bussoft_ptr
	 * represents an asic-type that it can handle.
	 */

	if (prom_bussoft_ptr->bs_asic_type == PCIIO_ASIC_TYPE_PPB) {
		return;		/* no further fixup necessary */
	}

	provider_soft = pcibr_bus_fixup(prom_bussoft_ptr);
	if (provider_soft == NULL) {
		return;		/* fixup failed or not applicable */
	}

	/*
	 * Generic bus fixup goes here.  Don't reference prom_bussoft_ptr
	 * after this point.
	 */

	bus->sysdata = controller;
	PCI_CONTROLLER(bus)->platform_data = provider_soft;

	nasid = NASID_GET(SN_PCIBUS_BUSSOFT(bus)->bs_base);
	cnode = nasid_to_cnodeid(nasid);
	hubdev_info = (struct hubdev_info *)(NODEPDA(cnode)->pdinfo);
	SN_PCIBUS_BUSSOFT(bus)->bs_xwidget_info =
	    &(hubdev_info->hdi_xwidget_info[SN_PCIBUS_BUSSOFT(bus)->bs_xid]);
}

/*
 * Ugly hack to get PCI setup until we have a proper ACPI namespace.
 */

#define PCI_BUSES_TO_SCAN 256

static int __init sn_pci_init(void)
{
	int i = 0;
	struct pci_dev *pci_dev = NULL;
	extern void sn_init_cpei_timer(void);
#ifdef CONFIG_PROC_FS
	extern void register_sn_procfs(void);
#endif

	if (!ia64_platform_is("sn2") || IS_RUNNING_ON_SIMULATOR())
		return 0;

	/*
	 * This is needed to avoid bounce limit checks in the blk layer
	 */
	ia64_max_iommu_merge_mask = ~PAGE_MASK;
	sn_fixup_ionodes();
	sn_irq = kmalloc(sizeof(struct sn_irq_info *) * NR_IRQS, GFP_KERNEL);
	if (sn_irq <= 0)
		BUG();		/* Canno afford to run out of memory. */
	memset(sn_irq, 0, sizeof(struct sn_irq_info *) * NR_IRQS);

	sn_init_cpei_timer();

#ifdef CONFIG_PROC_FS
	register_sn_procfs();
#endif

	for (i = 0; i < PCI_BUSES_TO_SCAN; i++) {
		sn_pci_controller_fixup(0, i);
	}

	/*
	 * Generic Linux PCI Layer has created the pci_bus and pci_dev 
	 * structures - time for us to add our SN PLatform specific 
	 * information.
	 */

	while ((pci_dev =
		pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pci_dev)) != NULL) {
		sn_pci_fixup_slot(pci_dev);
	}

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
