/*
 * PCIEHPRM ACPI: PHP Resource Manager for ACPI platform
 *
 * Copyright (C) 2003-2004 Intel Corporation
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <kristen.c.accardi@intel.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/pci-acpi.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#ifdef	CONFIG_IA64
#include <asm/iosapic.h>
#endif
#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/actypes.h>
#include "pciehp.h"
#include "pciehprm.h"

#define	PCI_MAX_BUS		0x100
#define	ACPI_STA_DEVICE_PRESENT	0x01

#define	METHOD_NAME__SUN	"_SUN"
#define	METHOD_NAME__HPP	"_HPP"
#define	METHOD_NAME_OSHP	"OSHP"

/* Status code for running acpi method to gain native control */
#define NC_NOT_RUN	0
#define OSC_NOT_EXIST	1
#define OSC_RUN_FAILED	2
#define OSHP_NOT_EXIST	3
#define OSHP_RUN_FAILED	4
#define NC_RUN_SUCCESS	5

#define	PHP_RES_BUS		0xA0
#define	PHP_RES_IO		0xA1
#define	PHP_RES_MEM		0xA2
#define	PHP_RES_PMEM		0xA3

#define	BRIDGE_TYPE_P2P		0x00
#define	BRIDGE_TYPE_HOST	0x01

/* this should go to drivers/acpi/include/ */
struct acpi__hpp {
	u8	cache_line_size;
	u8	latency_timer;
	u8	enable_serr;
	u8	enable_perr;
};

struct acpi_php_slot {
	struct acpi_php_slot	*next;
	struct acpi_bridge	*bridge;
	acpi_handle	handle;
	int	seg;
	int	bus;
	int	dev;
	int	fun;
	u32	sun;
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	void	*slot_ops;	/* _STA, _EJx, etc */
	struct slot *slot;
};		/* per func */

struct acpi_bridge {
	struct acpi_bridge	*parent;
	struct acpi_bridge	*next;
	struct acpi_bridge	*child;
	acpi_handle	handle;
	int seg;
	int pbus;			/* pdev->bus->number		*/
	int pdevice;			/* PCI_SLOT(pdev->devfn)	*/
	int pfunction;			/* PCI_DEVFN(pdev->devfn)	*/
	int bus;			/* pdev->subordinate->number	*/
	struct acpi__hpp		*_hpp;
	struct acpi_php_slot	*slots;
	struct pci_resource 	*tmem_head;	/* total from crs	*/
	struct pci_resource 	*tp_mem_head;	/* total from crs	*/
	struct pci_resource 	*tio_head;	/* total from crs	*/
	struct pci_resource 	*tbus_head;	/* total from crs	*/
	struct pci_resource 	*mem_head;	/* available	*/
	struct pci_resource 	*p_mem_head;	/* available	*/
	struct pci_resource 	*io_head;	/* available	*/
	struct pci_resource 	*bus_head;	/* available	*/
	int scanned;
	int type;
};

static struct acpi_bridge *acpi_bridges_head;

static u8 * acpi_path_name( acpi_handle	handle)
{
	acpi_status		status;
	static u8		path_name[ACPI_PATHNAME_MAX];
	struct acpi_buffer	ret_buf = { ACPI_PATHNAME_MAX, path_name };

	memset(path_name, 0, sizeof (path_name));
	status = acpi_get_name(handle, ACPI_FULL_PATHNAME, &ret_buf);

	if (ACPI_FAILURE(status))
		return NULL;
	else
		return path_name;	
}

static void acpi_get__hpp ( struct acpi_bridge	*ab);
static int acpi_run_oshp ( struct acpi_bridge	*ab);
static int osc_run_status = NC_NOT_RUN;
static int oshp_run_status = NC_NOT_RUN;

static int acpi_add_slot_to_php_slots(
	struct acpi_bridge	*ab,
	int			bus_num,
	acpi_handle		handle,
	u32			adr,
	u32			sun
	)
{
	struct acpi_php_slot	*aps;
	static long	samesun = -1;

	aps = (struct acpi_php_slot *) kmalloc (sizeof(struct acpi_php_slot), GFP_KERNEL);
	if (!aps) {
		err ("acpi_pciehprm: alloc for aps fail\n");
		return -1;
	}
	memset(aps, 0, sizeof(struct acpi_php_slot));

	aps->handle = handle;
	aps->bus = bus_num;
	aps->dev = (adr >> 16) & 0xffff;
	aps->fun = adr & 0xffff;
	aps->sun = sun;

	aps->next = ab->slots;	/* cling to the bridge */
	aps->bridge = ab;
	ab->slots = aps;

	ab->scanned += 1;
	if (!ab->_hpp)
		acpi_get__hpp(ab);
	
	if (osc_run_status == OSC_NOT_EXIST)
		oshp_run_status = acpi_run_oshp(ab);

	if (sun != samesun) {
		info("acpi_pciehprm:   Slot sun(%x) at s:b:d:f=0x%02x:%02x:%02x:%02x\n", 
			aps->sun, ab->seg, aps->bus, aps->dev, aps->fun);
		samesun = sun;
	}
	return 0;
}

static void acpi_get__hpp ( struct acpi_bridge	*ab)
{
	acpi_status		status;
	u8			nui[4];
	struct acpi_buffer	ret_buf = { 0, NULL};
	union acpi_object	*ext_obj, *package;
	u8			*path_name = acpi_path_name(ab->handle);
	int			i, len = 0;

	/* get _hpp */
	status = acpi_evaluate_object(ab->handle, METHOD_NAME__HPP, NULL, &ret_buf);
	switch (status) {
	case AE_BUFFER_OVERFLOW:
		ret_buf.pointer = kmalloc (ret_buf.length, GFP_KERNEL);
		if (!ret_buf.pointer) {
			err ("acpi_pciehprm:%s alloc for _HPP fail\n", path_name);
			return;
		}
		status = acpi_evaluate_object(ab->handle, METHOD_NAME__HPP, NULL, &ret_buf);
		if (ACPI_SUCCESS(status))
			break;
	default:
		if (ACPI_FAILURE(status)) {
			err("acpi_pciehprm:%s _HPP fail=0x%x\n", path_name, status);
			return;
		}
	}

	ext_obj = (union acpi_object *) ret_buf.pointer;
	if (ext_obj->type != ACPI_TYPE_PACKAGE) {
		err ("acpi_pciehprm:%s _HPP obj not a package\n", path_name);
		goto free_and_return;
	}

	len = ext_obj->package.count;
	package = (union acpi_object *) ret_buf.pointer;
	for ( i = 0; (i < len) || (i < 4); i++) {
		ext_obj = (union acpi_object *) &package->package.elements[i];
		switch (ext_obj->type) {
		case ACPI_TYPE_INTEGER:
			nui[i] = (u8)ext_obj->integer.value;
			break;
		default:
			err ("acpi_pciehprm:%s _HPP obj type incorrect\n", path_name);
			goto free_and_return;
		}
	}

	ab->_hpp = kmalloc (sizeof (struct acpi__hpp), GFP_KERNEL);
	if (!ab->_hpp) {
		err ("acpi_pciehprm:%s alloc for _HPP failed\n", path_name);
		goto free_and_return;
	}
	memset(ab->_hpp, 0, sizeof(struct acpi__hpp));

	ab->_hpp->cache_line_size	= nui[0];
	ab->_hpp->latency_timer		= nui[1];
	ab->_hpp->enable_serr		= nui[2];
	ab->_hpp->enable_perr		= nui[3];

	dbg("  _HPP: cache_line_size=0x%x\n", ab->_hpp->cache_line_size);
	dbg("  _HPP: latency timer  =0x%x\n", ab->_hpp->latency_timer);
	dbg("  _HPP: enable SERR    =0x%x\n", ab->_hpp->enable_serr);
	dbg("  _HPP: enable PERR    =0x%x\n", ab->_hpp->enable_perr);

free_and_return:
	kfree(ret_buf.pointer);
}

