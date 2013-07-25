/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2006 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <asm/sn/types.h>
#include <asm/sn/addrs.h>
#include <asm/sn/io.h>
#include <asm/sn/module.h>
#include <asm/sn/intr.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/sn_sal.h>
#include "xtalk/hubdev.h"

/*
 * The code in this file will only be executed when running with
 * a PROM that does _not_ have base ACPI IO support.
 * (i.e., SN_ACPI_BASE_SUPPORT() == 0)
 */

static int max_segment_number;		 /* Default highest segment number */
static int max_pcibus_number = 255;	/* Default highest pci bus number */


/*
 * Retrieve the hub device info structure for the given nasid.
 */
static inline u64 sal_get_hubdev_info(u64 handle, u64 address)
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
static inline u64 sal_get_pcibus_info(u64 segment, u64 busnum, u64 address)
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
static inline u64
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
 * sn_fixup_ionodes() - This routine initializes the HUB data structure for
 *			each node in the system. This function is only
 *			executed when running with a non-ACPI capable PROM.
 */
static void __init sn_fixup_ionodes(void)
{

	struct hubdev_info *hubdev;
	u64 status;
	u64 nasid;
	int i;
	extern void sn_common_hubdev_init(struct hubdev_info *);

	/*
	 * Get SGI Specific HUB chipset information.
	 * Inform Prom that this kernel can support domain bus numbering.
	 */
	for (i = 0; i < num_cnodes; i++) {
		hubdev = (struct hubdev_info *)(NODEPDA(i)->pdinfo);
		nasid = cnodeid_to_nasid(i);
		hubdev->max_segment_number = 0xffffffff;
		hubdev->max_pcibus_number = 0xff;
		status = sal_get_hubdev_info(nasid, (u64) __pa(hubdev));
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
		sn_common_hubdev_init(hubdev);
	}
}

/*
 * sn_pci_legacy_window_fixup - Setup PCI resources for
 *				legacy IO and MEM space. This needs to
 *				be done here, as the PROM does not have
 *				ACPI support defining the root buses
 *				and their resources (_CRS),
 */
static void
sn_legacy_pci_window_fixup(struct resource *res,
		u64 legacy_io, u64 legacy_mem)
{
		res[0].name = "legacy_io";
		res[0].flags = IORESOURCE_IO;
		res[0].start = legacy_io;
		res[0].end = res[0].start + 0xffff;
		res[0].parent = &ioport_resource;
		res[1].name = "legacy_mem";
		res[1].flags = IORESOURCE_MEM;
		res[1].start = legacy_mem;
		res[1].end = res[1].start + (1024 * 1024) - 1;
		res[1].parent = &iomem_resource;
}

/*
 * sn_io_slot_fixup() -   We are not running with an ACPI capable PROM,
 *			  and need to convert the pci_dev->resource
 *			  'start' and 'end' addresses to mapped addresses,
 *			  and setup the pci_controller->window array entries.
 */
void
sn_io_slot_fixup(struct pci_dev *dev)
{
	int idx;
	unsigned long addr, end, size, start;
	struct pcidev_info *pcidev_info;
	struct sn_irq_info *sn_irq_info;
	int status;

	pcidev_info = kzalloc(sizeof(struct pcidev_info), GFP_KERNEL);
	if (!pcidev_info)
		panic("%s: Unable to alloc memory for pcidev_info", __func__);

	sn_irq_info = kzalloc(sizeof(struct sn_irq_info), GFP_KERNEL);
	if (!sn_irq_info)
		panic("%s: Unable to alloc memory for sn_irq_info", __func__);

	/* Call to retrieve pci device information needed by kernel. */
	status = sal_get_pcidev_info((u64) pci_domain_nr(dev),
		(u64) dev->bus->number,
		dev->devfn,
		(u64) __pa(pcidev_info),
		(u64) __pa(sn_irq_info));

	BUG_ON(status); /* Cannot get platform pci device information */


	/* Copy over PIO Mapped Addresses */
	for (idx = 0; idx <= PCI_ROM_RESOURCE; idx++) {

		if (!pcidev_info->pdi_pio_mapped_addr[idx]) {
			continue;
		}

		start = dev->resource[idx].start;
		end = dev->resource[idx].end;
		size = end - start;
		if (size == 0) {
			continue;
		}
		addr = pcidev_info->pdi_pio_mapped_addr[idx];
		addr = ((addr << 4) >> 4) | __IA64_UNCACHED_OFFSET;
		dev->resource[idx].start = addr;
		dev->resource[idx].end = addr + size;

		/*
		 * if it's already in the device structure, remove it before
		 * inserting
		 */
		if (dev->resource[idx].parent && dev->resource[idx].parent->child)
			release_resource(&dev->resource[idx]);

		if (dev->resource[idx].flags & IORESOURCE_IO)
			insert_resource(&ioport_resource, &dev->resource[idx]);
		else
			insert_resource(&iomem_resource, &dev->resource[idx]);
		/*
		 * If ROM, set the actual ROM image size, and mark as
		 * shadowed in PROM.
		 */
		if (idx == PCI_ROM_RESOURCE) {
			size_t image_size;
			void __iomem *rom;

			rom = ioremap(pci_resource_start(dev, PCI_ROM_RESOURCE),
				      size + 1);
			image_size = pci_get_rom_size(dev, rom, size + 1);
			dev->resource[PCI_ROM_RESOURCE].end =
				dev->resource[PCI_ROM_RESOURCE].start +
				image_size - 1;
			dev->resource[PCI_ROM_RESOURCE].flags |=
						 IORESOURCE_ROM_BIOS_COPY;
		}
	}

	sn_pci_fixup_slot(dev, pcidev_info, sn_irq_info);
}

