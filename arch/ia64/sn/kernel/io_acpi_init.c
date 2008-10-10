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
#include <acpi/acnamesp.h>


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

struct sn_pcidev_match {
	u8 bus;
	unsigned int devfn;
	acpi_handle handle;
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
 * sn_acpi_hubdev_init() - This function is called by acpi_ns_get_device_callback()
 *			   for all SGIHUB and SGITIO acpi devices defined in the
 *			   DSDT. It obtains the hubdev_info pointer from the
 *			   ACPI vendor resource, which the PROM setup, and sets up the
 *			   hubdev_info in the pda.
 */

static acpi_status __init
sn_acpi_hubdev_init(acpi_handle handle, u32 depth, void *context, void **ret)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	u64 addr;
	struct hubdev_info *hubdev;
	struct hubdev_info *hubdev_ptr;
	int i;
	u64 nasid;
	struct acpi_resource *resource;
	acpi_status status;
	struct acpi_resource_vendor_typed *vendor;
	extern void sn_common_hubdev_init(struct hubdev_info *);

	status = acpi_get_vendor_resource(handle, METHOD_NAME__CRS,
					  &sn_uuid, &buffer);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR
		       "sn_acpi_hubdev_init: acpi_get_vendor_resource() "
		       "(0x%x) failed for: ", status);
		acpi_ns_print_node_pathname(handle, NULL);
		printk("\n");
		return AE_OK;		/* Continue walking namespace */
	}

	resource = buffer.pointer;
	vendor = &resource->data.vendor_typed;
	if ((vendor->byte_length - sizeof(struct acpi_vendor_uuid)) !=
	    sizeof(struct hubdev_info *)) {
		printk(KERN_ERR
		       "sn_acpi_hubdev_init: Invalid vendor data length: %d for: ",
		        vendor->byte_length);
		acpi_ns_print_node_pathname(handle, NULL);
		printk("\n");
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
	return AE_OK;		/* Continue walking namespace */
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
		printk(KERN_ERR "%s: "
		       "acpi_get_vendor_resource() failed (0x%x) for: ",
		       __func__, status);
		acpi_ns_print_node_pathname(handle, NULL);
		printk("\n");
		return NULL;
	}
	resource = buffer.pointer;
	vendor = &resource->data.vendor_typed;

	if ((vendor->byte_length - sizeof(struct acpi_vendor_uuid)) !=
	     sizeof(struct pcibus_bussoft *)) {
		printk(KERN_ERR
		       "%s: Invalid vendor data length %d\n",
			__func__, vendor->byte_length);
		kfree(buffer.pointer);
		return NULL;
	}
	memcpy(&addr, vendor->byte_data, sizeof(struct pcibus_bussoft *));
	prom_bussoft_ptr = __va((struct pcibus_bussoft *) addr);
	kfree(buffer.pointer);

	return prom_bussoft_ptr;
}

/*
 * sn_extract_device_info - Extract the pcidev_info and the sn_irq_info
 *			    pointers from the vendor resource using the
 *			    provided acpi handle, and copy the structures
 *			    into the argument buffers.
 */
static int
sn_extract_device_info(acpi_handle handle, struct pcidev_info **pcidev_info,
		    struct sn_irq_info **sn_irq_info)
{
	u64 addr;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct sn_irq_info *irq_info, *irq_info_prom;
	struct pcidev_info *pcidev_ptr, *pcidev_prom_ptr;
	struct acpi_resource *resource;
	int ret = 0;
	acpi_status status;
	struct acpi_resource_vendor_typed *vendor;

	/*
	 * The pointer to this device's pcidev_info structure in
	 * the PROM, is in the vendor resource.
	 */
	status = acpi_get_vendor_resource(handle, METHOD_NAME__CRS,
					  &sn_uuid, &buffer);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR
		       "%s: acpi_get_vendor_resource() failed (0x%x) for: ",
		        __func__, status);
		acpi_ns_print_node_pathname(handle, NULL);
		printk("\n");
		return 1;
	}

	resource = buffer.pointer;
	vendor = &resource->data.vendor_typed;
	if ((vendor->byte_length - sizeof(struct acpi_vendor_uuid)) !=
	    sizeof(struct pci_devdev_info *)) {
		printk(KERN_ERR
		       "%s: Invalid vendor data length: %d for: ",
		        __func__, vendor->byte_length);
		acpi_ns_print_node_pathname(handle, NULL);
		printk("\n");
		ret = 1;
		goto exit;
	}

	pcidev_ptr = kzalloc(sizeof(struct pcidev_info), GFP_KERNEL);
	if (!pcidev_ptr)
		panic("%s: Unable to alloc memory for pcidev_info", __func__);

	memcpy(&addr, vendor->byte_data, sizeof(struct pcidev_info *));
	pcidev_prom_ptr = __va(addr);
	memcpy(pcidev_ptr, pcidev_prom_ptr, sizeof(struct pcidev_info));

	/* Get the IRQ info */
	irq_info = kzalloc(sizeof(struct sn_irq_info), GFP_KERNEL);
	if (!irq_info)
		 panic("%s: Unable to alloc memory for sn_irq_info", __func__);

	if (pcidev_ptr->pdi_sn_irq_info) {
		irq_info_prom = __va(pcidev_ptr->pdi_sn_irq_info);
		memcpy(irq_info, irq_info_prom, sizeof(struct sn_irq_info));
	}

	*pcidev_info = pcidev_ptr;
	*sn_irq_info = irq_info;