static int acpi_run_oshp ( struct acpi_bridge	*ab)
{
	acpi_status		status;
	u8			*path_name = acpi_path_name(ab->handle);

	/* run OSHP */
	status = acpi_evaluate_object(ab->handle, METHOD_NAME_OSHP, NULL, NULL);
	if (ACPI_FAILURE(status)) {
		err("acpi_pciehprm:%s OSHP fails=0x%x\n", path_name, status);
		oshp_run_status = (status == AE_NOT_FOUND) ? OSHP_NOT_EXIST : OSHP_RUN_FAILED;
	} else {
		oshp_run_status = NC_RUN_SUCCESS;
		dbg("acpi_pciehprm:%s OSHP passes =0x%x\n", path_name, status);
		dbg("acpi_pciehprm:%s oshp_run_status =0x%x\n", path_name, oshp_run_status);
	}
	return oshp_run_status;
}

static acpi_status acpi_evaluate_crs(
	acpi_handle		handle,
	struct acpi_resource	**retbuf
	)
{
	acpi_status		status;
	struct acpi_buffer	crsbuf;
	u8			*path_name = acpi_path_name(handle);

	crsbuf.length  = 0;
	crsbuf.pointer = NULL;

	status = acpi_get_current_resources (handle, &crsbuf);

	switch (status) {
	case AE_BUFFER_OVERFLOW:
		break;		/* found */
	case AE_NOT_FOUND:
		dbg("acpi_pciehprm:%s _CRS not found\n", path_name);
		return status;
	default:
		err ("acpi_pciehprm:%s _CRS fail=0x%x\n", path_name, status);
		return status;
	}

	crsbuf.pointer = kmalloc (crsbuf.length, GFP_KERNEL);
	if (!crsbuf.pointer) {
		err ("acpi_pciehprm: alloc %ld bytes for %s _CRS fail\n", (ulong)crsbuf.length, path_name);
		return AE_NO_MEMORY;
	}

	status = acpi_get_current_resources (handle, &crsbuf);
	if (ACPI_FAILURE(status)) {
		err("acpi_pciehprm: %s _CRS fail=0x%x.\n", path_name, status);
		kfree(crsbuf.pointer);
		return status;
	}

	*retbuf = crsbuf.pointer;

	return status;
}

static void free_pci_resource ( struct pci_resource	*aprh)
{
	struct pci_resource	*res, *next;

	for (res = aprh; res; res = next) {
		next = res->next;
		kfree(res);
	}
}

static void print_pci_resource ( struct pci_resource	*aprh)
{
	struct pci_resource	*res;

	for (res = aprh; res; res = res->next)
		dbg("        base= 0x%x length= 0x%x\n", res->base, res->length);
}

static void print_slot_resources( struct acpi_php_slot	*aps)
{
	if (aps->bus_head) {
		dbg("    BUS Resources:\n");
		print_pci_resource (aps->bus_head);
	}

	if (aps->io_head) {
		dbg("    IO Resources:\n");
		print_pci_resource (aps->io_head);
	}

	if (aps->mem_head) {
		dbg("    MEM Resources:\n");
		print_pci_resource (aps->mem_head);
	}

	if (aps->p_mem_head) {
		dbg("    PMEM Resources:\n");
		print_pci_resource (aps->p_mem_head);
	}
}

static void print_pci_resources( struct acpi_bridge	*ab)
{
	if (ab->tbus_head) {
		dbg("    Total BUS Resources:\n");
		print_pci_resource (ab->tbus_head);
	}
	if (ab->bus_head) {
		dbg("    BUS Resources:\n");
		print_pci_resource (ab->bus_head);
	}

	if (ab->tio_head) {
		dbg("    Total IO Resources:\n");
		print_pci_resource (ab->tio_head);
	}
	if (ab->io_head) {
		dbg("    IO Resources:\n");
		print_pci_resource (ab->io_head);
	}

	if (ab->tmem_head) {
		dbg("    Total MEM Resources:\n");
		print_pci_resource (ab->tmem_head);
	}
	if (ab->mem_head) {
		dbg("    MEM Resources:\n");
		print_pci_resource (ab->mem_head);
	}

	if (ab->tp_mem_head) {
		dbg("    Total PMEM Resources:\n");
		print_pci_resource (ab->tp_mem_head);
	}
	if (ab->p_mem_head) {
		dbg("    PMEM Resources:\n");
		print_pci_resource (ab->p_mem_head);
	}
	if (ab->_hpp) {
		dbg("    _HPP: cache_line_size=0x%x\n", ab->_hpp->cache_line_size);
		dbg("    _HPP: latency timer  =0x%x\n", ab->_hpp->latency_timer);
		dbg("    _HPP: enable SERR    =0x%x\n", ab->_hpp->enable_serr);
		dbg("    _HPP: enable PERR    =0x%x\n", ab->_hpp->enable_perr);
	}
}

static int pciehprm_delete_resource(
	struct pci_resource **aprh,
	ulong base,
	ulong size)
{
	struct pci_resource *res;
	struct pci_resource *prevnode;
	struct pci_resource *split_node;
	ulong tbase;

	pciehp_resource_sort_and_combine(aprh);

	for (res = *aprh; res; res = res->next) {
		if (res->base > base)
			continue;

		if ((res->base + res->length) < (base + size))
			continue;

		if (res->base < base) {
			tbase = base;

			if ((res->length - (tbase - res->base)) < size)
				continue;

			split_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!split_node)
				return -ENOMEM;

			split_node->base = res->base;
			split_node->length = tbase - res->base;
			res->base = tbase;
			res->length -= split_node->length;

			split_node->next = res->next;
			res->next = split_node;
		}

		if (res->length >= size) {
			split_node = (struct pci_resource*) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!split_node)
				return -ENOMEM;

			split_node->base = res->base + size;
			split_node->length = res->length - size;
			res->length = size;

			split_node->next = res->next;
			res->next = split_node;
		}

		if (*aprh == res) {
			*aprh = res->next;
		} else {
			prevnode = *aprh;
			while (prevnode->next != res)
				prevnode = prevnode->next;

			prevnode->next = res->next;
		}
		res->next = NULL;
		kfree(res);
		break;
	}

	return 0;
}

static int pciehprm_delete_resources(
	struct pci_resource **aprh,
	struct pci_resource *this
	)
{
	struct pci_resource *res;

	for (res = this; res; res = res->next)
		pciehprm_delete_resource(aprh, res->base, res->length);

	return 0;
}

