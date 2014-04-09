/*
 * drivers/acpi/resource.c - ACPI device resources interpretation.
 *
 * Copyright (C) 2012, Intel Corp.
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#ifdef CONFIG_X86
#define valid_IRQ(i) (((i) != 0) && ((i) != 2))
#else
#define valid_IRQ(i) (true)
#endif

static unsigned long acpi_dev_memresource_flags(u64 len, u8 write_protect,
						bool window)
{
	unsigned long flags = IORESOURCE_MEM;

	if (len == 0)
		flags |= IORESOURCE_DISABLED;

	if (write_protect == ACPI_READ_WRITE_MEMORY)
		flags |= IORESOURCE_MEM_WRITEABLE;

	if (window)
		flags |= IORESOURCE_WINDOW;

	return flags;
}

static void acpi_dev_get_memresource(struct resource *res, u64 start, u64 len,
				     u8 write_protect)
{
	res->start = start;
	res->end = start + len - 1;
	res->flags = acpi_dev_memresource_flags(len, write_protect, false);
}

/**
 * acpi_dev_resource_memory - Extract ACPI memory resource information.
 * @ares: Input ACPI resource object.
 * @res: Output generic resource object.
 *
 * Check if the given ACPI resource object represents a memory resource and
 * if that's the case, use the information in it to populate the generic
 * resource object pointed to by @res.
 */
bool acpi_dev_resource_memory(struct acpi_resource *ares, struct resource *res)
{
	struct acpi_resource_memory24 *memory24;
	struct acpi_resource_memory32 *memory32;
	struct acpi_resource_fixed_memory32 *fixed_memory32;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_MEMORY24:
		memory24 = &ares->data.memory24;
		if (!memory24->address_length)
			return false;
		acpi_dev_get_memresource(res, memory24->minimum,
					 memory24->address_length,
					 memory24->write_protect);
		break;
	case ACPI_RESOURCE_TYPE_MEMORY32:
		memory32 = &ares->data.memory32;
		if (!memory32->address_length)
			return false;
		acpi_dev_get_memresource(res, memory32->minimum,
					 memory32->address_length,
					 memory32->write_protect);
		break;
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		fixed_memory32 = &ares->data.fixed_memory32;
		if (!fixed_memory32->address_length)
			return false;
		acpi_dev_get_memresource(res, fixed_memory32->address,
					 fixed_memory32->address_length,
					 fixed_memory32->write_protect);
		break;
	default:
		return false;
	}
	return true;
}
EXPORT_SYMBOL_GPL(acpi_dev_resource_memory);

static unsigned int acpi_dev_ioresource_flags(u64 start, u64 end, u8 io_decode,
					      bool window)
{
	int flags = IORESOURCE_IO;

	if (io_decode == ACPI_DECODE_16)
		flags |= IORESOURCE_IO_16BIT_ADDR;

	if (start > end || end >= 0x10003)
		flags |= IORESOURCE_DISABLED;

	if (window)
		flags |= IORESOURCE_WINDOW;

	return flags;
}

static void acpi_dev_get_ioresource(struct resource *res, u64 start, u64 len,
				    u8 io_decode)
{
	u64 end = start + len - 1;

	res->start = start;
	res->end = end;
	res->flags = acpi_dev_ioresource_flags(start, end, io_decode, false);
}

/**
 * acpi_dev_resource_io - Extract ACPI I/O resource information.
 * @ares: Input ACPI resource object.
 * @res: Output generic resource object.
 *
 * Check if the given ACPI resource object represents an I/O resource and
 * if that's the case, use the information in it to populate the generic
 * resource object pointed to by @res.
 */
bool acpi_dev_resource_io(struct acpi_resource *ares, struct resource *res)
{
	struct acpi_resource_io *io;
	struct acpi_resource_fixed_io *fixed_io;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_IO:
		io = &ares->data.io;
		if (!io->address_length)
			return false;
		acpi_dev_get_ioresource(res, io->minimum,
					io->address_length,
					io->io_decode);
		break;
	case ACPI_RESOURCE_TYPE_FIXED_IO:
		fixed_io = &ares->data.fixed_io;
		if (!fixed_io->address_length)
			return false;
		acpi_dev_get_ioresource(res, fixed_io->address,
					fixed_io->address_length,
					ACPI_DECODE_10);
		break;
	default:
		return false;
	}
	return true;
}
EXPORT_SYMBOL_GPL(acpi_dev_resource_io);

