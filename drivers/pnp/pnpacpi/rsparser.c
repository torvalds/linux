/*
 * pnpacpi -- PnP ACPI driver
 *
 * Copyright (c) 2004 Matthieu Castet <castet.matthieu@free.fr>
 * Copyright (c) 2004 Li Shaohua <shaohua.li@intel.com>
 * Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/pnp.h>
#include "../base.h"
#include "pnpacpi.h"

#ifdef CONFIG_IA64
#define valid_IRQ(i) (1)
#else
#define valid_IRQ(i) (((i) != 0) && ((i) != 2))
#endif

/*
 * Allocated Resources
 */
static int irq_flags(int triggering, int polarity, int shareable)
{
	int flags;

	if (triggering == ACPI_LEVEL_SENSITIVE) {
		if (polarity == ACPI_ACTIVE_LOW)
			flags = IORESOURCE_IRQ_LOWLEVEL;
		else
			flags = IORESOURCE_IRQ_HIGHLEVEL;
	} else {
		if (polarity == ACPI_ACTIVE_LOW)
			flags = IORESOURCE_IRQ_LOWEDGE;
		else
			flags = IORESOURCE_IRQ_HIGHEDGE;
	}

	if (shareable == ACPI_SHARED)
		flags |= IORESOURCE_IRQ_SHAREABLE;

	return flags;
}

static void decode_irq_flags(struct pnp_dev *dev, int flags, int *triggering,
			     int *polarity, int *shareable)
{
	switch (flags & (IORESOURCE_IRQ_LOWLEVEL | IORESOURCE_IRQ_HIGHLEVEL |
			 IORESOURCE_IRQ_LOWEDGE  | IORESOURCE_IRQ_HIGHEDGE)) {
	case IORESOURCE_IRQ_LOWLEVEL:
		*triggering = ACPI_LEVEL_SENSITIVE;
		*polarity = ACPI_ACTIVE_LOW;
		break;
	case IORESOURCE_IRQ_HIGHLEVEL:
		*triggering = ACPI_LEVEL_SENSITIVE;
		*polarity = ACPI_ACTIVE_HIGH;
		break;
	case IORESOURCE_IRQ_LOWEDGE:
		*triggering = ACPI_EDGE_SENSITIVE;
		*polarity = ACPI_ACTIVE_LOW;
		break;
	case IORESOURCE_IRQ_HIGHEDGE:
		*triggering = ACPI_EDGE_SENSITIVE;
		*polarity = ACPI_ACTIVE_HIGH;
		break;
	default:
		dev_err(&dev->dev, "can't encode invalid IRQ mode %#x\n",
			flags);
		*triggering = ACPI_EDGE_SENSITIVE;
		*polarity = ACPI_ACTIVE_HIGH;
		break;
	}

	if (flags & IORESOURCE_IRQ_SHAREABLE)
		*shareable = ACPI_SHARED;
	else
		*shareable = ACPI_EXCLUSIVE;
}

static void pnpacpi_parse_allocated_irqresource(struct pnp_dev *dev,
						u32 gsi, int triggering,
						int polarity, int shareable)
{
	int irq, flags;
	int p, t;

	if (!valid_IRQ(gsi)) {
		pnp_add_irq_resource(dev, gsi, IORESOURCE_DISABLED);
		return;
	}

	/*
	 * in IO-APIC mode, use overrided attribute. Two reasons:
	 * 1. BIOS bug in DSDT
	 * 2. BIOS uses IO-APIC mode Interrupt Source Override
	 */
	if (!acpi_get_override_irq(gsi, &t, &p)) {
		t = t ? ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE;
		p = p ? ACPI_ACTIVE_LOW : ACPI_ACTIVE_HIGH;

		if (triggering != t || polarity != p) {
			dev_warn(&dev->dev, "IRQ %d override to %s, %s\n",
				gsi, t ? "edge":"level", p ? "low":"high");
			triggering = t;
			polarity = p;
		}
	}

	flags = irq_flags(triggering, polarity, shareable);
	irq = acpi_register_gsi(gsi, triggering, polarity);
	if (irq >= 0)
		pcibios_penalize_isa_irq(irq, 1);
	else
		flags |= IORESOURCE_DISABLED;

	pnp_add_irq_resource(dev, irq, flags);
}

