/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Silicon Graphics, Inc. All rights reserved.
 */

#include <asm/sn/types.h>
#include <asm/sn/addrs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/sn_sal.h>
#include "xtalk/hubdev.h"
#include <linux/acpi.h>


/*
 * The code in this file will only be executed when running with
 * a PROM that has ACPI IO support. (i.e., SN_ACPI_BASE_SUPPORT() == 1)
 */


/*
 * This value must match the UUID the PROM uses
 * (io/acpi/defblk.c) when building a vendor descriptor.
 */
struct acpi_vendor_uuid sn_uuid = {
	.subtype = 0,
	.data	= { 0x2c, 0xc6, 0xa6, 0xfe, 0x9c, 0x44, 0xda, 0x11,
		    0xa2, 0x7c, 0x08, 0x00, 0x69, 0x13, 0xea, 0x51 },
};

/*
 * Perform the early IO init in PROM.
 */
static s64
sal_ioif_init(u64 *result)
{
	struct ia64_sal_retval isrv = {0,0,0,0};

	SAL_CALL_NOLOCK(isrv,
			SN_SAL_IOIF_INIT, 0, 0, 0, 0, 0, 0, 0);
	*result = isrv.v0;
	return isrv.status;
}

/*
 * sn_hubdev_add - The 'add' function of the acpi_sn_hubdev_driver.
 *		   Called for every "SGIHUB" or "SGITIO" device defined
 *		   in the ACPI namespace.
 */
static int __init
sn_hubdev_add(struct acpi_device *device)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	u64 addr;
	struct hubdev_info *hubdev;
	struct hubdev_info *hubdev_ptr;
	int i;
	u64 nasid;
	struct acpi_resource *resource;
	int ret = 0;
	acpi_status status;
	struct acpi_resource_vendor_typed *vendor;
	extern void sn_common_hubdev_init(struct hubdev_info *);

	status = acpi_get_vendor_resource(device->handle, METHOD_NAME__CRS,
					  &sn_uuid, &buffer);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR
		       "sn_hubdev_add: acpi_get_vendor_resource() failed: %d\n",
		        status);
		return 1;
	}

	resource = buffer.pointer;
	vendor = &resource->data.vendor_typed;
	if ((vendor->byte_length - sizeof(struct acpi_vendor_uuid)) !=
	    sizeof(struct hubdev_info *)) {
		printk(KERN_ERR
		       "sn_hubdev_add: Invalid vendor data length: %d\n",
		        vendor->byte_length);
		ret = 1;
		goto exit;
	}

	memcpy(&addr, vendor->byte_data, sizeof(struct hubdev_info *));
	hubdev_ptr = __va((struct hubdev_info *) addr);

	nasid = hubdev_ptr->hdi_nasid;
	i = nasid_to_cnodeid(nasid);
	hubdev = (struct hubdev_info *)(NODEPDA(i)->pdinfo);
	*hubdev = *hubdev_ptr;
	sn_common_hubdev_init(hubdev);

exit:
	kfree(buffer.pointer);
	return ret;
}

/*
 * sn_get_bussoft_ptr() - The pcibus_bussoft pointer is found in
 *			  the ACPI Vendor resource for this bus.
 */
static struct pcibus_bussoft *
sn_get_bussoft_ptr(struct pci_bus *bus)
{
	u64 addr;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_handle handle;
	struct pcibus_bussoft *prom_bussoft_ptr;
	struct acpi_resource *resource;
	acpi_status status;
	struct acpi_resource_vendor_typed *vendor;


	handle = PCI_CONTROLLER(bus)->acpi_handle;
	status = acpi_get_vendor_resource(handle, METHOD_NAME__CRS,
					  &sn_uuid, &buffer);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR "get_acpi_pcibus_ptr: "
		       "get_acpi_bussoft_info() failed: %d\n",
		       status);
		return NULL;
	}
	resource = buffer.pointer;
	vendor = &resource->data.vendor_typed;

	if ((vendor->byte_length - sizeof(struct acpi_vendor_uuid)) !=
	     sizeof(struct pcibus_bussoft *)) {
		printk(KERN_ERR
		       "get_acpi_bussoft_ptr: Invalid vendor data "
		       "length %d\n", vendor->byte_length);
		kfree(buffer.pointer);
		return NULL;
	}
	memcpy(&addr, vendor->byte_data, sizeof(struct pcibus_bussoft *));
	prom_bussoft_ptr = __va((struct pcibus_bussoft *) addr);
	kfree(buffer.pointer);

	return prom_bussoft_ptr;
}

/*
 * sn_acpi_bus_fixup
 */
void
sn_acpi_bus_fixup(struct pci_bus *bus)
{
	struct pci_dev *pci_dev = NULL;
	struct pcibus_bussoft *prom_bussoft_ptr;
	extern void sn_common_bus_fixup(struct pci_bus *,
					struct pcibus_bussoft *);

	if (!bus->parent) {	/* If root bus */
		prom_bussoft_ptr = sn_get_bussoft_ptr(bus);
		if (prom_bussoft_ptr == NULL) {
			printk(KERN_ERR
			       "sn_pci_fixup_bus: 0x%04x:0x%02x Unable to "
			       "obtain prom_bussoft_ptr\n",
			       pci_domain_nr(bus), bus->number);
			return;
		}
		sn_common_bus_fixup(bus, prom_bussoft_ptr);
	}
	list_for_each_entry(pci_dev, &bus->devices, bus_list) {
		sn_pci_fixup_slot(pci_dev);
	}
}

static struct acpi_driver acpi_sn_hubdev_driver = {
	.name = "SGI HUBDEV Driver",
	.ids = "SGIHUB,SGITIO",
	.ops = {
		.add = sn_hubdev_add,
		},
};


/*
 * sn_io_acpi_init - PROM has ACPI support for IO, defining at a minimum the
 *		     nodes and root buses in the DSDT. As a result, bus scanning
 *		     will be initiated by the Linux ACPI code.
 */

void __init
sn_io_acpi_init(void)
{
	u64 result;
	s64 status;

	acpi_bus_register_driver(&acpi_sn_hubdev_driver);
	status = sal_ioif_init(&result);
	if (status || result)
		panic("sal_ioif_init failed: [%lx] %s\n",
		      status, ia64_sal_strerror(status));
}
