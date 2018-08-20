/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/bootmem.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <asm/sn/types.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn_feature_sets.h>
#include <asm/sn/geo.h>
#include <asm/sn/io.h>
#include <asm/sn/l1.h>
#include <asm/sn/module.h>
#include <asm/sn/pcibr_provider.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/simulator.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/tioca_provider.h>
#include <asm/sn/tioce_provider.h>
#include "xtalk/hubdev.h"
#include "xtalk/xwidgetdev.h"
#include <linux/acpi.h>
#include <asm/sn/sn2/sn_hwperf.h>
#include <asm/sn/acpi.h>

extern void sn_init_cpei_timer(void);
extern void register_sn_procfs(void);
extern void sn_io_acpi_init(void);
extern void sn_io_init(void);


static struct list_head sn_sysdata_list;

/* sysdata list struct */
struct sysdata_el {
	struct list_head entry;
	void *sysdata;
};

int sn_ioif_inited;		/* SN I/O infrastructure initialized? */

int sn_acpi_rev;		/* SN ACPI revision */
EXPORT_SYMBOL_GPL(sn_acpi_rev);

struct sn_pcibus_provider *sn_pci_provider[PCIIO_ASIC_MAX_TYPES];	/* indexed by asic type */

/*
 * Hooks and struct for unsupported pci providers
 */

static dma_addr_t
sn_default_pci_map(struct pci_dev *pdev, unsigned long paddr, size_t size, int type)
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
 * Retrieve the DMA Flush List given nasid, widget, and device.
 * This list is needed to implement the WAR - Flush DMA data on PIO Reads.
 */
static inline u64
sal_get_device_dmaflush_list(u64 nasid, u64 widget_num, u64 device_num,
			     u64 address)
{
	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff,
			(u64) SN_SAL_IOIF_GET_DEVICE_DMAFLUSH_LIST,
			(u64) nasid, (u64) widget_num,
			(u64) device_num, (u64) address, 0, 0, 0);
	return ret_stuff.status;
}

/*
 * sn_pcidev_info_get() - Retrieve the pcidev_info struct for the specified
 *			  device.
 */
inline struct pcidev_info *
sn_pcidev_info_get(struct pci_dev *dev)
{
	struct pcidev_info *pcidev;

	list_for_each_entry(pcidev,
			    &(SN_PLATFORM_DATA(dev)->pcidev_info), pdi_list) {
		if (pcidev->pdi_linux_pcidev == dev)
			return pcidev;
	}
	return NULL;
}

/* Older PROM flush WAR
 *
 * 01/16/06 -- This war will be in place until a new official PROM is released.
 * Additionally note that the struct sn_flush_device_war also has to be
 * removed from arch/ia64/sn/include/xtalk/hubdev.h
 */

static s64 sn_device_fixup_war(u64 nasid, u64 widget, int device,
			       struct sn_flush_device_common *common)
{
	struct sn_flush_device_war *war_list;
	struct sn_flush_device_war *dev_entry;
	struct ia64_sal_retval isrv = {0,0,0,0};

	printk_once(KERN_WARNING
		"PROM version < 4.50 -- implementing old PROM flush WAR\n");

	war_list = kcalloc(DEV_PER_WIDGET, sizeof(*war_list), GFP_KERNEL);
	BUG_ON(!war_list);

	SAL_CALL_NOLOCK(isrv, SN_SAL_IOIF_GET_WIDGET_DMAFLUSH_LIST,
			nasid, widget, __pa(war_list), 0, 0, 0 ,0);
	if (isrv.status)
		panic("sn_device_fixup_war failed: %s\n",
		      ia64_sal_strerror(isrv.status));

	dev_entry = war_list + device;
	memcpy(common,dev_entry, sizeof(*common));
	kfree(war_list);

	return isrv.status;
}

/*
 * sn_common_hubdev_init() - This routine is called to initialize the HUB data
 *			     structure for each node in the system.
 */