static int dma_flags(struct pnp_dev *dev, int type, int bus_master,
		     int transfer)
{
	int flags = 0;

	if (bus_master)
		flags |= IORESOURCE_DMA_MASTER;
	switch (type) {
	case ACPI_COMPATIBILITY:
		flags |= IORESOURCE_DMA_COMPATIBLE;
		break;
	case ACPI_TYPE_A:
		flags |= IORESOURCE_DMA_TYPEA;
		break;
	case ACPI_TYPE_B:
		flags |= IORESOURCE_DMA_TYPEB;
		break;
	case ACPI_TYPE_F:
		flags |= IORESOURCE_DMA_TYPEF;
		break;
	default:
		/* Set a default value ? */
		flags |= IORESOURCE_DMA_COMPATIBLE;
		dev_err(&dev->dev, "invalid DMA type %d\n", type);
	}
	switch (transfer) {
	case ACPI_TRANSFER_8:
		flags |= IORESOURCE_DMA_8BIT;
		break;
	case ACPI_TRANSFER_8_16:
		flags |= IORESOURCE_DMA_8AND16BIT;
		break;
	case ACPI_TRANSFER_16:
		flags |= IORESOURCE_DMA_16BIT;
		break;
	default:
		/* Set a default value ? */
		flags |= IORESOURCE_DMA_8AND16BIT;
		dev_err(&dev->dev, "invalid DMA transfer type %d\n", transfer);
	}

	return flags;
}

static void pnpacpi_parse_allocated_ioresource(struct pnp_dev *dev, u64 start,
					       u64 len, int io_decode)
{
	int flags = 0;
	u64 end = start + len - 1;

	if (io_decode == ACPI_DECODE_16)
		flags |= IORESOURCE_IO_16BIT_ADDR;
	if (len == 0 || end >= 0x10003)
		flags |= IORESOURCE_DISABLED;

	pnp_add_io_resource(dev, start, end, flags);
}

/*
 * Device CSRs that do not appear in PCI config space should be described
 * via ACPI.  This would normally be done with Address Space Descriptors
 * marked as "consumer-only," but old versions of Windows and Linux ignore
 * the producer/consumer flag, so HP invented a vendor-defined resource to
 * describe the location and size of CSR space.
 */
static struct acpi_vendor_uuid hp_ccsr_uuid = {
	.subtype = 2,
	.data = { 0xf9, 0xad, 0xe9, 0x69, 0x4f, 0x92, 0x5f, 0xab, 0xf6, 0x4a,
	    0x24, 0xd2, 0x01, 0x37, 0x0e, 0xad },
};

static int vendor_resource_matches(struct pnp_dev *dev,
				   struct acpi_resource_vendor_typed *vendor,
				   struct acpi_vendor_uuid *match,
				   int expected_len)
{
	int uuid_len = sizeof(vendor->uuid);
	u8 uuid_subtype = vendor->uuid_subtype;
	u8 *uuid = vendor->uuid;
	int actual_len;

	/* byte_length includes uuid_subtype and uuid */
	actual_len = vendor->byte_length - uuid_len - 1;

	if (uuid_subtype == match->subtype &&
	    uuid_len == sizeof(match->data) &&
	    memcmp(uuid, match->data, uuid_len) == 0) {
		if (expected_len && expected_len != actual_len) {
			dev_err(&dev->dev, "wrong vendor descriptor size; "
				"expected %d, found %d bytes\n",
				expected_len, actual_len);
			return 0;
		}

		return 1;
	}

	return 0;
}

static void pnpacpi_parse_allocated_vendor(struct pnp_dev *dev,
				    struct acpi_resource_vendor_typed *vendor)
{
	if (vendor_resource_matches(dev, vendor, &hp_ccsr_uuid, 16)) {
		u64 start, length;

		memcpy(&start, vendor->byte_data, sizeof(start));
		memcpy(&length, vendor->byte_data + 8, sizeof(length));

		pnp_add_mem_resource(dev, start, start + length - 1, 0);
	}
}

static void pnpacpi_parse_allocated_memresource(struct pnp_dev *dev,
						u64 start, u64 len,
						int write_protect)
{
	int flags = 0;
	u64 end = start + len - 1;

	if (len == 0)
		flags |= IORESOURCE_DISABLED;
	if (write_protect == ACPI_READ_WRITE_MEMORY)
		flags |= IORESOURCE_MEM_WRITEABLE;

	pnp_add_mem_resource(dev, start, end, flags);
}

static void pnpacpi_parse_allocated_address_space(struct pnp_dev *dev,
						  struct acpi_resource *res)
{
	struct acpi_resource_address64 addr, *p = &addr;
	acpi_status status;

	status = acpi_resource_to_address64(res, p);
	if (!ACPI_SUCCESS(status)) {
		dev_warn(&dev->dev, "failed to convert resource type %d\n",
			 res->type);
		return;
	}

	if (p->producer_consumer == ACPI_PRODUCER)
		return;

	if (p->resource_type == ACPI_MEMORY_RANGE)
		pnpacpi_parse_allocated_memresource(dev,
			p->minimum, p->address_length,
			p->info.mem.write_protect);
	else if (p->resource_type == ACPI_IO_RANGE)
		pnpacpi_parse_allocated_ioresource(dev,
			p->minimum, p->address_length,
			p->granularity == 0xfff ? ACPI_DECODE_10 :
				ACPI_DECODE_16);
}