/**
 * acpi_dev_resource_address_space - Extract ACPI address space information.
 * @ares: Input ACPI resource object.
 * @res: Output generic resource object.
 *
 * Check if the given ACPI resource object represents an address space resource
 * and if that's the case, use the information in it to populate the generic
 * resource object pointed to by @res.
 */
bool acpi_dev_resource_address_space(struct acpi_resource *ares,
				     struct resource *res)
{
	acpi_status status;
	struct acpi_resource_address64 addr;
	bool window;
	u64 len;
	u8 io_decode;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_ADDRESS16:
	case ACPI_RESOURCE_TYPE_ADDRESS32:
	case ACPI_RESOURCE_TYPE_ADDRESS64:
		break;
	default:
		return false;
	}

	status = acpi_resource_to_address64(ares, &addr);
	if (ACPI_FAILURE(status))
		return true;

	res->start = addr.minimum;
	res->end = addr.maximum;
	window = addr.producer_consumer == ACPI_PRODUCER;

	switch(addr.resource_type) {
	case ACPI_MEMORY_RANGE:
		len = addr.maximum - addr.minimum + 1;
		res->flags = acpi_dev_memresource_flags(len,
						addr.info.mem.write_protect,
						window);
		break;
	case ACPI_IO_RANGE:
		io_decode = addr.granularity == 0xfff ?
				ACPI_DECODE_10 : ACPI_DECODE_16;
		res->flags = acpi_dev_ioresource_flags(addr.minimum,
						       addr.maximum,
						       io_decode, window);
		break;
	case ACPI_BUS_NUMBER_RANGE:
		res->flags = IORESOURCE_BUS;
		break;
	default:
		res->flags = 0;
	}

	return true;
}
EXPORT_SYMBOL_GPL(acpi_dev_resource_address_space);

/**
 * acpi_dev_resource_ext_address_space - Extract ACPI address space information.
 * @ares: Input ACPI resource object.
 * @res: Output generic resource object.
 *
 * Check if the given ACPI resource object represents an extended address space
 * resource and if that's the case, use the information in it to populate the
 * generic resource object pointed to by @res.
 */
bool acpi_dev_resource_ext_address_space(struct acpi_resource *ares,
					 struct resource *res)
{
	struct acpi_resource_extended_address64 *ext_addr;
	bool window;
	u64 len;
	u8 io_decode;

	if (ares->type != ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64)
		return false;

	ext_addr = &ares->data.ext_address64;

	res->start = ext_addr->minimum;
	res->end = ext_addr->maximum;
	window = ext_addr->producer_consumer == ACPI_PRODUCER;

	switch(ext_addr->resource_type) {
	case ACPI_MEMORY_RANGE:
		len = ext_addr->maximum - ext_addr->minimum + 1;
		res->flags = acpi_dev_memresource_flags(len,
					ext_addr->info.mem.write_protect,
					window);
		break;
	case ACPI_IO_RANGE:
		io_decode = ext_addr->granularity == 0xfff ?
				ACPI_DECODE_10 : ACPI_DECODE_16;
		res->flags = acpi_dev_ioresource_flags(ext_addr->minimum,
						       ext_addr->maximum,
						       io_decode, window);
		break;
	case ACPI_BUS_NUMBER_RANGE:
		res->flags = IORESOURCE_BUS;
		break;
	default:
		res->flags = 0;
	}

	return true;
}
EXPORT_SYMBOL_GPL(acpi_dev_resource_ext_address_space);

/**
 * acpi_dev_irq_flags - Determine IRQ resource flags.
 * @triggering: Triggering type as provided by ACPI.
 * @polarity: Interrupt polarity as provided by ACPI.
 * @shareable: Whether or not the interrupt is shareable.
 */