static int pciehprm_add_resource(
	struct pci_resource **aprh,
	ulong base,
	ulong size)
{
	struct pci_resource *res;

	for (res = *aprh; res; res = res->next) {
		if ((res->base + res->length) == base) {
			res->length += size;
			size = 0L;
			break;
		}
		if (res->next == *aprh)
			break;
	}

	if (size) {
		res = kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
		if (!res) {
			err ("acpi_pciehprm: alloc for res fail\n");
			return -ENOMEM;
		}
		memset(res, 0, sizeof (struct pci_resource));

		res->base = base;
		res->length = size;
		res->next = *aprh;
		*aprh = res;
	}

	return 0;
}

static int pciehprm_add_resources(
	struct pci_resource **aprh,
	struct pci_resource *this
	)
{
	struct pci_resource *res;
	int	rc = 0;

	for (res = this; res && !rc; res = res->next)
		rc = pciehprm_add_resource(aprh, res->base, res->length);

	return rc;
}

static void acpi_parse_io (
	struct acpi_bridge		*ab,
	union acpi_resource_data	*data
	)
{
	struct acpi_resource_io	*dataio;
	dataio = (struct acpi_resource_io *) data;

	dbg("Io Resource\n");
	dbg("  %d bit decode\n", ACPI_DECODE_16 == dataio->io_decode ? 16:10);
	dbg("  Range minimum base: %08X\n", dataio->min_base_address);
	dbg("  Range maximum base: %08X\n", dataio->max_base_address);
	dbg("  Alignment: %08X\n", dataio->alignment);
	dbg("  Range Length: %08X\n", dataio->range_length);
}

static void acpi_parse_fixed_io (
	struct acpi_bridge	*ab,
	union acpi_resource_data	*data
	)
{
	struct acpi_resource_fixed_io  *datafio;
	datafio = (struct acpi_resource_fixed_io *) data;

	dbg("Fixed Io Resource\n");
	dbg("  Range base address: %08X", datafio->base_address);
	dbg("  Range length: %08X", datafio->range_length);
}

static void acpi_parse_address16_32 (
	struct acpi_bridge	*ab,
	union acpi_resource_data	*data,
	acpi_resource_type	id
	)
{
	/* 
	 * acpi_resource_address16 == acpi_resource_address32
	 * acpi_resource_address16 *data16 = (acpi_resource_address16 *) data;
	 */
	struct acpi_resource_address32 *data32 = (struct acpi_resource_address32 *) data;
	struct pci_resource **aprh, **tprh;

	if (id == ACPI_RSTYPE_ADDRESS16)
		dbg("acpi_pciehprm:16-Bit Address Space Resource\n");
	else
		dbg("acpi_pciehprm:32-Bit Address Space Resource\n");

	switch (data32->resource_type) {
	case ACPI_MEMORY_RANGE: 
		dbg("  Resource Type: Memory Range\n");
		aprh = &ab->mem_head;
		tprh = &ab->tmem_head;

		switch (data32->attribute.memory.cache_attribute) {
		case ACPI_NON_CACHEABLE_MEMORY:
			dbg("  Type Specific: Noncacheable memory\n");
			break; 
		case ACPI_CACHABLE_MEMORY:
			dbg("  Type Specific: Cacheable memory\n");
			break; 
		case ACPI_WRITE_COMBINING_MEMORY:
			dbg("  Type Specific: Write-combining memory\n");
			break; 
		case ACPI_PREFETCHABLE_MEMORY:
			aprh = &ab->p_mem_head;
			dbg("  Type Specific: Prefetchable memory\n");
			break; 
		default:
			dbg("  Type Specific: Invalid cache attribute\n");
			break;
		}

		dbg("  Type Specific: Read%s\n", ACPI_READ_WRITE_MEMORY == data32->attribute.memory.read_write_attribute ? "/Write":" Only");
		break;

	case ACPI_IO_RANGE: 
		dbg("  Resource Type: I/O Range\n");
		aprh = &ab->io_head;
		tprh = &ab->tio_head;

		switch (data32->attribute.io.range_attribute) {
		case ACPI_NON_ISA_ONLY_RANGES:
			dbg("  Type Specific: Non-ISA Io Addresses\n");
			break; 
		case ACPI_ISA_ONLY_RANGES:
			dbg("  Type Specific: ISA Io Addresses\n");
			break; 
		case ACPI_ENTIRE_RANGE:
			dbg("  Type Specific: ISA and non-ISA Io Addresses\n");
			break; 
		default:
			dbg("  Type Specific: Invalid range attribute\n");
			break;
		}
		break;

	case ACPI_BUS_NUMBER_RANGE: 
		dbg("  Resource Type: Bus Number Range(fixed)\n");
		/* fixup to be compatible with the rest of php driver */
		data32->min_address_range++;
		data32->address_length--;
		aprh = &ab->bus_head;
		tprh = &ab->tbus_head;
		break; 
	default: 
		dbg("  Resource Type: Invalid resource type. Exiting.\n");
		return;
	}

	dbg("  Resource %s\n", ACPI_CONSUMER == data32->producer_consumer ? "Consumer":"Producer");
	dbg("  %s decode\n", ACPI_SUB_DECODE == data32->decode ? "Subtractive":"Positive");
	dbg("  Min address is %s fixed\n", ACPI_ADDRESS_FIXED == data32->min_address_fixed ? "":"not");
	dbg("  Max address is %s fixed\n", ACPI_ADDRESS_FIXED == data32->max_address_fixed ? "":"not");
	dbg("  Granularity: %08X\n", data32->granularity);
	dbg("  Address range min: %08X\n", data32->min_address_range);
	dbg("  Address range max: %08X\n", data32->max_address_range);
	dbg("  Address translation offset: %08X\n", data32->address_translation_offset);
	dbg("  Address Length: %08X\n", data32->address_length);

	if (0xFF != data32->resource_source.index) {
		dbg("  Resource Source Index: %X\n", data32->resource_source.index);
		/* dbg("  Resource Source: %s\n", data32->resource_source.string_ptr); */
	}

	pciehprm_add_resource(aprh, data32->min_address_range, data32->address_length);
}

static acpi_status acpi_parse_crs(
	struct acpi_bridge	*ab,
	struct acpi_resource	*crsbuf
	)
{
	acpi_status		status = AE_OK;
	struct acpi_resource	*resource = crsbuf;
	u8				count = 0;
	u8				done = 0;

	while (!done) {
		dbg("acpi_pciehprm: PCI bus 0x%x Resource structure %x.\n", ab->bus, count++);
		switch (resource->id) {
		case ACPI_RSTYPE_IRQ:
			dbg("Irq -------- Resource\n");
			break; 
		case ACPI_RSTYPE_DMA:
			dbg("DMA -------- Resource\n");
			break; 
		case ACPI_RSTYPE_START_DPF:
			dbg("Start DPF -------- Resource\n");
			break; 
		case ACPI_RSTYPE_END_DPF:
			dbg("End DPF -------- Resource\n");
			break; 
		case ACPI_RSTYPE_IO:
			acpi_parse_io (ab, &resource->data);
			break; 
		case ACPI_RSTYPE_FIXED_IO:
			acpi_parse_fixed_io (ab, &resource->data);
			break; 
		case ACPI_RSTYPE_VENDOR:
			dbg("Vendor -------- Resource\n");
			break; 
		case ACPI_RSTYPE_END_TAG:
			dbg("End_tag -------- Resource\n");
			done = 1;
			break; 
		case ACPI_RSTYPE_MEM24:
			dbg("Mem24 -------- Resource\n");
			break; 
		case ACPI_RSTYPE_MEM32:
			dbg("Mem32 -------- Resource\n");
			break; 
		case ACPI_RSTYPE_FIXED_MEM32:
			dbg("Fixed Mem32 -------- Resource\n");
			break; 
		case ACPI_RSTYPE_ADDRESS16:
			acpi_parse_address16_32(ab, &resource->data, ACPI_RSTYPE_ADDRESS16);
			break; 
		case ACPI_RSTYPE_ADDRESS32:
			acpi_parse_address16_32(ab, &resource->data, ACPI_RSTYPE_ADDRESS32);
			break; 
		case ACPI_RSTYPE_ADDRESS64:
			info("Address64 -------- Resource unparsed\n");
			break; 
		case ACPI_RSTYPE_EXT_IRQ:
			dbg("Ext Irq -------- Resource\n");
			break; 
		default:
			dbg("Invalid -------- resource type 0x%x\n", resource->id);
			break;
		}

		resource = (struct acpi_resource *) ((char *)resource + resource->length);
	}

	return status;
}