static acpi_status pnpacpi_allocated_resource(struct acpi_resource *res,
					      void *data)
{
	struct pnp_dev *dev = data;
	struct acpi_resource_irq *irq;
	struct acpi_resource_dma *dma;
	struct acpi_resource_io *io;
	struct acpi_resource_fixed_io *fixed_io;
	struct acpi_resource_vendor_typed *vendor_typed;
	struct acpi_resource_memory24 *memory24;
	struct acpi_resource_memory32 *memory32;
	struct acpi_resource_fixed_memory32 *fixed_memory32;
	struct acpi_resource_extended_irq *extended_irq;
	int i, flags;

	switch (res->type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		/*
		 * Per spec, only one interrupt per descriptor is allowed in
		 * _CRS, but some firmware violates this, so parse them all.
		 */
		irq = &res->data.irq;
		if (irq->interrupt_count == 0)
			pnp_add_irq_resource(dev, 0, IORESOURCE_DISABLED);
		else {
			for (i = 0; i < irq->interrupt_count; i++) {
				pnpacpi_parse_allocated_irqresource(dev,
					irq->interrupts[i],
					irq->triggering,
					irq->polarity,
				    irq->sharable);
			}

			/*
			 * The IRQ encoder puts a single interrupt in each
			 * descriptor, so if a _CRS descriptor has more than
			 * one interrupt, we won't be able to re-encode it.
			 */
			if (pnp_can_write(dev) && irq->interrupt_count > 1) {
				dev_warn(&dev->dev, "multiple interrupts in "
					 "_CRS descriptor; configuration can't "
					 "be changed\n");
				dev->capabilities &= ~PNP_WRITE;
			}
		}
		break;

	case ACPI_RESOURCE_TYPE_DMA:
		dma = &res->data.dma;
		if (dma->channel_count > 0 && dma->channels[0] != (u8) -1)
			flags = dma_flags(dev, dma->type, dma->bus_master,
					  dma->transfer);
		else
			flags = IORESOURCE_DISABLED;
		pnp_add_dma_resource(dev, dma->channels[0], flags);
		break;

	case ACPI_RESOURCE_TYPE_IO:
		io = &res->data.io;
		pnpacpi_parse_allocated_ioresource(dev,
			io->minimum,
			io->address_length,
			io->io_decode);
		break;

	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
	case ACPI_RESOURCE_TYPE_END_DEPENDENT:
		break;

	case ACPI_RESOURCE_TYPE_FIXED_IO:
		fixed_io = &res->data.fixed_io;
		pnpacpi_parse_allocated_ioresource(dev,
			fixed_io->address,
			fixed_io->address_length,
			ACPI_DECODE_10);
		break;

	case ACPI_RESOURCE_TYPE_VENDOR:
		vendor_typed = &res->data.vendor_typed;
		pnpacpi_parse_allocated_vendor(dev, vendor_typed);
		break;

	case ACPI_RESOURCE_TYPE_END_TAG:
		break;

	case ACPI_RESOURCE_TYPE_MEMORY24:
		memory24 = &res->data.memory24;
		pnpacpi_parse_allocated_memresource(dev,
			memory24->minimum,
			memory24->address_length,
			memory24->write_protect);
		break;
	case ACPI_RESOURCE_TYPE_MEMORY32:
		memory32 = &res->data.memory32;
		pnpacpi_parse_allocated_memresource(dev,
			memory32->minimum,
			memory32->address_length,
			memory32->write_protect);
		break;
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		fixed_memory32 = &res->data.fixed_memory32;
		pnpacpi_parse_allocated_memresource(dev,
			fixed_memory32->address,
			fixed_memory32->address_length,
			fixed_memory32->write_protect);
		break;
	case ACPI_RESOURCE_TYPE_ADDRESS16:
	case ACPI_RESOURCE_TYPE_ADDRESS32:
	case ACPI_RESOURCE_TYPE_ADDRESS64:
		pnpacpi_parse_allocated_address_space(dev, res);
		break;

	case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
		if (res->data.ext_address64.producer_consumer == ACPI_PRODUCER)
			return AE_OK;
		break;

	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		extended_irq = &res->data.extended_irq;

		if (extended_irq->interrupt_count == 0)
			pnp_add_irq_resource(dev, 0, IORESOURCE_DISABLED);
		else {
			for (i = 0; i < extended_irq->interrupt_count; i++) {
				pnpacpi_parse_allocated_irqresource(dev,
					extended_irq->interrupts[i],
					extended_irq->triggering,
					extended_irq->polarity,
					extended_irq->sharable);
			}

			/*
			 * The IRQ encoder puts a single interrupt in each
			 * descriptor, so if a _CRS descriptor has more than
			 * one interrupt, we won't be able to re-encode it.
			 */
			if (pnp_can_write(dev) &&
			    extended_irq->interrupt_count > 1) {
				dev_warn(&dev->dev, "multiple interrupts in "
					 "_CRS descriptor; configuration can't "
					 "be changed\n");
				dev->capabilities &= ~PNP_WRITE;
			}
		}
		break;

	case ACPI_RESOURCE_TYPE_GENERIC_REGISTER:
		break;

	default:
		dev_warn(&dev->dev, "unknown resource type %d in _CRS\n",
			 res->type);
		return AE_ERROR;
	}

	return AE_OK;
}