unsigned long acpi_dev_irq_flags(u8 triggering, u8 polarity, u8 shareable)
{
	unsigned long flags;

	if (triggering == ACPI_LEVEL_SENSITIVE)
		flags = polarity == ACPI_ACTIVE_LOW ?
			IORESOURCE_IRQ_LOWLEVEL : IORESOURCE_IRQ_HIGHLEVEL;
	else
		flags = polarity == ACPI_ACTIVE_LOW ?
			IORESOURCE_IRQ_LOWEDGE : IORESOURCE_IRQ_HIGHEDGE;

	if (shareable == ACPI_SHARED)
		flags |= IORESOURCE_IRQ_SHAREABLE;

	return flags | IORESOURCE_IRQ;
}
EXPORT_SYMBOL_GPL(acpi_dev_irq_flags);

static void acpi_dev_irqresource_disabled(struct resource *res, u32 gsi)
{
	res->start = gsi;
	res->end = gsi;
	res->flags = IORESOURCE_IRQ | IORESOURCE_DISABLED;
}

static void acpi_dev_get_irqresource(struct resource *res, u32 gsi,
				     u8 triggering, u8 polarity, u8 shareable,
				     bool legacy)
{
	int irq, p, t;

	if (!valid_IRQ(gsi)) {
		acpi_dev_irqresource_disabled(res, gsi);
		return;
	}

	/*
	 * In IO-APIC mode, use overrided attribute. Two reasons:
	 * 1. BIOS bug in DSDT
	 * 2. BIOS uses IO-APIC mode Interrupt Source Override
	 *
	 * We do this only if we are dealing with IRQ() or IRQNoFlags()
	 * resource (the legacy ISA resources). With modern ACPI 5 devices
	 * using extended IRQ descriptors we take the IRQ configuration
	 * from _CRS directly.
	 */
	if (legacy && !acpi_get_override_irq(gsi, &t, &p)) {
		u8 trig = t ? ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE;
		u8 pol = p ? ACPI_ACTIVE_LOW : ACPI_ACTIVE_HIGH;

		if (triggering != trig || polarity != pol) {
			pr_warning("ACPI: IRQ %d override to %s, %s\n", gsi,
				   t ? "level" : "edge", p ? "low" : "high");
			triggering = trig;
			polarity = pol;
		}
	}

	res->flags = acpi_dev_irq_flags(triggering, polarity, shareable);
	irq = acpi_register_gsi(NULL, gsi, triggering, polarity);
	if (irq >= 0) {
		res->start = irq;
		res->end = irq;
	} else {
		acpi_dev_irqresource_disabled(res, gsi);
	}
}

/**
 * acpi_dev_resource_interrupt - Extract ACPI interrupt resource information.
 * @ares: Input ACPI resource object.
 * @index: Index into the array of GSIs represented by the resource.
 * @res: Output generic resource object.
 *
 * Check if the given ACPI resource object represents an interrupt resource
 * and @index does not exceed the resource's interrupt count (true is returned
 * in that case regardless of the results of the other checks)).  If that's the
 * case, register the GSI corresponding to @index from the array of interrupts
 * represented by the resource and populate the generic resource object pointed
 * to by @res accordingly.  If the registration of the GSI is not successful,
 * IORESOURCE_DISABLED will be set it that object's flags.
 */
bool acpi_dev_resource_interrupt(struct acpi_resource *ares, int index,
				 struct resource *res)
{
	struct acpi_resource_irq *irq;
	struct acpi_resource_extended_irq *ext_irq;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		/*
		 * Per spec, only one interrupt per descriptor is allowed in
		 * _CRS, but some firmware violates this, so parse them all.
		 */
		irq = &ares->data.irq;
		if (index >= irq->interrupt_count) {
			acpi_dev_irqresource_disabled(res, 0);
			return false;
		}
		acpi_dev_get_irqresource(res, irq->interrupts[index],
					 irq->triggering, irq->polarity,
					 irq->sharable, true);
		break;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		ext_irq = &ares->data.extended_irq;
		if (index >= ext_irq->interrupt_count) {
			acpi_dev_irqresource_disabled(res, 0);
			return false;
		}
		acpi_dev_get_irqresource(res, ext_irq->interrupts[index],
					 ext_irq->triggering, ext_irq->polarity,
					 ext_irq->sharable, false);
		break;
	default:
		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(acpi_dev_resource_interrupt);

/**
 * acpi_dev_free_resource_list - Free resource from %acpi_dev_get_resources().
 * @list: The head of the resource list to free.
 */
void acpi_dev_free_resource_list(struct list_head *list)
{
	struct resource_list_entry *rentry, *re;

	list_for_each_entry_safe(rentry, re, list, node) {
		list_del(&rentry->node);
		kfree(rentry);
	}
}
EXPORT_SYMBOL_GPL(acpi_dev_free_resource_list);

struct res_proc_context {
	struct list_head *list;
	int (*preproc)(struct acpi_resource *, void *);
	void *preproc_data;
	int count;
	int error;
};

static acpi_status acpi_dev_new_resource_entry(struct resource *r,
					       struct res_proc_context *c)
{
	struct resource_list_entry *rentry;