static acpi_status acpi_get_crs( struct acpi_bridge	*ab)
{
	acpi_status		status;
	struct acpi_resource	*crsbuf;

	status = acpi_evaluate_crs(ab->handle, &crsbuf);
	if (ACPI_SUCCESS(status)) {
		status = acpi_parse_crs(ab, crsbuf);
		kfree(crsbuf);

		pciehp_resource_sort_and_combine(&ab->bus_head);
		pciehp_resource_sort_and_combine(&ab->io_head);
		pciehp_resource_sort_and_combine(&ab->mem_head);
		pciehp_resource_sort_and_combine(&ab->p_mem_head);

		pciehprm_add_resources (&ab->tbus_head, ab->bus_head);
		pciehprm_add_resources (&ab->tio_head, ab->io_head);
		pciehprm_add_resources (&ab->tmem_head, ab->mem_head);
		pciehprm_add_resources (&ab->tp_mem_head, ab->p_mem_head);
	}

	return status;
}

/* find acpi_bridge downword from ab.  */
static struct acpi_bridge *
find_acpi_bridge_by_bus(
	struct acpi_bridge *ab,
	int seg,
	int bus		/* pdev->subordinate->number */
	)
{
	struct acpi_bridge	*lab = NULL;

	if (!ab)
		return NULL;

	if ((ab->bus == bus) && (ab->seg == seg))
		return ab;

	if (ab->child)
		lab = find_acpi_bridge_by_bus(ab->child, seg, bus);

	if (!lab)
	if (ab->next)
		lab = find_acpi_bridge_by_bus(ab->next, seg, bus);

	return lab;
}

/*
 * Build a device tree of ACPI PCI Bridges
 */
static void pciehprm_acpi_register_a_bridge (
	struct acpi_bridge	**head,
	struct acpi_bridge	*pab,	/* parent bridge to which child bridge is added */
	struct acpi_bridge	*cab	/* child bridge to add */
	)
{
	struct acpi_bridge	*lpab;
	struct acpi_bridge	*lcab;

	lpab = find_acpi_bridge_by_bus(*head, pab->seg, pab->bus);
	if (!lpab) {
		if (!(pab->type & BRIDGE_TYPE_HOST))
			warn("PCI parent bridge s:b(%x:%x) not in list.\n", pab->seg, pab->bus);
		pab->next = *head;
		*head = pab;
		lpab = pab;
	}

	if ((cab->type & BRIDGE_TYPE_HOST) && (pab == cab))
		return;

	lcab = find_acpi_bridge_by_bus(*head, cab->seg, cab->bus);
	if (lcab) {
		if ((pab->bus != lcab->parent->bus) || (lcab->bus != cab->bus))
			err("PCI child bridge s:b(%x:%x) in list with diff parent.\n", cab->seg, cab->bus);
		return;
	} else
		lcab = cab;

	lcab->parent = lpab;
	lcab->next = lpab->child;
	lpab->child = lcab;
}

static acpi_status pciehprm_acpi_build_php_slots_callback(
	acpi_handle		handle,
	u32			Level,
	void			*context,
	void			**retval
	)
{
	ulong		bus_num;
	ulong		seg_num;
	ulong		sun, adr;
	ulong		padr = 0;
	acpi_handle		phandle = NULL;
	struct acpi_bridge	*pab = (struct acpi_bridge *)context;
	struct acpi_bridge	*lab;
	acpi_status		status;
	u8			*path_name = acpi_path_name(handle);

	/* get _SUN */
	status = acpi_evaluate_integer(handle, METHOD_NAME__SUN, NULL, &sun);
	switch(status) {
	case AE_NOT_FOUND:
		return AE_OK;
	default:
		if (ACPI_FAILURE(status)) {
			err("acpi_pciehprm:%s _SUN fail=0x%x\n", path_name, status);
			return status;
		}
	}

	/* get _ADR. _ADR must exist if _SUN exists */
	status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL, &adr);
	if (ACPI_FAILURE(status)) {
		err("acpi_pciehprm:%s _ADR fail=0x%x\n", path_name, status);
		return status;
	}

	dbg("acpi_pciehprm:%s sun=0x%08x adr=0x%08x\n", path_name, (u32)sun, (u32)adr);

	status = acpi_get_parent(handle, &phandle);
	if (ACPI_FAILURE(status)) {
		err("acpi_pciehprm:%s get_parent fail=0x%x\n", path_name, status);
		return (status);
	}

	bus_num = pab->bus;
	seg_num = pab->seg;

	if (pab->bus == bus_num) {
		lab = pab;
	} else {
		dbg("WARN: pab is not parent\n");
		lab = find_acpi_bridge_by_bus(pab, seg_num, bus_num);
		if (!lab) {
			dbg("acpi_pciehprm: alloc new P2P bridge(%x) for sun(%08x)\n", (u32)bus_num, (u32)sun);
			lab = (struct acpi_bridge *)kmalloc(sizeof(struct acpi_bridge), GFP_KERNEL);
			if (!lab) {
				err("acpi_pciehprm: alloc for ab fail\n");
				return AE_NO_MEMORY;
			}
			memset(lab, 0, sizeof(struct acpi_bridge));

			lab->handle = phandle;
			lab->pbus = pab->bus;
			lab->pdevice = (int)(padr >> 16) & 0xffff;
			lab->pfunction = (int)(padr & 0xffff);
			lab->bus = (int)bus_num;
			lab->scanned = 0;
			lab->type = BRIDGE_TYPE_P2P;

			pciehprm_acpi_register_a_bridge (&acpi_bridges_head, pab, lab);
		} else
			dbg("acpi_pciehprm: found P2P bridge(%x) for sun(%08x)\n", (u32)bus_num, (u32)sun);
	}

	acpi_add_slot_to_php_slots(lab, (int)bus_num, handle, (u32)adr, (u32)sun);

	return (status);
}