int pnpacpi_parse_allocated_resource(struct pnp_dev *dev)
{
	acpi_handle handle = dev->data;
	acpi_status status;

	dev_dbg(&dev->dev, "parse allocated resources\n");

	pnp_init_resources(dev);

	status = acpi_walk_resources(handle, METHOD_NAME__CRS,
				     pnpacpi_allocated_resource, dev);

	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND)
			dev_err(&dev->dev, "can't evaluate _CRS: %d", status);
		return -EPERM;
	}
	return 0;
}

static __init void pnpacpi_parse_dma_option(struct pnp_dev *dev,
					    unsigned int option_flags,
					    struct acpi_resource_dma *p)
{
	int i;
	unsigned char map = 0, flags;

	if (p->channel_count == 0)
		return;

	for (i = 0; i < p->channel_count; i++)
		map |= 1 << p->channels[i];

	flags = dma_flags(dev, p->type, p->bus_master, p->transfer);
	pnp_register_dma_resource(dev, option_flags, map, flags);
}

static __init void pnpacpi_parse_irq_option(struct pnp_dev *dev,
					    unsigned int option_flags,
					    struct acpi_resource_irq *p)
{
	int i;
	pnp_irq_mask_t map;
	unsigned char flags;

	if (p->interrupt_count == 0)
		return;

	bitmap_zero(map.bits, PNP_IRQ_NR);
	for (i = 0; i < p->interrupt_count; i++)
		if (p->interrupts[i])
			__set_bit(p->interrupts[i], map.bits);

	flags = irq_flags(p->triggering, p->polarity, p->sharable);
	pnp_register_irq_resource(dev, option_flags, &map, flags);
}

static __init void pnpacpi_parse_ext_irq_option(struct pnp_dev *dev,
					unsigned int option_flags,
					struct acpi_resource_extended_irq *p)
{
	int i;
	pnp_irq_mask_t map;
	unsigned char flags;

	if (p->interrupt_count == 0)
		return;

	bitmap_zero(map.bits, PNP_IRQ_NR);
	for (i = 0; i < p->interrupt_count; i++) {
		if (p->interrupts[i]) {
			if (p->interrupts[i] < PNP_IRQ_NR)
				__set_bit(p->interrupts[i], map.bits);
			else
				dev_err(&dev->dev, "ignoring IRQ %d option "
					"(too large for %d entry bitmap)\n",
					p->interrupts[i], PNP_IRQ_NR);
		}
	}

	flags = irq_flags(p->triggering, p->polarity, p->sharable);
	pnp_register_irq_resource(dev, option_flags, &map, flags);
}

static __init void pnpacpi_parse_port_option(struct pnp_dev *dev,
					     unsigned int option_flags,
					     struct acpi_resource_io *io)
{
	unsigned char flags = 0;

	if (io->address_length == 0)
		return;

	if (io->io_decode == ACPI_DECODE_16)
		flags = IORESOURCE_IO_16BIT_ADDR;
	pnp_register_port_resource(dev, option_flags, io->minimum, io->maximum,
				   io->alignment, io->address_length, flags);
}

static __init void pnpacpi_parse_fixed_port_option(struct pnp_dev *dev,
					unsigned int option_flags,
					struct acpi_resource_fixed_io *io)
{
	if (io->address_length == 0)
		return;

	pnp_register_port_resource(dev, option_flags, io->address, io->address,
				   0, io->address_length, IORESOURCE_IO_FIXED);
}

static __init void pnpacpi_parse_mem24_option(struct pnp_dev *dev,
					      unsigned int option_flags,
					      struct acpi_resource_memory24 *p)
{
	unsigned char flags = 0;

	if (p->address_length == 0)
		return;

	if (p->write_protect == ACPI_READ_WRITE_MEMORY)
		flags = IORESOURCE_MEM_WRITEABLE;
	pnp_register_mem_resource(dev, option_flags, p->minimum, p->maximum,
				  p->alignment, p->address_length, flags);
}

static __init void pnpacpi_parse_mem32_option(struct pnp_dev *dev,
					      unsigned int option_flags,
					      struct acpi_resource_memory32 *p)
{
	unsigned char flags = 0;

	if (p->address_length == 0)
		return;

	if (p->write_protect == ACPI_READ_WRITE_MEMORY)
		flags = IORESOURCE_MEM_WRITEABLE;
	pnp_register_mem_resource(dev, option_flags, p->minimum, p->maximum,
				  p->alignment, p->address_length, flags);
}