	rentry = kmalloc(sizeof(*rentry), GFP_KERNEL);
	if (!rentry) {
		c->error = -ENOMEM;
		return AE_NO_MEMORY;
	}
	rentry->res = *r;
	list_add_tail(&rentry->node, c->list);
	c->count++;
	return AE_OK;
}

static acpi_status acpi_dev_process_resource(struct acpi_resource *ares,
					     void *context)
{
	struct res_proc_context *c = context;
	struct resource r;
	int i;

	if (c->preproc) {
		int ret;

		ret = c->preproc(ares, c->preproc_data);
		if (ret < 0) {
			c->error = ret;
			return AE_CTRL_TERMINATE;
		} else if (ret > 0) {
			return AE_OK;
		}
	}

	memset(&r, 0, sizeof(r));

	if (acpi_dev_resource_memory(ares, &r)
	    || acpi_dev_resource_io(ares, &r)
	    || acpi_dev_resource_address_space(ares, &r)
	    || acpi_dev_resource_ext_address_space(ares, &r))
		return acpi_dev_new_resource_entry(&r, c);

	for (i = 0; acpi_dev_resource_interrupt(ares, i, &r); i++) {
		acpi_status status;

		status = acpi_dev_new_resource_entry(&r, c);
		if (ACPI_FAILURE(status))
			return status;
	}

	return AE_OK;
}

/**
 * acpi_dev_get_resources - Get current resources of a device.
 * @adev: ACPI device node to get the resources for.
 * @list: Head of the resultant list of resources (must be empty).
 * @preproc: The caller's preprocessing routine.
 * @preproc_data: Pointer passed to the caller's preprocessing routine.
 *
 * Evaluate the _CRS method for the given device node and process its output by
 * (1) executing the @preproc() rountine provided by the caller, passing the
 * resource pointer and @preproc_data to it as arguments, for each ACPI resource
 * returned and (2) converting all of the returned ACPI resources into struct
 * resource objects if possible.  If the return value of @preproc() in step (1)
 * is different from 0, step (2) is not applied to the given ACPI resource and
 * if that value is negative, the whole processing is aborted and that value is
 * returned as the final error code.
 *
 * The resultant struct resource objects are put on the list pointed to by
 * @list, that must be empty initially, as members of struct resource_list_entry
 * objects.  Callers of this routine should use %acpi_dev_free_resource_list() to
 * free that list.
 *
 * The number of resources in the output list is returned on success, an error
 * code reflecting the error condition is returned otherwise.
 */
int acpi_dev_get_resources(struct acpi_device *adev, struct list_head *list,
			   int (*preproc)(struct acpi_resource *, void *),
			   void *preproc_data)
{
	struct res_proc_context c;
	acpi_status status;

	if (!adev || !adev->handle || !list_empty(list))
		return -EINVAL;

	if (!acpi_has_method(adev->handle, METHOD_NAME__CRS))
		return 0;

	c.list = list;
	c.preproc = preproc;
	c.preproc_data = preproc_data;
	c.count = 0;
	c.error = 0;
	status = acpi_walk_resources(adev->handle, METHOD_NAME__CRS,
				     acpi_dev_process_resource, &c);
	if (ACPI_FAILURE(status)) {
		acpi_dev_free_resource_list(list);
		return c.error ? c.error : -EIO;
	}

	return c.count;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_resources);