static int pciehprm_acpi_build_php_slots(
	struct acpi_bridge	*ab,
	u32			depth
	)
{
	acpi_status	status;
	u8		*path_name = acpi_path_name(ab->handle);

	/* Walk down this pci bridge to get _SUNs if any behind P2P */
	status = acpi_walk_namespace ( ACPI_TYPE_DEVICE,
				ab->handle,
				depth,
				pciehprm_acpi_build_php_slots_callback,
				ab,
				NULL );
	if (ACPI_FAILURE(status)) {
		dbg("acpi_pciehprm:%s walk for _SUN on pci bridge seg:bus(%x:%x) fail=0x%x\n", path_name, ab->seg, ab->bus, status);
		return -1;
	}

	return 0;
}

static void build_a_bridge(
	struct acpi_bridge	*pab,
	struct acpi_bridge	*ab
	)
{
	u8		*path_name = acpi_path_name(ab->handle);

	pciehprm_acpi_register_a_bridge (&acpi_bridges_head, pab, ab);

	switch (ab->type) {
	case BRIDGE_TYPE_HOST:
		dbg("acpi_pciehprm: Registered PCI HOST Bridge(%02x)    on s:b:d:f(%02x:%02x:%02x:%02x) [%s]\n",
			ab->bus, ab->seg, ab->pbus, ab->pdevice, ab->pfunction, path_name);
		break;
	case BRIDGE_TYPE_P2P:
		dbg("acpi_pciehprm: Registered PCI  P2P Bridge(%02x-%02x) on s:b:d:f(%02x:%02x:%02x:%02x) [%s]\n",
			ab->pbus, ab->bus, ab->seg, ab->pbus, ab->pdevice, ab->pfunction, path_name);
		break;
	};

	/* build any immediate PHP slots under this pci bridge */
	pciehprm_acpi_build_php_slots(ab, 1);
}

static struct acpi_bridge * add_p2p_bridge(
	acpi_handle handle,
	struct acpi_bridge	*pab,	/* parent */
	ulong	adr
	)
{
	struct acpi_bridge	*ab;
	struct pci_dev	*pdev;
	ulong		devnum, funcnum;
	u8			*path_name = acpi_path_name(handle);

	ab = (struct acpi_bridge *) kmalloc (sizeof(struct acpi_bridge), GFP_KERNEL);
	if (!ab) {
		err("acpi_pciehprm: alloc for ab fail\n");
		return NULL;
	}
	memset(ab, 0, sizeof(struct acpi_bridge));

	devnum = (adr >> 16) & 0xffff;
	funcnum = adr & 0xffff;

	pdev = pci_find_slot(pab->bus, PCI_DEVFN(devnum, funcnum));
	if (!pdev || !pdev->subordinate) {
		err("acpi_pciehprm:%s is not a P2P Bridge\n", path_name);
		kfree(ab);
		return NULL;
	}

	ab->handle = handle;
	ab->seg = pab->seg;
	ab->pbus = pab->bus;		/* or pdev->bus->number */
	ab->pdevice = devnum;		/* or PCI_SLOT(pdev->devfn) */
	ab->pfunction = funcnum;	/* or PCI_FUNC(pdev->devfn) */
	ab->bus = pdev->subordinate->number;
	ab->scanned = 0;
	ab->type = BRIDGE_TYPE_P2P;

	dbg("acpi_pciehprm: P2P(%x-%x) on pci=b:d:f(%x:%x:%x) acpi=b:d:f(%x:%x:%x) [%s]\n",
		pab->bus, ab->bus, pdev->bus->number, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
		pab->bus, (u32)devnum, (u32)funcnum, path_name);

	build_a_bridge(pab, ab);

	return ab;
}

static acpi_status scan_p2p_bridge(
	acpi_handle		handle,
	u32			Level,
	void			*context,
	void			**retval
	)
{
	struct acpi_bridge	*pab = (struct acpi_bridge *)context;
	struct acpi_bridge	*ab;
	acpi_status		status;
	ulong			adr = 0;
	u8			*path_name = acpi_path_name(handle);
	ulong			devnum, funcnum;
	struct pci_dev		*pdev;

	/* get device, function */
	status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL, &adr);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND)
			err("acpi_pciehprm:%s _ADR fail=0x%x\n", path_name, status);
		return AE_OK;
	}

	devnum = (adr >> 16) & 0xffff;
	funcnum = adr & 0xffff;

	pdev = pci_find_slot(pab->bus, PCI_DEVFN(devnum, funcnum));
	if (!pdev)
		return AE_OK;
	if (!pdev->subordinate)
		return AE_OK;

	ab = add_p2p_bridge(handle, pab, adr);
	if (ab) {
		status = acpi_walk_namespace ( ACPI_TYPE_DEVICE,
					handle,
					(u32)1,
					scan_p2p_bridge,
					ab,
					NULL);
		if (ACPI_FAILURE(status))
			dbg("acpi_pciehprm:%s find_p2p fail=0x%x\n", path_name, status);
	}

	return AE_OK;
}

static struct acpi_bridge * add_host_bridge(
	acpi_handle handle,
	ulong	segnum,
	ulong	busnum
	)
{
	ulong			adr = 0;
	acpi_status		status;
	struct acpi_bridge	*ab;
	u8			*path_name = acpi_path_name(handle);

	/* get device, function: host br adr is always 0000 though.  */
	status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL, &adr);
	if (ACPI_FAILURE(status)) {
		err("acpi_pciehprm:%s _ADR fail=0x%x\n", path_name, status);
		return NULL;
	}
	dbg("acpi_pciehprm: ROOT PCI seg(0x%x)bus(0x%x)dev(0x%x)func(0x%x) [%s]\n", (u32)segnum, 
		(u32)busnum, (u32)(adr >> 16) & 0xffff, (u32)adr & 0xffff, path_name);

	ab = (struct acpi_bridge *) kmalloc (sizeof(struct acpi_bridge), GFP_KERNEL);
	if (!ab) {
		err("acpi_pciehprm: alloc for ab fail\n");
		return NULL;
	}
	memset(ab, 0, sizeof(struct acpi_bridge));

	ab->handle = handle;
	ab->seg = (int)segnum;
	ab->bus = ab->pbus = (int)busnum;
	ab->pdevice = (int)(adr >> 16) & 0xffff;
	ab->pfunction = (int)(adr & 0xffff);
	ab->scanned = 0;
	ab->type = BRIDGE_TYPE_HOST;

	/* get root pci bridge's current resources */
	status = acpi_get_crs(ab);
	if (ACPI_FAILURE(status)) {
		err("acpi_pciehprm:%s evaluate _CRS fail=0x%x\n", path_name, status);
		kfree(ab);
		return NULL;
	}

	status = pci_osc_control_set (OSC_PCI_EXPRESS_NATIVE_HP_CONTROL); 
	if (ACPI_FAILURE(status)) {
		err("%s: status %x\n", __FUNCTION__, status);
		osc_run_status = (status == AE_NOT_FOUND) ? OSC_NOT_EXIST : OSC_RUN_FAILED;
	} else {
		osc_run_status = NC_RUN_SUCCESS;
	}	
	dbg("%s: osc_run_status %x\n", __FUNCTION__, osc_run_status);
	
	build_a_bridge(ab, ab);

	return ab;
}