static __init void pnpacpi_parse_fixed_mem32_option(struct pnp_dev *dev,
					unsigned int option_flags,
					struct acpi_resource_fixed_memory32 *p)
{
	unsigned char flags = 0;

	if (p->address_length == 0)
		return;

	if (p->write_protect == ACPI_READ_WRITE_MEMORY)
		flags = IORESOURCE_MEM_WRITEABLE;
	pnp_register_mem_resource(dev, option_flags, p->address, p->address,
				  0, p->address_length, flags);
}

static __init void pnpacpi_parse_address_option(struct pnp_dev *dev,
						unsigned int option_flags,
						struct acpi_resource *r)
{
	struct acpi_resource_address64 addr, *p = &addr;
	acpi_status status;
	unsigned char flags = 0;

	status = acpi_resource_to_address64(r, p);
	if (ACPI_FAILURE(status)) {
		dev_warn(&dev->dev, "can't convert resource type %d\n",
			 r->type);
		return;
	}

	if (p->address_length == 0)
		return;

	if (p->resource_type == ACPI_MEMORY_RANGE) {
		if (p->info.mem.write_protect == ACPI_READ_WRITE_MEMORY)
			flags = IORESOURCE_MEM_WRITEABLE;
		pnp_register_mem_resource(dev, option_flags, p->minimum,
					  p->minimum, 0, p->address_length,
					  flags);
	} else if (p->resource_type == ACPI_IO_RANGE)
		pnp_register_port_resource(dev, option_flags, p->minimum,
					   p->minimum, 0, p->address_length,
					   IORESOURCE_IO_FIXED);
}

struct acpipnp_parse_option_s {
	struct pnp_dev *dev;
	unsigned int option_flags;
};

static __init acpi_status pnpacpi_option_resource(struct acpi_resource *res,
						  void *data)
{
	int priority;
	struct acpipnp_parse_option_s *parse_data = data;
	struct pnp_dev *dev = parse_data->dev;
	unsigned int option_flags = parse_data->option_flags;

	switch (res->type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		pnpacpi_parse_irq_option(dev, option_flags, &res->data.irq);
		break;

	case ACPI_RESOURCE_TYPE_DMA:
		pnpacpi_parse_dma_option(dev, option_flags, &res->data.dma);
		break;

	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
		switch (res->data.start_dpf.compatibility_priority) {
		case ACPI_GOOD_CONFIGURATION:
			priority = PNP_RES_PRIORITY_PREFERRED;
			break;

		case ACPI_ACCEPTABLE_CONFIGURATION:
			priority = PNP_RES_PRIORITY_ACCEPTABLE;
			break;

		case ACPI_SUB_OPTIMAL_CONFIGURATION:
			priority = PNP_RES_PRIORITY_FUNCTIONAL;
			break;
		default:
			priority = PNP_RES_PRIORITY_INVALID;
			break;
		}
		parse_data->option_flags = pnp_new_dependent_set(dev, priority);
		break;

	case ACPI_RESOURCE_TYPE_END_DEPENDENT:
		parse_data->option_flags = 0;
		break;

	case ACPI_RESOURCE_TYPE_IO:
		pnpacpi_parse_port_option(dev, option_flags, &res->data.io);
		break;

	case ACPI_RESOURCE_TYPE_FIXED_IO:
		pnpacpi_parse_fixed_port_option(dev, option_flags,
					        &res->data.fixed_io);
		break;

	case ACPI_RESOURCE_TYPE_VENDOR:
	case ACPI_RESOURCE_TYPE_END_TAG:
		break;

	case ACPI_RESOURCE_TYPE_MEMORY24:
		pnpacpi_parse_mem24_option(dev, option_flags,
					   &res->data.memory24);
		break;

	case ACPI_RESOURCE_TYPE_MEMORY32:
		pnpacpi_parse_mem32_option(dev, option_flags,
					   &res->data.memory32);
		break;

	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		pnpacpi_parse_fixed_mem32_option(dev, option_flags,
						 &res->data.fixed_memory32);
		break;

	case ACPI_RESOURCE_TYPE_ADDRESS16:
	case ACPI_RESOURCE_TYPE_ADDRESS32:
	case ACPI_RESOURCE_TYPE_ADDRESS64:
		pnpacpi_parse_address_option(dev, option_flags, res);
		break;

	case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
		break;

	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		pnpacpi_parse_ext_irq_option(dev, option_flags,
					     &res->data.extended_irq);
		break;

	case ACPI_RESOURCE_TYPE_GENERIC_REGISTER:
		break;

	default:
		dev_warn(&dev->dev, "unknown resource type %d in _PRS\n",
			 res->type);
		return AE_ERROR;
	}

	return AE_OK;
}