exit:
	kfree(buffer.pointer);
	return ret;
}

static unsigned int
get_host_devfn(acpi_handle device_handle, acpi_handle rootbus_handle)
{
	unsigned long long adr;
	acpi_handle child;
	unsigned int devfn;
	int function;
	acpi_handle parent;
	int slot;
	acpi_status status;

	/*
	 * Do an upward search to find the root bus device, and
	 * obtain the host devfn from the previous child device.
	 */
	child = device_handle;
	while (child) {
		status = acpi_get_parent(child, &parent);
		if (ACPI_FAILURE(status)) {
			printk(KERN_ERR "%s: acpi_get_parent() failed "
			       "(0x%x) for: ", __func__, status);
			acpi_ns_print_node_pathname(child, NULL);
			printk("\n");
			panic("%s: Unable to find host devfn\n", __func__);
		}
		if (parent == rootbus_handle)
			break;
		child = parent;
	}
	if (!child) {
		printk(KERN_ERR "%s: Unable to find root bus for: ",
		       __func__);
		acpi_ns_print_node_pathname(device_handle, NULL);
		printk("\n");
		BUG();
	}

	status = acpi_evaluate_integer(child, METHOD_NAME__ADR, NULL, &adr);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR "%s: Unable to get _ADR (0x%x) for: ",
		       __func__, status);
		acpi_ns_print_node_pathname(child, NULL);
		printk("\n");
		panic("%s: Unable to find host devfn\n", __func__);
	}

	slot = (adr >> 16) & 0xffff;
	function = adr & 0xffff;
	devfn = PCI_DEVFN(slot, function);
	return devfn;
}

/*
 * find_matching_device - Callback routine to find the ACPI device
 *			  that matches up with our pci_dev device.
 *			  Matching is done on bus number and devfn.
 *			  To find the bus number for a particular
 *			  ACPI device, we must look at the _BBN method
 *			  of its parent.
 */
static acpi_status
find_matching_device(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	unsigned long long bbn = -1;
	unsigned long long adr;
	acpi_handle parent = NULL;
	acpi_status status;
	unsigned int devfn;
	int function;
	int slot;
	struct sn_pcidev_match *info = context;

        status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL,
                                       &adr);
        if (ACPI_SUCCESS(status)) {
		status = acpi_get_parent(handle, &parent);
		if (ACPI_FAILURE(status)) {
			printk(KERN_ERR
			       "%s: acpi_get_parent() failed (0x%x) for: ",
					__func__, status);
			acpi_ns_print_node_pathname(handle, NULL);
			printk("\n");
			return AE_OK;
		}
		status = acpi_evaluate_integer(parent, METHOD_NAME__BBN,
					       NULL, &bbn);
		if (ACPI_FAILURE(status)) {
			printk(KERN_ERR
			  "%s: Failed to find _BBN in parent of: ",
					__func__);
			acpi_ns_print_node_pathname(handle, NULL);
			printk("\n");
			return AE_OK;
		}

                slot = (adr >> 16) & 0xffff;
                function = adr & 0xffff;
                devfn = PCI_DEVFN(slot, function);
                if ((info->devfn == devfn) && (info->bus == bbn)) {
			/* We have a match! */
			info->handle = handle;
			return 1;
		}
	}
	return AE_OK;
}

/*
 * sn_acpi_get_pcidev_info - Search ACPI namespace for the acpi
 *			     device matching the specified pci_dev,
 *			     and return the pcidev info and irq info.
 */
int
sn_acpi_get_pcidev_info(struct pci_dev *dev, struct pcidev_info **pcidev_info,
			struct sn_irq_info **sn_irq_info)
{
	unsigned int host_devfn;
	struct sn_pcidev_match pcidev_match;
	acpi_handle rootbus_handle;
	unsigned long long segment;
	acpi_status status;

	rootbus_handle = PCI_CONTROLLER(dev)->acpi_handle;
        status = acpi_evaluate_integer(rootbus_handle, METHOD_NAME__SEG, NULL,
                                       &segment);
        if (ACPI_SUCCESS(status)) {
		if (segment != pci_domain_nr(dev)) {
			printk(KERN_ERR
			       "%s: Segment number mismatch, 0x%lx vs 0x%x for: ",
			       __func__, segment, pci_domain_nr(dev));
			acpi_ns_print_node_pathname(rootbus_handle, NULL);
			printk("\n");
			return 1;
		}
	} else {
		printk(KERN_ERR "%s: Unable to get __SEG from: ",
		       __func__);
		acpi_ns_print_node_pathname(rootbus_handle, NULL);
		printk("\n");
		return 1;
	}