static acpi_status acpi_scan_from_root_pci_callback (
	acpi_handle	handle,
	u32			Level,
	void		*context,
	void		**retval
	)
{
	ulong		segnum = 0;
	ulong		busnum = 0;
	acpi_status		status;
	struct acpi_bridge	*ab;
	u8			*path_name = acpi_path_name(handle);

	/* get bus number of this pci root bridge */
	status = acpi_evaluate_integer(handle, METHOD_NAME__SEG, NULL, &segnum);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND) {
			err("acpi_pciehprm:%s evaluate _SEG fail=0x%x\n", path_name, status);
			return status;
		}
		segnum = 0;
	}

	/* get bus number of this pci root bridge */
	status = acpi_evaluate_integer(handle, METHOD_NAME__BBN, NULL, &busnum);
	if (ACPI_FAILURE(status)) {
		err("acpi_pciehprm:%s evaluate _BBN fail=0x%x\n", path_name, status);
		return (status);
	}

	ab = add_host_bridge(handle, segnum, busnum);
	if (ab) {
		status = acpi_walk_namespace ( ACPI_TYPE_DEVICE,
					handle,
					1,
					scan_p2p_bridge,
					ab,
					NULL);
		if (ACPI_FAILURE(status))
			dbg("acpi_pciehprm:%s find_p2p fail=0x%x\n", path_name, status);
	}

	return AE_OK;
}

static int pciehprm_acpi_scan_pci (void)
{
	acpi_status	status;

	/*
	 * TBD: traverse LDM device tree with the help of
	 *  unified ACPI augmented for php device population.
	 */
	status = acpi_get_devices ( PCI_ROOT_HID_STRING,
				acpi_scan_from_root_pci_callback,
				NULL,
				NULL );
	if (ACPI_FAILURE(status)) {
		err("acpi_pciehprm:get_device PCI ROOT HID fail=0x%x\n", status);
		return -1;
	}

	return 0;
}

int pciehprm_init(enum php_ctlr_type ctlr_type)
{
	int	rc;

	if (ctlr_type != PCI)
		return -ENODEV;

	dbg("pciehprm ACPI init <enter>\n");
	acpi_bridges_head = NULL;

	/* construct PCI bus:device tree of acpi_handles */
	rc = pciehprm_acpi_scan_pci();
	if (rc)
		return rc;

	if ((oshp_run_status != NC_RUN_SUCCESS) && (osc_run_status != NC_RUN_SUCCESS)) {
		err("Fails to gain control of native hot-plug\n");
		rc = -ENODEV;
	}

	dbg("pciehprm ACPI init %s\n", (rc)?"fail":"success");
	return rc;
}

static void free_a_slot(struct acpi_php_slot *aps)
{
	dbg("        free a php func of slot(0x%02x) on PCI b:d:f=0x%02x:%02x:%02x\n", aps->sun, aps->bus, aps->dev, aps->fun);

	free_pci_resource (aps->io_head);
	free_pci_resource (aps->bus_head);
	free_pci_resource (aps->mem_head);
	free_pci_resource (aps->p_mem_head);

	kfree(aps);
}

static void free_a_bridge( struct acpi_bridge	*ab)
{
	struct acpi_php_slot	*aps, *next;

	switch (ab->type) {
	case BRIDGE_TYPE_HOST:
		dbg("Free ACPI PCI HOST Bridge(%x) [%s] on s:b:d:f(%x:%x:%x:%x)\n",
			ab->bus, acpi_path_name(ab->handle), ab->seg, ab->pbus, ab->pdevice, ab->pfunction);
		break;
	case BRIDGE_TYPE_P2P:
		dbg("Free ACPI PCI P2P Bridge(%x-%x) [%s] on s:b:d:f(%x:%x:%x:%x)\n",
			ab->pbus, ab->bus, acpi_path_name(ab->handle), ab->seg, ab->pbus, ab->pdevice, ab->pfunction);
		break;
	};

	/* free slots first */
	for (aps = ab->slots; aps; aps = next) {
		next = aps->next;
		free_a_slot(aps);
	}

	free_pci_resource (ab->io_head);
	free_pci_resource (ab->tio_head);
	free_pci_resource (ab->bus_head);
	free_pci_resource (ab->tbus_head);
	free_pci_resource (ab->mem_head);
	free_pci_resource (ab->tmem_head);
	free_pci_resource (ab->p_mem_head);
	free_pci_resource (ab->tp_mem_head);

	kfree(ab);
}

static void pciehprm_free_bridges ( struct acpi_bridge	*ab)
{
	if (!ab)
		return;

	if (ab->child)
		pciehprm_free_bridges (ab->child);

	if (ab->next)
		pciehprm_free_bridges (ab->next);

	free_a_bridge(ab);
}

void pciehprm_cleanup(void)
{
	pciehprm_free_bridges (acpi_bridges_head);
}

static int get_number_of_slots (
	struct acpi_bridge	*ab,
	int				selfonly
	)
{
	struct acpi_php_slot	*aps;
	int	prev_slot = -1;
	int	slot_num = 0;

	for ( aps = ab->slots; aps; aps = aps->next)
		if (aps->dev != prev_slot) {
			prev_slot = aps->dev;
			slot_num++;
		}

	if (ab->child)
		slot_num += get_number_of_slots (ab->child, 0);

	if (selfonly)
		return slot_num;

	if (ab->next)
		slot_num += get_number_of_slots (ab->next, 0);

	return slot_num;
}

static int print_acpi_resources (struct acpi_bridge	*ab)
{
	struct acpi_php_slot		*aps;
	int	i;

	switch (ab->type) {
	case BRIDGE_TYPE_HOST:
		dbg("PCI HOST Bridge (%x) [%s]\n", ab->bus, acpi_path_name(ab->handle));
		break;
	case BRIDGE_TYPE_P2P:
		dbg("PCI P2P Bridge (%x-%x) [%s]\n", ab->pbus, ab->bus, acpi_path_name(ab->handle));
		break;
	};

	print_pci_resources (ab);

	for ( i = -1, aps = ab->slots; aps; aps = aps->next) {
		if (aps->dev == i)
			continue;
		dbg("  Slot sun(%x) s:b:d:f(%02x:%02x:%02x:%02x)\n", aps->sun, aps->seg, aps->bus, aps->dev, aps->fun);
		print_slot_resources(aps);
		i = aps->dev;
	}

	if (ab->child)
		print_acpi_resources (ab->child);

	if (ab->next)
		print_acpi_resources (ab->next);

	return 0;
}

int pciehprm_print_pirt(void)
{
	dbg("PCIEHPRM ACPI Slots\n");
	if (acpi_bridges_head)
		print_acpi_resources (acpi_bridges_head);

	return 0;
}

static struct acpi_php_slot * get_acpi_slot (
	struct acpi_bridge *ab,
	u32 sun
	)
{
	struct acpi_php_slot	*aps = NULL;

	for ( aps = ab->slots; aps; aps = aps->next)
		if (aps->sun == sun)
			return aps;

	if (!aps && ab->child) {
		aps = (struct acpi_php_slot *)get_acpi_slot (ab->child, sun);
		if (aps)
			return aps;
	}

	if (!aps && ab->next) {
		aps = (struct acpi_php_slot *)get_acpi_slot (ab->next, sun);
		if (aps)
			return aps;
	}

	return aps;

}

#if 0
void * pciehprm_get_slot(struct slot *slot)
{
	struct acpi_bridge	*ab = acpi_bridges_head;
	struct acpi_php_slot	*aps = get_acpi_slot (ab, slot->number);

	aps->slot = slot;

	dbg("Got acpi slot sun(%x): s:b:d:f(%x:%x:%x:%x)\n", aps->sun, aps->seg, aps->bus, aps->dev, aps->fun);

	return (void *)aps;
}
#endif