int __init pnpacpi_parse_resource_option_data(struct pnp_dev *dev)
{
	acpi_handle handle = dev->data;
	acpi_status status;
	struct acpipnp_parse_option_s parse_data;

	dev_dbg(&dev->dev, "parse resource options\n");

	parse_data.dev = dev;
	parse_data.option_flags = 0;

	status = acpi_walk_resources(handle, METHOD_NAME__PRS,
				     pnpacpi_option_resource, &parse_data);

	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND)
			dev_err(&dev->dev, "can't evaluate _PRS: %d", status);
		return -EPERM;
	}
	return 0;
}

static int pnpacpi_supported_resource(struct acpi_resource *res)
{
	switch (res->type) {
	case ACPI_RESOURCE_TYPE_IRQ:
	case ACPI_RESOURCE_TYPE_DMA:
	case ACPI_RESOURCE_TYPE_IO:
	case ACPI_RESOURCE_TYPE_FIXED_IO:
	case ACPI_RESOURCE_TYPE_MEMORY24:
	case ACPI_RESOURCE_TYPE_MEMORY32:
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
	case ACPI_RESOURCE_TYPE_ADDRESS16:
	case ACPI_RESOURCE_TYPE_ADDRESS32:
	case ACPI_RESOURCE_TYPE_ADDRESS64:
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		return 1;
	}
	return 0;
}

/*
 * Set resource
 */
static acpi_status pnpacpi_count_resources(struct acpi_resource *res,
					   void *data)
{
	int *res_cnt = data;

	if (pnpacpi_supported_resource(res))
		(*res_cnt)++;
	return AE_OK;
}

static acpi_status pnpacpi_type_resources(struct acpi_resource *res, void *data)
{
	struct acpi_resource **resource = data;

	if (pnpacpi_supported_resource(res)) {
		(*resource)->type = res->type;
		(*resource)->length = sizeof(struct acpi_resource);
		if (res->type == ACPI_RESOURCE_TYPE_IRQ)
			(*resource)->data.irq.descriptor_length =
					res->data.irq.descriptor_length;
		(*resource)++;
	}

	return AE_OK;
}

int pnpacpi_build_resource_template(struct pnp_dev *dev,
				    struct acpi_buffer *buffer)
{
	acpi_handle handle = dev->data;
	struct acpi_resource *resource;
	int res_cnt = 0;
	acpi_status status;

	status = acpi_walk_resources(handle, METHOD_NAME__CRS,
				     pnpacpi_count_resources, &res_cnt);
	if (ACPI_FAILURE(status)) {
		dev_err(&dev->dev, "can't evaluate _CRS: %d\n", status);
		return -EINVAL;
	}
	if (!res_cnt)
		return -EINVAL;
	buffer->length = sizeof(struct acpi_resource) * (res_cnt + 1) + 1;
	buffer->pointer = kzalloc(buffer->length - 1, GFP_KERNEL);
	if (!buffer->pointer)
		return -ENOMEM;

	resource = (struct acpi_resource *)buffer->pointer;
	status = acpi_walk_resources(handle, METHOD_NAME__CRS,
				     pnpacpi_type_resources, &resource);
	if (ACPI_FAILURE(status)) {
		kfree(buffer->pointer);
		dev_err(&dev->dev, "can't evaluate _CRS: %d\n", status);
		return -EINVAL;
	}
	/* resource will pointer the end resource now */
	resource->type = ACPI_RESOURCE_TYPE_END_TAG;

	return 0;
}

static void pnpacpi_encode_irq(struct pnp_dev *dev,
			       struct acpi_resource *resource,
			       struct resource *p)
{
	struct acpi_resource_irq *irq = &resource->data.irq;
	int triggering, polarity, shareable;

	if (!pnp_resource_enabled(p)) {
		irq->interrupt_count = 0;
		dev_dbg(&dev->dev, "  encode irq (%s)\n",
			p ? "disabled" : "missing");
		return;
	}

	decode_irq_flags(dev, p->flags, &triggering, &polarity, &shareable);
	irq->triggering = triggering;
	irq->polarity = polarity;
	irq->sharable = shareable;
	irq->interrupt_count = 1;
	irq->interrupts[0] = p->start;

	dev_dbg(&dev->dev, "  encode irq %d %s %s %s (%d-byte descriptor)\n",
		(int) p->start,
		triggering == ACPI_LEVEL_SENSITIVE ? "level" : "edge",
		polarity == ACPI_ACTIVE_LOW ? "low" : "high",
		irq->sharable == ACPI_SHARED ? "shared" : "exclusive",
		irq->descriptor_length);
}

static void pnpacpi_encode_ext_irq(struct pnp_dev *dev,
				   struct acpi_resource *resource,
				   struct resource *p)
{
	struct acpi_resource_extended_irq *extended_irq = &resource->data.extended_irq;
	int triggering, polarity, shareable;

	if (!pnp_resource_enabled(p)) {
		extended_irq->interrupt_count = 0;
		dev_dbg(&dev->dev, "  encode extended irq (%s)\n",
			p ? "disabled" : "missing");
		return;
	}