void __init
sn_common_hubdev_init(struct hubdev_info *hubdev)
{

	struct sn_flush_device_kernel *sn_flush_device_kernel;
	struct sn_flush_device_kernel *dev_entry;
	s64 status;
	int widget, device, size;

	/* Attach the error interrupt handlers */
	if (hubdev->hdi_nasid & 1)	/* If TIO */
		ice_error_init(hubdev);
	else
		hub_error_init(hubdev);

	for (widget = 0; widget <= HUB_WIDGET_ID_MAX; widget++)
		hubdev->hdi_xwidget_info[widget].xwi_hubinfo = hubdev;

	if (!hubdev->hdi_flush_nasid_list.widget_p)
		return;

	size = (HUB_WIDGET_ID_MAX + 1) *
		sizeof(struct sn_flush_device_kernel *);
	hubdev->hdi_flush_nasid_list.widget_p =
		kzalloc(size, GFP_KERNEL);
	BUG_ON(!hubdev->hdi_flush_nasid_list.widget_p);

	for (widget = 0; widget <= HUB_WIDGET_ID_MAX; widget++) {
		size = DEV_PER_WIDGET *
			sizeof(struct sn_flush_device_kernel);
		sn_flush_device_kernel = kzalloc(size, GFP_KERNEL);
		BUG_ON(!sn_flush_device_kernel);

		dev_entry = sn_flush_device_kernel;
		for (device = 0; device < DEV_PER_WIDGET;
		     device++, dev_entry++) {
			size = sizeof(struct sn_flush_device_common);
			dev_entry->common = kzalloc(size, GFP_KERNEL);
			BUG_ON(!dev_entry->common);
			if (sn_prom_feature_available(PRF_DEVICE_FLUSH_LIST))
				status = sal_get_device_dmaflush_list(
					     hubdev->hdi_nasid, widget, device,
					     (u64)(dev_entry->common));
			else
				status = sn_device_fixup_war(hubdev->hdi_nasid,
							     widget, device,
							     dev_entry->common);
			if (status != SALRET_OK)
				panic("SAL call failed: %s\n",
				      ia64_sal_strerror(status));

			spin_lock_init(&dev_entry->sfdl_flush_lock);
		}

		if (sn_flush_device_kernel)
			hubdev->hdi_flush_nasid_list.widget_p[widget] =
							 sn_flush_device_kernel;
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
 * sn_pci_fixup_slot()
 */
void sn_pci_fixup_slot(struct pci_dev *dev, struct pcidev_info *pcidev_info,
		       struct sn_irq_info *sn_irq_info)
{
	int segment = pci_domain_nr(dev->bus);
	struct pcibus_bussoft *bs;
	struct pci_dev *host_pci_dev;
	unsigned int bus_no, devfn;

	pci_dev_get(dev); /* for the sysdata pointer */

	/* Add pcidev_info to list in pci_controller.platform_data */
	list_add_tail(&pcidev_info->pdi_list,
		      &(SN_PLATFORM_DATA(dev->bus)->pcidev_info));
	/*
	 * Using the PROMs values for the PCI host bus, get the Linux
	 * PCI host_pci_dev struct and set up host bus linkages
 	 */

	bus_no = (pcidev_info->pdi_slot_host_handle >> 32) & 0xff;
	devfn = pcidev_info->pdi_slot_host_handle & 0xffffffff;
	host_pci_dev = pci_get_domain_bus_and_slot(segment, bus_no, devfn);

	pcidev_info->host_pci_dev = host_pci_dev;
	pcidev_info->pdi_linux_pcidev = dev;
	pcidev_info->pdi_host_pcidev_info = SN_PCIDEV_INFO(host_pci_dev);
	bs = SN_PCIBUS_BUSSOFT(dev->bus);
	pcidev_info->pdi_pcibus_info = bs;

	if (bs && bs->bs_asic_type < PCIIO_ASIC_MAX_TYPES) {
		SN_PCIDEV_BUSPROVIDER(dev) = sn_pci_provider[bs->bs_asic_type];
	} else {
		SN_PCIDEV_BUSPROVIDER(dev) = &sn_pci_default_provider;
	}

	/* Only set up IRQ stuff if this device has a host bus context */
	if (bs && sn_irq_info->irq_irq) {
		pcidev_info->pdi_sn_irq_info = sn_irq_info;
		dev->irq = pcidev_info->pdi_sn_irq_info->irq_irq;
		sn_irq_fixup(dev, sn_irq_info);
	} else {
		pcidev_info->pdi_sn_irq_info = NULL;
		kfree(sn_irq_info);
	}
}

/*
 * sn_common_bus_fixup - Perform platform specific bus fixup.
 *			 Execute the ASIC specific fixup routine
 *			 for this bus.
 */
void
sn_common_bus_fixup(struct pci_bus *bus,
		    struct pcibus_bussoft *prom_bussoft_ptr)
{
	int cnode;
	struct pci_controller *controller;
	struct hubdev_info *hubdev_info;
	int nasid;
	void *provider_soft;
	struct sn_pcibus_provider *provider;
	struct sn_platform_data *sn_platform_data;

	controller = PCI_CONTROLLER(bus);
	/*
	 * Per-provider fixup.  Copies the bus soft structure from prom
	 * to local area and links SN_PCIBUS_BUSSOFT().
	 */

	if (prom_bussoft_ptr->bs_asic_type >= PCIIO_ASIC_MAX_TYPES) {
		printk(KERN_WARNING "sn_common_bus_fixup: Unsupported asic type, %d",
		       prom_bussoft_ptr->bs_asic_type);
		return;
	}

	if (prom_bussoft_ptr->bs_asic_type == PCIIO_ASIC_TYPE_PPB)
		return;	/* no further fixup necessary */

	provider = sn_pci_provider[prom_bussoft_ptr->bs_asic_type];
	if (provider == NULL)
		panic("sn_common_bus_fixup: No provider registered for this asic type, %d",
		      prom_bussoft_ptr->bs_asic_type);

	if (provider->bus_fixup)
		provider_soft = (*provider->bus_fixup) (prom_bussoft_ptr,
				 controller);
	else
		provider_soft = NULL;

	/*
	 * Generic bus fixup goes here.  Don't reference prom_bussoft_ptr
	 * after this point.
	 */
	controller->platform_data = kzalloc(sizeof(struct sn_platform_data),
					    GFP_KERNEL);
	BUG_ON(controller->platform_data == NULL);
	sn_platform_data =
			(struct sn_platform_data *) controller->platform_data;
	sn_platform_data->provider_soft = provider_soft;
	INIT_LIST_HEAD(&((struct sn_platform_data *)
			 controller->platform_data)->pcidev_info);
	nasid = NASID_GET(SN_PCIBUS_BUSSOFT(bus)->bs_base);
	cnode = nasid_to_cnodeid(nasid);
	hubdev_info = (struct hubdev_info *)(NODEPDA(cnode)->pdinfo);
	SN_PCIBUS_BUSSOFT(bus)->bs_xwidget_info =
	    &(hubdev_info->hdi_xwidget_info[SN_PCIBUS_BUSSOFT(bus)->bs_xid]);

	/*
	 * If the node information we obtained during the fixup phase is
	 * invalid then set controller->node to -1 (undetermined)
	 */
	if (controller->node >= num_online_nodes()) {
		struct pcibus_bussoft *b = SN_PCIBUS_BUSSOFT(bus);

		printk(KERN_WARNING "Device ASIC=%u XID=%u PBUSNUM=%u "
		       "L_IO=%llx L_MEM=%llx BASE=%llx\n",
		       b->bs_asic_type, b->bs_xid, b->bs_persist_busnum,
		       b->bs_legacy_io, b->bs_legacy_mem, b->bs_base);
		printk(KERN_WARNING "on node %d but only %d nodes online."
		       "Association set to undetermined.\n",
		       controller->node, num_online_nodes());
		controller->node = -1;
	}
}

void sn_bus_store_sysdata(struct pci_dev *dev)
{
	struct sysdata_el *element;

	element = kzalloc(sizeof(struct sysdata_el), GFP_KERNEL);
	if (!element) {
		dev_dbg(&dev->dev, "%s: out of memory!\n", __func__);
		return;
	}
	element->sysdata = SN_PCIDEV_INFO(dev);
	list_add(&element->entry, &sn_sysdata_list);
}

void sn_bus_free_sysdata(void)
{
	struct sysdata_el *element;
	struct list_head *list, *safe;

	list_for_each_safe(list, safe, &sn_sysdata_list) {
		element = list_entry(list, struct sysdata_el, entry);
		list_del(&element->entry);
		list_del(&(((struct pcidev_info *)
			     (element->sysdata))->pdi_list));
		kfree(element->sysdata);
		kfree(element);
	}
	return;
}

/*
 * hubdev_init_node() - Creates the HUB data structure and link them to it's
 *			own NODE specific data area.
 */
void __init hubdev_init_node(nodepda_t * npda, cnodeid_t node)
{
	struct hubdev_info *hubdev_info;
	int size;
	pg_data_t *pg;

	size = sizeof(struct hubdev_info);

	if (node >= num_online_nodes())	/* Headless/memless IO nodes */
		pg = NODE_DATA(0);
	else
		pg = NODE_DATA(node);

	hubdev_info = (struct hubdev_info *)alloc_bootmem_node(pg, size);

	npda->pdinfo = (void *)hubdev_info;
}

geoid_t
cnodeid_get_geoid(cnodeid_t cnode)
{
	struct hubdev_info *hubdev;

	hubdev = (struct hubdev_info *)(NODEPDA(cnode)->pdinfo);
	return hubdev->hdi_geoid;
}

void sn_generate_path(struct pci_bus *pci_bus, char *address)
{
	nasid_t nasid;
	cnodeid_t cnode;
	geoid_t geoid;
	moduleid_t moduleid;
	u16 bricktype;

	nasid = NASID_GET(SN_PCIBUS_BUSSOFT(pci_bus)->bs_base);
	cnode = nasid_to_cnodeid(nasid);
	geoid = cnodeid_get_geoid(cnode);
	moduleid = geo_module(geoid);

	sprintf(address, "module_%c%c%c%c%.2d",
		'0'+RACK_GET_CLASS(MODULE_GET_RACK(moduleid)),
		'0'+RACK_GET_GROUP(MODULE_GET_RACK(moduleid)),
		'0'+RACK_GET_NUM(MODULE_GET_RACK(moduleid)),
		MODULE_GET_BTCHAR(moduleid), MODULE_GET_BPOS(moduleid));

	/* Tollhouse requires slot id to be displayed */
	bricktype = MODULE_GET_BTYPE(moduleid);
	if ((bricktype == L1_BRICKTYPE_191010) ||
	    (bricktype == L1_BRICKTYPE_1932))
			sprintf(address + strlen(address), "^%d",
						geo_slot(geoid));
}

void sn_pci_fixup_bus(struct pci_bus *bus)
{

	if (SN_ACPI_BASE_SUPPORT())
		sn_acpi_bus_fixup(bus);
	else
		sn_bus_fixup(bus);
}

/*
 * sn_io_early_init - Perform early IO (and some non-IO) initialization.
 *		      In particular, setup the sn_pci_provider[] array.
 *		      This needs to be done prior to any bus scanning
 *		      (acpi_scan_init()) in the ACPI case, as the SN
 *		      bus fixup code will reference the array.
 */
static int __init
sn_io_early_init(void)
{
	int i;

	if (!ia64_platform_is("sn2") || IS_RUNNING_ON_FAKE_PROM())
		return 0;

	/* we set the acpi revision to that of the DSDT table OEM rev. */
	{
		struct acpi_table_header *header = NULL;

		acpi_get_table(ACPI_SIG_DSDT, 1, &header);
		BUG_ON(header == NULL);
		sn_acpi_rev = header->oem_revision;
	}

	/*
	 * prime sn_pci_provider[].  Individual provider init routines will
	 * override their respective default entries.
	 */

	for (i = 0; i < PCIIO_ASIC_MAX_TYPES; i++)
		sn_pci_provider[i] = &sn_pci_default_provider;

	pcibr_init_provider();
	tioca_init_provider();
	tioce_init_provider();

	sn_irq_lh_init();
	INIT_LIST_HEAD(&sn_sysdata_list);
	sn_init_cpei_timer();

#ifdef CONFIG_PROC_FS
	register_sn_procfs();
#endif

	{
		struct acpi_table_header *header;
		(void)acpi_get_table(ACPI_SIG_DSDT, 1, &header);
		printk(KERN_INFO "ACPI  DSDT OEM Rev 0x%x\n",
			header->oem_revision);
	}
	if (SN_ACPI_BASE_SUPPORT())
		sn_io_acpi_init();
	else
		sn_io_init();
	return 0;
}

arch_initcall(sn_io_early_init);

/*
 * sn_io_late_init() - Perform any final platform specific IO initialization.
 */

int __init
sn_io_late_init(void)
{
	struct pci_bus *bus;
	struct pcibus_bussoft *bussoft;
	cnodeid_t cnode;
	nasid_t nasid;
	cnodeid_t near_cnode;

	if (!ia64_platform_is("sn2") || IS_RUNNING_ON_FAKE_PROM())
		return 0;

	/*
	 * Setup closest node in pci_controller->node for
	 * PIC, TIOCP, TIOCE (TIOCA does it during bus fixup using
	 * info from the PROM).
	 */
	bus = NULL;
	while ((bus = pci_find_next_bus(bus)) != NULL) {
		bussoft = SN_PCIBUS_BUSSOFT(bus);
		nasid = NASID_GET(bussoft->bs_base);
		cnode = nasid_to_cnodeid(nasid);
		if ((bussoft->bs_asic_type == PCIIO_ASIC_TYPE_TIOCP) ||
		    (bussoft->bs_asic_type == PCIIO_ASIC_TYPE_TIOCE) ||
		    (bussoft->bs_asic_type == PCIIO_ASIC_TYPE_PIC)) {
			/* PCI Bridge: find nearest node with CPUs */
			int e = sn_hwperf_get_nearest_node(cnode, NULL,
							   &near_cnode);
			if (e < 0) {
				near_cnode = (cnodeid_t)-1; /* use any node */
				printk(KERN_WARNING "sn_io_late_init: failed "
				       "to find near node with CPUs for "
				       "node %d, err=%d\n", cnode, e);
			}
			PCI_CONTROLLER(bus)->node = near_cnode;
		}
	}

	sn_ioif_inited = 1;	/* SN I/O infrastructure now initialized */

	return 0;
}

fs_initcall(sn_io_late_init);

EXPORT_SYMBOL(sn_pci_unfixup_slot);
EXPORT_SYMBOL(sn_bus_store_sysdata);
EXPORT_SYMBOL(sn_bus_free_sysdata);
EXPORT_SYMBOL(sn_generate_path);