static void pciehprm_dump_func_res( struct pci_func *fun)
{
	struct pci_func *func = fun;

	if (func->bus_head) {
		dbg(":    BUS Resources:\n");
		print_pci_resource (func->bus_head);
	}
	if (func->io_head) {
		dbg(":    IO Resources:\n");
		print_pci_resource (func->io_head);
	}
	if (func->mem_head) {
		dbg(":    MEM Resources:\n");
		print_pci_resource (func->mem_head);
	}
	if (func->p_mem_head) {
		dbg(":    PMEM Resources:\n");
		print_pci_resource (func->p_mem_head);
	}
}

static void pciehprm_dump_ctrl_res( struct controller *ctlr)
{
	struct controller *ctrl = ctlr;

	if (ctrl->bus_head) {
		dbg(":    BUS Resources:\n");
		print_pci_resource (ctrl->bus_head);
	}
	if (ctrl->io_head) {
		dbg(":    IO Resources:\n");
		print_pci_resource (ctrl->io_head);
	}
	if (ctrl->mem_head) {
		dbg(":    MEM Resources:\n");
		print_pci_resource (ctrl->mem_head);
	}
	if (ctrl->p_mem_head) {
		dbg(":    PMEM Resources:\n");
		print_pci_resource (ctrl->p_mem_head);
	}
}

static int pciehprm_get_used_resources (
	struct controller *ctrl,
	struct pci_func *func
	)
{
	return pciehp_save_used_resources (ctrl, func, !DISABLE_CARD);
}

static int configure_existing_function(
	struct controller *ctrl,
	struct pci_func *func
	)
{
	int rc;

	/* see how much resources the func has used. */
	rc = pciehprm_get_used_resources (ctrl, func);

	if (!rc) {
		/* subtract the resources used by the func from ctrl resources */
		rc  = pciehprm_delete_resources (&ctrl->bus_head, func->bus_head);
		rc |= pciehprm_delete_resources (&ctrl->io_head, func->io_head);
		rc |= pciehprm_delete_resources (&ctrl->mem_head, func->mem_head);
		rc |= pciehprm_delete_resources (&ctrl->p_mem_head, func->p_mem_head);
		if (rc)
			warn("aCEF: cannot del used resources\n");
	} else
		err("aCEF: cannot get used resources\n");

	return rc;
}

static int bind_pci_resources_to_slots ( struct controller *ctrl)
{
	struct pci_func *func, new_func;
	int busn = ctrl->slot_bus;
	int devn, funn;
	u32	vid;

	for (devn = 0; devn < 32; devn++) {
		for (funn = 0; funn < 8; funn++) {
			/*
			if (devn == ctrl->device && funn == ctrl->function)
				continue;
			*/
			/* find out if this entry is for an occupied slot */
			vid = 0xFFFFFFFF;
			pci_bus_read_config_dword(ctrl->pci_dev->subordinate, PCI_DEVFN(devn, funn), PCI_VENDOR_ID, &vid);

			if (vid != 0xFFFFFFFF) {
				dbg("%s: vid = %x\n", __FUNCTION__, vid);
				func = pciehp_slot_find(busn, devn, funn);
				if (!func) {
					memset(&new_func, 0, sizeof(struct pci_func));
					new_func.bus = busn;
					new_func.device = devn;
					new_func.function = funn;
					new_func.is_a_board = 1;
					configure_existing_function(ctrl, &new_func);
					pciehprm_dump_func_res(&new_func);
				} else {
					configure_existing_function(ctrl, func);
					pciehprm_dump_func_res(func);
				}
				dbg("aCCF:existing PCI 0x%x Func ResourceDump\n", ctrl->bus);
			}
		}
	}

	return 0;
}

static int bind_pci_resources(
	struct controller 	*ctrl,
	struct acpi_bridge	*ab
	)
{
	int		status = 0;

	if (ab->bus_head) {
		dbg("bapr:  BUS Resources add on PCI 0x%x\n", ab->bus);
		status = pciehprm_add_resources (&ctrl->bus_head, ab->bus_head);
		if (pciehprm_delete_resources (&ab->bus_head, ctrl->bus_head))
			warn("bapr:  cannot sub BUS Resource on PCI 0x%x\n", ab->bus);
		if (status) {
			err("bapr:  BUS Resource add on PCI 0x%x: fail=0x%x\n", ab->bus, status);
			return status;
		}
	} else
		info("bapr:  No BUS Resource on PCI 0x%x.\n", ab->bus);

	if (ab->io_head) {
		dbg("bapr:  IO Resources add on PCI 0x%x\n", ab->bus);
		status = pciehprm_add_resources (&ctrl->io_head, ab->io_head);
		if (pciehprm_delete_resources (&ab->io_head, ctrl->io_head))
			warn("bapr:  cannot sub IO Resource on PCI 0x%x\n", ab->bus);
		if (status) {
			err("bapr:  IO Resource add on PCI 0x%x: fail=0x%x\n", ab->bus, status);
			return status;
		}
	} else
		info("bapr:  No  IO Resource on PCI 0x%x.\n", ab->bus);

	if (ab->mem_head) {
		dbg("bapr:  MEM Resources add on PCI 0x%x\n", ab->bus);
		status = pciehprm_add_resources (&ctrl->mem_head, ab->mem_head);
		if (pciehprm_delete_resources (&ab->mem_head, ctrl->mem_head))
			warn("bapr:  cannot sub MEM Resource on PCI 0x%x\n", ab->bus);
		if (status) {
			err("bapr:  MEM Resource add on PCI 0x%x: fail=0x%x\n", ab->bus, status);
			return status;
		}
	} else
		info("bapr:  No MEM Resource on PCI 0x%x.\n", ab->bus);

	if (ab->p_mem_head) {
		dbg("bapr:  PMEM Resources add on PCI 0x%x\n", ab->bus);
		status = pciehprm_add_resources (&ctrl->p_mem_head, ab->p_mem_head);
		if (pciehprm_delete_resources (&ab->p_mem_head, ctrl->p_mem_head))
			warn("bapr:  cannot sub PMEM Resource on PCI 0x%x\n", ab->bus);
		if (status) {
			err("bapr:  PMEM Resource add on PCI 0x%x: fail=0x%x\n", ab->bus, status);
			return status;
		}
	} else
		info("bapr:  No PMEM Resource on PCI 0x%x.\n", ab->bus);

	return status;
}

static int no_pci_resources( struct acpi_bridge *ab)
{
	return !(ab->p_mem_head || ab->mem_head || ab->io_head || ab->bus_head);
}

static int find_pci_bridge_resources (
	struct controller *ctrl,
	struct acpi_bridge *ab
	)
{
	int	rc = 0;
	struct pci_func func;

	memset(&func, 0, sizeof(struct pci_func));

	func.bus = ab->pbus;
	func.device = ab->pdevice;
	func.function = ab->pfunction;
	func.is_a_board = 1;

	/* Get used resources for this PCI bridge */
	rc = pciehp_save_used_resources (ctrl, &func, !DISABLE_CARD);

	ab->io_head = func.io_head;
	ab->mem_head = func.mem_head;
	ab->p_mem_head = func.p_mem_head;
	ab->bus_head = func.bus_head;
	if (ab->bus_head)
		pciehprm_delete_resource(&ab->bus_head, ctrl->pci_dev->subordinate->number, 1);

	return rc;
}