	decode_irq_flags(dev, p->flags, &triggering, &polarity, &shareable);
	extended_irq->producer_consumer = ACPI_CONSUMER;
	extended_irq->triggering = triggering;
	extended_irq->polarity = polarity;
	extended_irq->sharable = shareable;
	extended_irq->interrupt_count = 1;
	extended_irq->interrupts[0] = p->start;

	dev_dbg(&dev->dev, "  encode irq %d %s %s %s\n", (int) p->start,
		triggering == ACPI_LEVEL_SENSITIVE ? "level" : "edge",
		polarity == ACPI_ACTIVE_LOW ? "low" : "high",
		extended_irq->sharable == ACPI_SHARED ? "shared" : "exclusive");
}

static void pnpacpi_encode_dma(struct pnp_dev *dev,
			       struct acpi_resource *resource,
			       struct resource *p)
{
	struct acpi_resource_dma *dma = &resource->data.dma;

	if (!pnp_resource_enabled(p)) {
		dma->channel_count = 0;
		dev_dbg(&dev->dev, "  encode dma (%s)\n",
			p ? "disabled" : "missing");
		return;
	}

	/* Note: pnp_assign_dma will copy pnp_dma->flags into p->flags */
	switch (p->flags & IORESOURCE_DMA_SPEED_MASK) {
	case IORESOURCE_DMA_TYPEA:
		dma->type = ACPI_TYPE_A;
		break;
	case IORESOURCE_DMA_TYPEB:
		dma->type = ACPI_TYPE_B;
		break;
	case IORESOURCE_DMA_TYPEF:
		dma->type = ACPI_TYPE_F;
		break;
	default:
		dma->type = ACPI_COMPATIBILITY;
	}

	switch (p->flags & IORESOURCE_DMA_TYPE_MASK) {
	case IORESOURCE_DMA_8BIT:
		dma->transfer = ACPI_TRANSFER_8;
		break;
	case IORESOURCE_DMA_8AND16BIT:
		dma->transfer = ACPI_TRANSFER_8_16;
		break;
	default:
		dma->transfer = ACPI_TRANSFER_16;
	}

	dma->bus_master = !!(p->flags & IORESOURCE_DMA_MASTER);
	dma->channel_count = 1;
	dma->channels[0] = p->start;

	dev_dbg(&dev->dev, "  encode dma %d "
		"type %#x transfer %#x master %d\n",
		(int) p->start, dma->type, dma->transfer, dma->bus_master);
}

static void pnpacpi_encode_io(struct pnp_dev *dev,
			      struct acpi_resource *resource,
			      struct resource *p)
{
	struct acpi_resource_io *io = &resource->data.io;

	if (pnp_resource_enabled(p)) {
		/* Note: pnp_assign_port copies pnp_port->flags into p->flags */
		io->io_decode = (p->flags & IORESOURCE_IO_16BIT_ADDR) ?
		    ACPI_DECODE_16 : ACPI_DECODE_10;
		io->minimum = p->start;
		io->maximum = p->end;
		io->alignment = 0;	/* Correct? */
		io->address_length = p->end - p->start + 1;
	} else {
		io->minimum = 0;
		io->address_length = 0;
	}

	dev_dbg(&dev->dev, "  encode io %#x-%#x decode %#x\n", io->minimum,
		io->minimum + io->address_length - 1, io->io_decode);
}

static void pnpacpi_encode_fixed_io(struct pnp_dev *dev,
				    struct acpi_resource *resource,
				    struct resource *p)
{
	struct acpi_resource_fixed_io *fixed_io = &resource->data.fixed_io;

	if (pnp_resource_enabled(p)) {
		fixed_io->address = p->start;
		fixed_io->address_length = p->end - p->start + 1;
	} else {
		fixed_io->address = 0;
		fixed_io->address_length = 0;
	}

	dev_dbg(&dev->dev, "  encode fixed_io %#x-%#x\n", fixed_io->address,
		fixed_io->address + fixed_io->address_length - 1);
}

static void pnpacpi_encode_mem24(struct pnp_dev *dev,
				 struct acpi_resource *resource,
				 struct resource *p)
{
	struct acpi_resource_memory24 *memory24 = &resource->data.memory24;

	if (pnp_resource_enabled(p)) {
		/* Note: pnp_assign_mem copies pnp_mem->flags into p->flags */
		memory24->write_protect = p->flags & IORESOURCE_MEM_WRITEABLE ?
		    ACPI_READ_WRITE_MEMORY : ACPI_READ_ONLY_MEMORY;
		memory24->minimum = p->start;
		memory24->maximum = p->end;
		memory24->alignment = 0;
		memory24->address_length = p->end - p->start + 1;
	} else {
		memory24->minimum = 0;
		memory24->address_length = 0;
	}

	dev_dbg(&dev->dev, "  encode mem24 %#x-%#x write_protect %#x\n",
		memory24->minimum,
		memory24->minimum + memory24->address_length - 1,
		memory24->write_protect);
}