	/*
	 * We want to search all devices in this segment/domain
	 * of the ACPI namespace for the matching ACPI device,
	 * which holds the pcidev_info pointer in its vendor resource.
	 */
	pcidev_match.bus = dev->bus->number;
	pcidev_match.devfn = dev->devfn;
	pcidev_match.handle = NULL;

	acpi_walk_namespace(ACPI_TYPE_DEVICE, rootbus_handle, ACPI_UINT32_MAX,
			    find_matching_device, &pcidev_match, NULL);

	if (!pcidev_match.handle) {
		printk(KERN_ERR
		       "%s: Could not find matching ACPI device for %s.\n",
		       __func__, pci_name(dev));
		return 1;
	}

	if (sn_extract_device_info(pcidev_match.handle, pcidev_info, sn_irq_info))
		return 1;

	/* Build up the pcidev_info.pdi_slot_host_handle */
	host_devfn = get_host_devfn(pcidev_match.handle, rootbus_handle);
	(*pcidev_info)->pdi_slot_host_handle =
			((unsigned long) pci_domain_nr(dev) << 40) |
					/* bus == 0 */
					host_devfn;
	return 0;
}

/*
 * sn_acpi_slot_fixup - Obtain the pcidev_info and sn_irq_info.
 *			Perform any SN specific slot fixup.
 *			At present there does not appear to be
 *			any generic way to handle a ROM image
 *			that has been shadowed by the PROM, so
 *			we pass a pointer to it	within the
 *			pcidev_info structure.
 */

void
sn_acpi_slot_fixup(struct pci_dev *dev)
{
	void __iomem *addr;
	struct pcidev_info *pcidev_info = NULL;
	struct sn_irq_info *sn_irq_info = NULL;
	size_t image_size, size;

	if (sn_acpi_get_pcidev_info(dev, &pcidev_info, &sn_irq_info)) {
		panic("%s:  Failure obtaining pcidev_info for %s\n",
		      __func__, pci_name(dev));
	}

	if (pcidev_info->pdi_pio_mapped_addr[PCI_ROM_RESOURCE]) {
		/*
		 * A valid ROM image exists and has been shadowed by the
		 * PROM. Setup the pci_dev ROM resource with the address
		 * of the shadowed copy, and the actual length of the ROM image.
		 */
		size = pci_resource_len(dev, PCI_ROM_RESOURCE);
		addr = ioremap(pcidev_info->pdi_pio_mapped_addr[PCI_ROM_RESOURCE],
			       size);
		image_size = pci_get_rom_size(addr, size);
		dev->resource[PCI_ROM_RESOURCE].start = (unsigned long) addr;
		dev->resource[PCI_ROM_RESOURCE].end =
					(unsigned long) addr + image_size - 1;
		dev->resource[PCI_ROM_RESOURCE].flags |= IORESOURCE_ROM_BIOS_COPY;
	}
	sn_pci_fixup_slot(dev, pcidev_info, sn_irq_info);
}

EXPORT_SYMBOL(sn_acpi_slot_fixup);


/*
 * sn_acpi_bus_fixup -  Perform SN specific setup of software structs
 *			(pcibus_bussoft, pcidev_info) and hardware
 *			registers, for the specified bus and devices under it.
 */
void
sn_acpi_bus_fixup(struct pci_bus *bus)
{
	struct pci_dev *pci_dev = NULL;
	struct pcibus_bussoft *prom_bussoft_ptr;

	if (!bus->parent) {	/* If root bus */
		prom_bussoft_ptr = sn_get_bussoft_ptr(bus);
		if (prom_bussoft_ptr == NULL) {
			printk(KERN_ERR
			       "%s: 0x%04x:0x%02x Unable to "
			       "obtain prom_bussoft_ptr\n",
			       __func__, pci_domain_nr(bus), bus->number);
			return;
		}
		sn_common_bus_fixup(bus, prom_bussoft_ptr);
	}
	list_for_each_entry(pci_dev, &bus->devices, bus_list) {
		sn_acpi_slot_fixup(pci_dev);
	}
}

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

	/* SN Altix does not follow the IOSAPIC IRQ routing model */
	acpi_irq_model = ACPI_IRQ_MODEL_PLATFORM;

	/* Setup hubdev_info for all SGIHUB/SGITIO devices */
	acpi_get_devices("SGIHUB", sn_acpi_hubdev_init, NULL, NULL);
	acpi_get_devices("SGITIO", sn_acpi_hubdev_init, NULL, NULL);

	status = sal_ioif_init(&result);
	if (status || result)
		panic("sal_ioif_init failed: [%lx] %s\n",
		      status, ia64_sal_strerror(status));
}