static int get_pci_resources_from_bridge(
	struct controller *ctrl,
	struct acpi_bridge *ab
	)
{
	int	rc = 0;

	dbg("grfb:  Get Resources for PCI 0x%x from actual PCI bridge 0x%x.\n", ctrl->bus, ab->bus);

	rc = find_pci_bridge_resources (ctrl, ab);

	pciehp_resource_sort_and_combine(&ab->bus_head);
	pciehp_resource_sort_and_combine(&ab->io_head);
	pciehp_resource_sort_and_combine(&ab->mem_head);
	pciehp_resource_sort_and_combine(&ab->p_mem_head);

	pciehprm_add_resources (&ab->tbus_head, ab->bus_head);
	pciehprm_add_resources (&ab->tio_head, ab->io_head);
	pciehprm_add_resources (&ab->tmem_head, ab->mem_head);
	pciehprm_add_resources (&ab->tp_mem_head, ab->p_mem_head);

	return rc;
}

static int get_pci_resources(
	struct controller	*ctrl,
	struct acpi_bridge	*ab
	)
{
	int	rc = 0;

	if (no_pci_resources(ab)) {
		dbg("spbr:PCI 0x%x has no resources. Get parent resources.\n", ab->bus);
		rc = get_pci_resources_from_bridge(ctrl, ab);
	}

	return rc;
}

/*
 * Get resources for this ctrl.
 *  1. get total resources from ACPI _CRS or bridge (this ctrl)
 *  2. find used resources of existing adapters
 *	3. subtract used resources from total resources
 */
int pciehprm_find_available_resources( struct controller *ctrl)
{
	int rc = 0;
	struct acpi_bridge	*ab;

	ab = find_acpi_bridge_by_bus(acpi_bridges_head, ctrl->seg, ctrl->pci_dev->subordinate->number);
	if (!ab) {
		err("pfar:cannot locate acpi bridge of PCI 0x%x.\n", ctrl->pci_dev->subordinate->number);
		return -1;
	}
	if (no_pci_resources(ab)) {
		rc = get_pci_resources(ctrl, ab);
		if (rc) {
			err("pfar:cannot get pci resources of PCI 0x%x.\n", ctrl->pci_dev->subordinate->number);
			return -1;
		}
	}

	rc = bind_pci_resources(ctrl, ab);
	dbg("pfar:pre-Bind PCI 0x%x Ctrl Resource Dump\n", ctrl->pci_dev->subordinate->number);
	pciehprm_dump_ctrl_res(ctrl);

	bind_pci_resources_to_slots (ctrl);

	dbg("pfar:post-Bind PCI 0x%x Ctrl Resource Dump\n", ctrl->pci_dev->subordinate->number);
	pciehprm_dump_ctrl_res(ctrl);

	return rc;
}

int pciehprm_set_hpp(
	struct controller *ctrl,
	struct pci_func *func,
	u8	card_type
	)
{
	struct acpi_bridge	*ab;
	struct pci_bus lpci_bus, *pci_bus;
	int				rc = 0;
	unsigned int	devfn;
	u8				cls= 0x08;	/* default cache line size	*/
	u8				lt = 0x40;	/* default latency timer	*/
	u8				ep = 0;
	u8				es = 0;

	memcpy(&lpci_bus, ctrl->pci_bus, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	pci_bus->number = func->bus;
	devfn = PCI_DEVFN(func->device, func->function);

	ab = find_acpi_bridge_by_bus(acpi_bridges_head, ctrl->seg, ctrl->bus);

	if (ab) {
		if (ab->_hpp) {
			lt  = (u8)ab->_hpp->latency_timer;
			cls = (u8)ab->_hpp->cache_line_size;
			ep  = (u8)ab->_hpp->enable_perr;
			es  = (u8)ab->_hpp->enable_serr;
		} else
			dbg("_hpp: no _hpp for B/D/F=%#x/%#x/%#x. use default value\n", func->bus, func->device, func->function);
	} else
		dbg("_hpp: no acpi bridge for B/D/F = %#x/%#x/%#x. use default value\n", func->bus, func->device, func->function);


	if (card_type == PCI_HEADER_TYPE_BRIDGE) {
		/* set subordinate Latency Timer */
		rc |= pci_bus_write_config_byte(pci_bus, devfn, PCI_SEC_LATENCY_TIMER, lt);
	}

	/* set base Latency Timer */
	rc |= pci_bus_write_config_byte(pci_bus, devfn, PCI_LATENCY_TIMER, lt);
	dbg("  set latency timer  =0x%02x: %x\n", lt, rc);

	rc |= pci_bus_write_config_byte(pci_bus, devfn, PCI_CACHE_LINE_SIZE, cls);
	dbg("  set cache_line_size=0x%02x: %x\n", cls, rc);

	return rc;
}

void pciehprm_enable_card(
	struct controller *ctrl,
	struct pci_func *func,
	u8 card_type)
{
	u16 command, cmd, bcommand, bcmd;
	struct pci_bus lpci_bus, *pci_bus;
	struct acpi_bridge	*ab;
	unsigned int devfn;
	int rc;

	memcpy(&lpci_bus, ctrl->pci_bus, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	pci_bus->number = func->bus;
	devfn = PCI_DEVFN(func->device, func->function);

	rc = pci_bus_read_config_word(pci_bus, devfn, PCI_COMMAND, &cmd);

	if (card_type == PCI_HEADER_TYPE_BRIDGE) {
		rc = pci_bus_read_config_word(pci_bus, devfn, PCI_BRIDGE_CONTROL, &bcmd);
	}

	command  = cmd | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE
		| PCI_COMMAND_IO | PCI_COMMAND_MEMORY;
	bcommand  = bcmd | PCI_BRIDGE_CTL_NO_ISA;

	ab = find_acpi_bridge_by_bus(acpi_bridges_head, ctrl->seg, ctrl->bus);
	if (ab) {
		if (ab->_hpp) {
			if (ab->_hpp->enable_perr) {
				command |= PCI_COMMAND_PARITY;
				bcommand |= PCI_BRIDGE_CTL_PARITY;
			} else {
				command &= ~PCI_COMMAND_PARITY;
				bcommand &= ~PCI_BRIDGE_CTL_PARITY;
			}
			if (ab->_hpp->enable_serr) {
				command |= PCI_COMMAND_SERR;
				bcommand |= PCI_BRIDGE_CTL_SERR;
			} else {
				command &= ~PCI_COMMAND_SERR;
				bcommand &= ~PCI_BRIDGE_CTL_SERR;
			}
		} else
			dbg("no _hpp for B/D/F = %#x/%#x/%#x.\n", func->bus, func->device, func->function);
	} else
		dbg("no acpi bridge for B/D/F = %#x/%#x/%#x.\n", func->bus, func->device, func->function);

	if (command != cmd) {
		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_COMMAND, command);
	}
	if ((card_type == PCI_HEADER_TYPE_BRIDGE) && (bcommand != bcmd)) {
		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_BRIDGE_CONTROL, bcommand);
	}
}