static void pnpacpi_encode_mem32(struct pnp_dev *dev,
				 struct acpi_resource *resource,
				 struct resource *p)
{
	struct acpi_resource_memory32 *memory32 = &resource->data.memory32;

	if (pnp_resource_enabled(p)) {
		memory32->write_protect = p->flags & IORESOURCE_MEM_WRITEABLE ?
		    ACPI_READ_WRITE_MEMORY : ACPI_READ_ONLY_MEMORY;
		memory32->minimum = p->start;
		memory32->maximum = p->end;
		memory32->alignment = 0;
		memory32->address_length = p->end - p->start + 1;
	} else {
		memory32->minimum = 0;
		memory32->alignment = 0;
	}

	dev_dbg(&dev->dev, "  encode mem32 %#x-%#x write_protect %#x\n",
		memory32->minimum,
		memory32->minimum + memory32->address_length - 1,
		memory32->write_protect);
}

static void pnpacpi_encode_fixed_mem32(struct pnp_dev *dev,
				       struct acpi_resource *resource,
				       struct resource *p)
{
	struct acpi_resource_fixed_memory32 *fixed_memory32 = &resource->data.fixed_memory32;

	if (pnp_resource_enabled(p)) {
		fixed_memory32->write_protect =
		    p->flags & IORESOURCE_MEM_WRITEABLE ?
		    ACPI_READ_WRITE_MEMORY : ACPI_READ_ONLY_MEMORY;
		fixed_memory32->address = p->start;
		fixed_memory32->address_length = p->end - p->start + 1;
	} else {
		fixed_memory32->address = 0;
		fixed_memory32->address_length = 0;
	}

	dev_dbg(&dev->dev, "  encode fixed_mem32 %#x-%#x write_protect %#x\n",
		fixed_memory32->address,
		fixed_memory32->address + fixed_memory32->address_length - 1,
		fixed_memory32->write_protect);
}

int pnpacpi_encode_resources(struct pnp_dev *dev, struct acpi_buffer *buffer)
{
	int i = 0;
	/* pnpacpi_build_resource_template allocates extra mem */
	int res_cnt = (buffer->length - 1) / sizeof(struct acpi_resource) - 1;
	struct acpi_resource *resource = buffer->pointer;
	int port = 0, irq = 0, dma = 0, mem = 0;

	dev_dbg(&dev->dev, "encode %d resources\n", res_cnt);
	while (i < res_cnt) {
		switch (resource->type) {
		case ACPI_RESOURCE_TYPE_IRQ:
			pnpacpi_encode_irq(dev, resource,
			       pnp_get_resource(dev, IORESOURCE_IRQ, irq));
			irq++;
			break;

		case ACPI_RESOURCE_TYPE_DMA:
			pnpacpi_encode_dma(dev, resource,
				pnp_get_resource(dev, IORESOURCE_DMA, dma));
			dma++;
			break;
		case ACPI_RESOURCE_TYPE_IO:
			pnpacpi_encode_io(dev, resource,
				pnp_get_resource(dev, IORESOURCE_IO, port));
			port++;
			break;
		case ACPI_RESOURCE_TYPE_FIXED_IO:
			pnpacpi_encode_fixed_io(dev, resource,
				pnp_get_resource(dev, IORESOURCE_IO, port));
			port++;
			break;
		case ACPI_RESOURCE_TYPE_MEMORY24:
			pnpacpi_encode_mem24(dev, resource,
				pnp_get_resource(dev, IORESOURCE_MEM, mem));
			mem++;
			break;
		case ACPI_RESOURCE_TYPE_MEMORY32:
			pnpacpi_encode_mem32(dev, resource,
				pnp_get_resource(dev, IORESOURCE_MEM, mem));
			mem++;
			break;
		case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
			pnpacpi_encode_fixed_mem32(dev, resource,
				pnp_get_resource(dev, IORESOURCE_MEM, mem));
			mem++;
			break;
		case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
			pnpacpi_encode_ext_irq(dev, resource,
				pnp_get_resource(dev, IORESOURCE_IRQ, irq));
			irq++;
			break;
		case ACPI_RESOURCE_TYPE_START_DEPENDENT:
		case ACPI_RESOURCE_TYPE_END_DEPENDENT:
		case ACPI_RESOURCE_TYPE_VENDOR:
		case ACPI_RESOURCE_TYPE_END_TAG:
		case ACPI_RESOURCE_TYPE_ADDRESS16:
		case ACPI_RESOURCE_TYPE_ADDRESS32:
		case ACPI_RESOURCE_TYPE_ADDRESS64:
		case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
		case ACPI_RESOURCE_TYPE_GENERIC_REGISTER:
		default:	/* other type */
			dev_warn(&dev->dev, "can't encode unknown resource "
				 "type %d\n", resource->type);
			return -EINVAL;
		}
		resource++;
		i++;
	}
	return 0;
}