EXPORT_SYMBOL(sn_io_slot_fixup);

/*
 * sn_pci_controller_fixup() - This routine sets up a bus's resources
 *			       consistent with the Linux PCI abstraction layer.
 */
static void __init
sn_pci_controller_fixup(int segment, int busnum, struct pci_bus *bus)
{
	s64 status = 0;
	struct pci_controller *controller;
	struct pcibus_bussoft *prom_bussoft_ptr;
	struct resource *res;
	LIST_HEAD(resources);

 	status = sal_get_pcibus_info((u64) segment, (u64) busnum,
 				     (u64) ia64_tpa(&prom_bussoft_ptr));
 	if (status > 0)
		return;		/*bus # does not exist */
	prom_bussoft_ptr = __va(prom_bussoft_ptr);

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	BUG_ON(!controller);
	controller->segment = segment;

	res = kcalloc(2, sizeof(struct resource), GFP_KERNEL);
	BUG_ON(!res);

	/*
	 * Temporarily save the prom_bussoft_ptr for use by sn_bus_fixup().
	 * (platform_data will be overwritten later in sn_common_bus_fixup())
	 */
	controller->platform_data = prom_bussoft_ptr;

	sn_legacy_pci_window_fixup(res,
			prom_bussoft_ptr->bs_legacy_io,
			prom_bussoft_ptr->bs_legacy_mem);
	pci_add_resource_offset(&resources,	&res[0],
			prom_bussoft_ptr->bs_legacy_io);
	pci_add_resource_offset(&resources,	&res[1],
			prom_bussoft_ptr->bs_legacy_mem);

	bus = pci_scan_root_bus(NULL, busnum, &pci_root_ops, controller,
				&resources);
 	if (bus == NULL) {
		kfree(res);
		kfree(controller);
	}
}

/*
 * sn_bus_fixup
 */
void
sn_bus_fixup(struct pci_bus *bus)
{
	struct pci_dev *pci_dev = NULL;
	struct pcibus_bussoft *prom_bussoft_ptr;

	if (!bus->parent) {  /* If root bus */
		prom_bussoft_ptr = PCI_CONTROLLER(bus)->platform_data;
		if (prom_bussoft_ptr == NULL) {
			printk(KERN_ERR
			       "sn_bus_fixup: 0x%04x:0x%02x Unable to "
			       "obtain prom_bussoft_ptr\n",
			       pci_domain_nr(bus), bus->number);
			return;
		}
		sn_common_bus_fixup(bus, prom_bussoft_ptr);
        }
        list_for_each_entry(pci_dev, &bus->devices, bus_list) {
                sn_io_slot_fixup(pci_dev);
        }

}

/*
 * sn_io_init - PROM does not have ACPI support to define nodes or root buses,
 *		so we need to do things the hard way, including initiating the
 *		bus scanning ourselves.
 */

void __init sn_io_init(void)
{
	int i, j;

	sn_fixup_ionodes();

	/* busses are not known yet ... */
	for (i = 0; i <= max_segment_number; i++)
		for (j = 0; j <= max_pcibus_number; j++)
			sn_pci_controller_fixup(i, j, NULL);
}
