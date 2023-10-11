// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/acpi/resource.c - ACPI device resources interpretation.
 *
 * Copyright (C) 2012, Intel Corp.
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/dmi.h>

#ifdef CONFIG_X86
#define valid_IRQ(i) (((i) != 0) && ((i) != 2))
static inline bool acpi_iospace_resource_valid(struct resource *res)
{
	/* On X86 IO space is limited to the [0 - 64K] IO port range */
	return res->end < 0x10003;
}
#else
#define valid_IRQ(i) (true)
/*
 * ACPI IO descriptors on arches other than X86 contain MMIO CPU physical
 * addresses mapping IO space in CPU physical address space, IO space
 * resources can be placed anywhere in the 64-bit physical address space.
 */
static inline bool
acpi_iospace_resource_valid(struct resource *res) { return true; }
#endif

#if IS_ENABLED(CONFIG_ACPI_GENERIC_GSI)
static inline bool is_gsi(struct acpi_resource_extended_irq *ext_irq)
{
	return ext_irq->resource_source.string_length == 0 &&
	       ext_irq->producer_consumer == ACPI_CONSUMER;
}
#else
static inline bool is_gsi(struct acpi_resource_extended_irq *ext_irq)
{
	return true;
}
#endif

static bool acpi_dev_resource_len_valid(u64 start, u64 end, u64 len, bool io)
{
	u64 reslen = end - start + 1;

	/*
	 * CHECKME: len might be required to check versus a minimum
	 * length as well. 1 for io is fine, but for memory it does
	 * not make any sense at all.
	 * Note: some BIOSes report incorrect length for ACPI address space
	 * descriptor, so remove check of 'reslen == len' to avoid regression.
	 */
	if (len && reslen && start <= end)
		return true;

	pr_debug("ACPI: invalid or unassigned resource %s [%016llx - %016llx] length [%016llx]\n",
		io ? "io" : "mem", start, end, len);

	return false;
}

static void acpi_dev_memresource_flags(struct resource *res, u64 len,
				       u8 write_protect)
{
	res->flags = IORESOURCE_MEM;

	if (!acpi_dev_resource_len_valid(res->start, res->end, len, false))
		res->flags |= IORESOURCE_DISABLED | IORESOURCE_UNSET;

	if (write_protect == ACPI_READ_WRITE_MEMORY)
		res->flags |= IORESOURCE_MEM_WRITEABLE;
}

static void acpi_dev_get_memresource(struct resource *res, u64 start, u64 len,
				     u8 write_protect)
{
	res->start = start;
	res->end = start + len - 1;
	acpi_dev_memresource_flags(res, len, write_protect);
}

/**
 * acpi_dev_resource_memory - Extract ACPI memory resource information.
 * @ares: Input ACPI resource object.
 * @res: Output generic resource object.
 *
 * Check if the given ACPI resource object represents a memory resource and
 * if that's the case, use the information in it to populate the generic
 * resource object pointed to by @res.
 *
 * Return:
 * 1) false with res->flags setting to zero: not the expected resource type
 * 2) false with IORESOURCE_DISABLED in res->flags: valid unassigned resource
 * 3) true: valid assigned resource
 */
bool acpi_dev_resource_memory(struct acpi_resource *ares, struct resource *res)
{
	struct acpi_resource_memory24 *memory24;
	struct acpi_resource_memory32 *memory32;
	struct acpi_resource_fixed_memory32 *fixed_memory32;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_MEMORY24:
		memory24 = &ares->data.memory24;
		acpi_dev_get_memresource(res, memory24->minimum << 8,
					 memory24->address_length << 8,
					 memory24->write_protect);
		break;
	case ACPI_RESOURCE_TYPE_MEMORY32:
		memory32 = &ares->data.memory32;
		acpi_dev_get_memresource(res, memory32->minimum,
					 memory32->address_length,
					 memory32->write_protect);
		break;
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		fixed_memory32 = &ares->data.fixed_memory32;
		acpi_dev_get_memresource(res, fixed_memory32->address,
					 fixed_memory32->address_length,
					 fixed_memory32->write_protect);
		break;
	default:
		res->flags = 0;
		return false;
	}

	return !(res->flags & IORESOURCE_DISABLED);
}
EXPORT_SYMBOL_GPL(acpi_dev_resource_memory);

static void acpi_dev_ioresource_flags(struct resource *res, u64 len,
				      u8 io_decode, u8 translation_type)
{
	res->flags = IORESOURCE_IO;

	if (!acpi_dev_resource_len_valid(res->start, res->end, len, true))
		res->flags |= IORESOURCE_DISABLED | IORESOURCE_UNSET;

	if (!acpi_iospace_resource_valid(res))
		res->flags |= IORESOURCE_DISABLED | IORESOURCE_UNSET;

	if (io_decode == ACPI_DECODE_16)
		res->flags |= IORESOURCE_IO_16BIT_ADDR;
	if (translation_type == ACPI_SPARSE_TRANSLATION)
		res->flags |= IORESOURCE_IO_SPARSE;
}

static void acpi_dev_get_ioresource(struct resource *res, u64 start, u64 len,
				    u8 io_decode)
{
	res->start = start;
	res->end = start + len - 1;
	acpi_dev_ioresource_flags(res, len, io_decode, 0);
}

/**
 * acpi_dev_resource_io - Extract ACPI I/O resource information.
 * @ares: Input ACPI resource object.
 * @res: Output generic resource object.
 *
 * Check if the given ACPI resource object represents an I/O resource and
 * if that's the case, use the information in it to populate the generic
 * resource object pointed to by @res.
 *
 * Return:
 * 1) false with res->flags setting to zero: not the expected resource type
 * 2) false with IORESOURCE_DISABLED in res->flags: valid unassigned resource
 * 3) true: valid assigned resource
 */
bool acpi_dev_resource_io(struct acpi_resource *ares, struct resource *res)
{
	struct acpi_resource_io *io;
	struct acpi_resource_fixed_io *fixed_io;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_IO:
		io = &ares->data.io;
		acpi_dev_get_ioresource(res, io->minimum,
					io->address_length,
					io->io_decode);
		break;
	case ACPI_RESOURCE_TYPE_FIXED_IO:
		fixed_io = &ares->data.fixed_io;
		acpi_dev_get_ioresource(res, fixed_io->address,
					fixed_io->address_length,
					ACPI_DECODE_10);
		break;
	default:
		res->flags = 0;
		return false;
	}

	return !(res->flags & IORESOURCE_DISABLED);
}
EXPORT_SYMBOL_GPL(acpi_dev_resource_io);

static bool acpi_decode_space(struct resource_win *win,
			      struct acpi_resource_address *addr,
			      struct acpi_address64_attribute *attr)
{
	u8 iodec = attr->granularity == 0xfff ? ACPI_DECODE_10 : ACPI_DECODE_16;
	bool wp = addr->info.mem.write_protect;
	u64 len = attr->address_length;
	u64 start, end, offset = 0;
	struct resource *res = &win->res;

	/*
	 * Filter out invalid descriptor according to ACPI Spec 5.0, section
	 * 6.4.3.5 Address Space Resource Descriptors.
	 */
	if ((addr->min_address_fixed != addr->max_address_fixed && len) ||
	    (addr->min_address_fixed && addr->max_address_fixed && !len))
		pr_debug("ACPI: Invalid address space min_addr_fix %d, max_addr_fix %d, len %llx\n",
			 addr->min_address_fixed, addr->max_address_fixed, len);

	/*
	 * For bridges that translate addresses across the bridge,
	 * translation_offset is the offset that must be added to the
	 * address on the secondary side to obtain the address on the
	 * primary side. Non-bridge devices must list 0 for all Address
	 * Translation offset bits.
	 */
	if (addr->producer_consumer == ACPI_PRODUCER)
		offset = attr->translation_offset;
	else if (attr->translation_offset)
		pr_debug("ACPI: translation_offset(%lld) is invalid for non-bridge device.\n",
			 attr->translation_offset);
	start = attr->minimum + offset;
	end = attr->maximum + offset;

	win->offset = offset;
	res->start = start;
	res->end = end;
	if (sizeof(resource_size_t) < sizeof(u64) &&
	    (offset != win->offset || start != res->start || end != res->end)) {
		pr_warn("acpi resource window ([%#llx-%#llx] ignored, not CPU addressable)\n",
			attr->minimum, attr->maximum);
		return false;
	}

	switch (addr->resource_type) {
	case ACPI_MEMORY_RANGE:
		acpi_dev_memresource_flags(res, len, wp);
		break;
	case ACPI_IO_RANGE:
		acpi_dev_ioresource_flags(res, len, iodec,
					  addr->info.io.translation_type);
		break;
	case ACPI_BUS_NUMBER_RANGE:
		res->flags = IORESOURCE_BUS;
		break;
	default:
		return false;
	}

	if (addr->producer_consumer == ACPI_PRODUCER)
		res->flags |= IORESOURCE_WINDOW;

	if (addr->info.mem.caching == ACPI_PREFETCHABLE_MEMORY)
		res->flags |= IORESOURCE_PREFETCH;

	return !(res->flags & IORESOURCE_DISABLED);
}

/**
 * acpi_dev_resource_address_space - Extract ACPI address space information.
 * @ares: Input ACPI resource object.
 * @win: Output generic resource object.
 *
 * Check if the given ACPI resource object represents an address space resource
 * and if that's the case, use the information in it to populate the generic
 * resource object pointed to by @win.
 *
 * Return:
 * 1) false with win->res.flags setting to zero: not the expected resource type
 * 2) false with IORESOURCE_DISABLED in win->res.flags: valid unassigned
 *    resource
 * 3) true: valid assigned resource
 */
bool acpi_dev_resource_address_space(struct acpi_resource *ares,
				     struct resource_win *win)
{
	struct acpi_resource_address64 addr;

	win->res.flags = 0;
	if (ACPI_FAILURE(acpi_resource_to_address64(ares, &addr)))
		return false;

	return acpi_decode_space(win, (struct acpi_resource_address *)&addr,
				 &addr.address);
}
EXPORT_SYMBOL_GPL(acpi_dev_resource_address_space);

/**
 * acpi_dev_resource_ext_address_space - Extract ACPI address space information.
 * @ares: Input ACPI resource object.
 * @win: Output generic resource object.
 *
 * Check if the given ACPI resource object represents an extended address space
 * resource and if that's the case, use the information in it to populate the
 * generic resource object pointed to by @win.
 *
 * Return:
 * 1) false with win->res.flags setting to zero: not the expected resource type
 * 2) false with IORESOURCE_DISABLED in win->res.flags: valid unassigned
 *    resource
 * 3) true: valid assigned resource
 */
bool acpi_dev_resource_ext_address_space(struct acpi_resource *ares,
					 struct resource_win *win)
{
	struct acpi_resource_extended_address64 *ext_addr;

	win->res.flags = 0;
	if (ares->type != ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64)
		return false;

	ext_addr = &ares->data.ext_address64;

	return acpi_decode_space(win, (struct acpi_resource_address *)ext_addr,
				 &ext_addr->address);
}
EXPORT_SYMBOL_GPL(acpi_dev_resource_ext_address_space);

/**
 * acpi_dev_irq_flags - Determine IRQ resource flags.
 * @triggering: Triggering type as provided by ACPI.
 * @polarity: Interrupt polarity as provided by ACPI.
 * @shareable: Whether or not the interrupt is shareable.
 * @wake_capable: Wake capability as provided by ACPI.
 */
unsigned long acpi_dev_irq_flags(u8 triggering, u8 polarity, u8 shareable, u8 wake_capable)
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

	if (wake_capable == ACPI_WAKE_CAPABLE)
		flags |= IORESOURCE_IRQ_WAKECAPABLE;

	return flags | IORESOURCE_IRQ;
}
EXPORT_SYMBOL_GPL(acpi_dev_irq_flags);

/**
 * acpi_dev_get_irq_type - Determine irq type.
 * @triggering: Triggering type as provided by ACPI.
 * @polarity: Interrupt polarity as provided by ACPI.
 */
unsigned int acpi_dev_get_irq_type(int triggering, int polarity)
{
	switch (polarity) {
	case ACPI_ACTIVE_LOW:
		return triggering == ACPI_EDGE_SENSITIVE ?
		       IRQ_TYPE_EDGE_FALLING :
		       IRQ_TYPE_LEVEL_LOW;
	case ACPI_ACTIVE_HIGH:
		return triggering == ACPI_EDGE_SENSITIVE ?
		       IRQ_TYPE_EDGE_RISING :
		       IRQ_TYPE_LEVEL_HIGH;
	case ACPI_ACTIVE_BOTH:
		if (triggering == ACPI_EDGE_SENSITIVE)
			return IRQ_TYPE_EDGE_BOTH;
		fallthrough;
	default:
		return IRQ_TYPE_NONE;
	}
}
EXPORT_SYMBOL_GPL(acpi_dev_get_irq_type);

static const struct dmi_system_id medion_laptop[] = {
	{
		.ident = "MEDION P15651",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDION"),
			DMI_MATCH(DMI_BOARD_NAME, "M15T"),
		},
	},
	{
		.ident = "MEDION S17405",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDION"),
			DMI_MATCH(DMI_BOARD_NAME, "M17T"),
		},
	},
	{
		.ident = "MEDION S17413",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDION"),
			DMI_MATCH(DMI_BOARD_NAME, "M1xA"),
		},
	},
	{ }
};

static const struct dmi_system_id asus_laptop[] = {
	{
		.ident = "Asus Vivobook K3402ZA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_BOARD_NAME, "K3402ZA"),
		},
	},
	{
		.ident = "Asus Vivobook K3502ZA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_BOARD_NAME, "K3502ZA"),
		},
	},
	{
		.ident = "Asus Vivobook S5402ZA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_BOARD_NAME, "S5402ZA"),
		},
	},
	{
		.ident = "Asus Vivobook S5602ZA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_BOARD_NAME, "S5602ZA"),
		},
	},
	{
		.ident = "Asus ExpertBook B1502CBA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_BOARD_NAME, "B1502CBA"),
		},
	},
	{
		.ident = "Asus ExpertBook B2402CBA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_BOARD_NAME, "B2402CBA"),
		},
	},
	{
		.ident = "Asus ExpertBook B2402FBA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_BOARD_NAME, "B2402FBA"),
		},
	},
	{
		.ident = "Asus ExpertBook B2502",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_BOARD_NAME, "B2502CBA"),
		},
	},
	{ }
};

static const struct dmi_system_id tongfang_gm_rg[] = {
	{
		.ident = "TongFang GMxRGxx/XMG CORE 15 (M22)/TUXEDO Stellaris 15 Gen4 AMD",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "GMxRGxx"),
		},
	},
	{ }
};

static const struct dmi_system_id maingear_laptop[] = {
	{
		.ident = "MAINGEAR Vector Pro 2 15",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro Electronics Inc"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MG-VCP2-15A3070T"),
		}
	},
	{
		.ident = "MAINGEAR Vector Pro 2 17",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro Electronics Inc"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MG-VCP2-17A3070T"),
		},
	},
	{ }
};

static const struct dmi_system_id pcspecialist_laptop[] = {
	{
		.ident = "PCSpecialist Elimina Pro 16 M",
		/*
		 * Some models have product-name "Elimina Pro 16 M",
		 * others "GM6BGEQ". Match on board-name to match both.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "PCSpecialist"),
			DMI_MATCH(DMI_BOARD_NAME, "GM6BGEQ"),
		},
	},
	{ }
};

static const struct dmi_system_id lg_laptop[] = {
	{
		.ident = "LG Electronics 17U70P",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LG Electronics"),
			DMI_MATCH(DMI_BOARD_NAME, "17U70P"),
		},
	},
	{ }
};

struct irq_override_cmp {
	const struct dmi_system_id *system;
	unsigned char irq;
	unsigned char triggering;
	unsigned char polarity;
	unsigned char shareable;
	bool override;
};

static const struct irq_override_cmp override_table[] = {
	{ medion_laptop, 1, ACPI_LEVEL_SENSITIVE, ACPI_ACTIVE_LOW, 0, false },
	{ asus_laptop, 1, ACPI_LEVEL_SENSITIVE, ACPI_ACTIVE_LOW, 0, false },
	{ tongfang_gm_rg, 1, ACPI_EDGE_SENSITIVE, ACPI_ACTIVE_LOW, 1, true },
	{ maingear_laptop, 1, ACPI_EDGE_SENSITIVE, ACPI_ACTIVE_LOW, 1, true },
	{ pcspecialist_laptop, 1, ACPI_EDGE_SENSITIVE, ACPI_ACTIVE_LOW, 1, true },
	{ lg_laptop, 1, ACPI_LEVEL_SENSITIVE, ACPI_ACTIVE_LOW, 0, false },
};

static bool acpi_dev_irq_override(u32 gsi, u8 triggering, u8 polarity,
				  u8 shareable)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(override_table); i++) {
		const struct irq_override_cmp *entry = &override_table[i];

		if (dmi_check_system(entry->system) &&
		    entry->irq == gsi &&
		    entry->triggering == triggering &&
		    entry->polarity == polarity &&
		    entry->shareable == shareable)
			return entry->override;
	}

#ifdef CONFIG_X86
	/*
	 * Always use the MADT override info, except for the i8042 PS/2 ctrl
	 * IRQs (1 and 12). For these the DSDT IRQ settings should sometimes
	 * be used otherwise PS/2 keyboards / mice will not work.
	 */
	if (gsi != 1 && gsi != 12)
		return true;

	/* If the override comes from an INT_SRC_OVR MADT entry, honor it. */
	if (acpi_int_src_ovr[gsi])
		return true;

	/*
	 * IRQ override isn't needed on modern AMD Zen systems and
	 * this override breaks active low IRQs on AMD Ryzen 6000 and
	 * newer systems. Skip it.
	 */
	if (boot_cpu_has(X86_FEATURE_ZEN))
		return false;
#endif

	return true;
}

static void acpi_dev_get_irqresource(struct resource *res, u32 gsi,
				     u8 triggering, u8 polarity, u8 shareable,
				     u8 wake_capable, bool check_override)
{
	int irq, p, t;

	if (!valid_IRQ(gsi)) {
		irqresource_disabled(res, gsi);
		return;
	}

	/*
	 * In IO-APIC mode, use overridden attribute. Two reasons:
	 * 1. BIOS bug in DSDT
	 * 2. BIOS uses IO-APIC mode Interrupt Source Override
	 *
	 * We do this only if we are dealing with IRQ() or IRQNoFlags()
	 * resource (the legacy ISA resources). With modern ACPI 5 devices
	 * using extended IRQ descriptors we take the IRQ configuration
	 * from _CRS directly.
	 */
	if (check_override &&
	    acpi_dev_irq_override(gsi, triggering, polarity, shareable) &&
	    !acpi_get_override_irq(gsi, &t, &p)) {
		u8 trig = t ? ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE;
		u8 pol = p ? ACPI_ACTIVE_LOW : ACPI_ACTIVE_HIGH;

		if (triggering != trig || polarity != pol) {
			pr_warn("ACPI: IRQ %d override to %s%s, %s%s\n", gsi,
				t ? "level" : "edge",
				trig == triggering ? "" : "(!)",
				p ? "low" : "high",
				pol == polarity ? "" : "(!)");
			triggering = trig;
			polarity = pol;
		}
	}

	res->flags = acpi_dev_irq_flags(triggering, polarity, shareable, wake_capable);
	irq = acpi_register_gsi(NULL, gsi, triggering, polarity);
	if (irq >= 0) {
		res->start = irq;
		res->end = irq;
	} else {
		irqresource_disabled(res, gsi);
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
 *
 * Return:
 * 1) false with res->flags setting to zero: not the expected resource type
 * 2) false with IORESOURCE_DISABLED in res->flags: valid unassigned resource
 * 3) true: valid assigned resource
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
			irqresource_disabled(res, 0);
			return false;
		}
		acpi_dev_get_irqresource(res, irq->interrupts[index],
					 irq->triggering, irq->polarity,
					 irq->shareable, irq->wake_capable,
					 true);
		break;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		ext_irq = &ares->data.extended_irq;
		if (index >= ext_irq->interrupt_count) {
			irqresource_disabled(res, 0);
			return false;
		}
		if (is_gsi(ext_irq))
			acpi_dev_get_irqresource(res, ext_irq->interrupts[index],
					 ext_irq->triggering, ext_irq->polarity,
					 ext_irq->shareable, ext_irq->wake_capable,
					 false);
		else
			irqresource_disabled(res, 0);
		break;
	default:
		res->flags = 0;
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
	resource_list_free(list);
}
EXPORT_SYMBOL_GPL(acpi_dev_free_resource_list);

struct res_proc_context {
	struct list_head *list;
	int (*preproc)(struct acpi_resource *, void *);
	void *preproc_data;
	int count;
	int error;
};

static acpi_status acpi_dev_new_resource_entry(struct resource_win *win,
					       struct res_proc_context *c)
{
	struct resource_entry *rentry;

	rentry = resource_list_create_entry(NULL, 0);
	if (!rentry) {
		c->error = -ENOMEM;
		return AE_NO_MEMORY;
	}
	*rentry->res = win->res;
	rentry->offset = win->offset;
	resource_list_add_tail(rentry, c->list);
	c->count++;
	return AE_OK;
}

static acpi_status acpi_dev_process_resource(struct acpi_resource *ares,
					     void *context)
{
	struct res_proc_context *c = context;
	struct resource_win win;
	struct resource *res = &win.res;
	int i;

	if (c->preproc) {
		int ret;

		ret = c->preproc(ares, c->preproc_data);
		if (ret < 0) {
			c->error = ret;
			return AE_ABORT_METHOD;
		} else if (ret > 0) {
			return AE_OK;
		}
	}

	memset(&win, 0, sizeof(win));

	if (acpi_dev_resource_memory(ares, res)
	    || acpi_dev_resource_io(ares, res)
	    || acpi_dev_resource_address_space(ares, &win)
	    || acpi_dev_resource_ext_address_space(ares, &win))
		return acpi_dev_new_resource_entry(&win, c);

	for (i = 0; acpi_dev_resource_interrupt(ares, i, res); i++) {
		acpi_status status;

		status = acpi_dev_new_resource_entry(&win, c);
		if (ACPI_FAILURE(status))
			return status;
	}

	return AE_OK;
}

static int __acpi_dev_get_resources(struct acpi_device *adev,
				    struct list_head *list,
				    int (*preproc)(struct acpi_resource *, void *),
				    void *preproc_data, char *method)
{
	struct res_proc_context c;
	acpi_status status;

	if (!adev || !adev->handle || !list_empty(list))
		return -EINVAL;

	if (!acpi_has_method(adev->handle, method))
		return 0;

	c.list = list;
	c.preproc = preproc;
	c.preproc_data = preproc_data;
	c.count = 0;
	c.error = 0;
	status = acpi_walk_resources(adev->handle, method,
				     acpi_dev_process_resource, &c);
	if (ACPI_FAILURE(status)) {
		acpi_dev_free_resource_list(list);
		return c.error ? c.error : -EIO;
	}

	return c.count;
}

/**
 * acpi_dev_get_resources - Get current resources of a device.
 * @adev: ACPI device node to get the resources for.
 * @list: Head of the resultant list of resources (must be empty).
 * @preproc: The caller's preprocessing routine.
 * @preproc_data: Pointer passed to the caller's preprocessing routine.
 *
 * Evaluate the _CRS method for the given device node and process its output by
 * (1) executing the @preproc() routine provided by the caller, passing the
 * resource pointer and @preproc_data to it as arguments, for each ACPI resource
 * returned and (2) converting all of the returned ACPI resources into struct
 * resource objects if possible.  If the return value of @preproc() in step (1)
 * is different from 0, step (2) is not applied to the given ACPI resource and
 * if that value is negative, the whole processing is aborted and that value is
 * returned as the final error code.
 *
 * The resultant struct resource objects are put on the list pointed to by
 * @list, that must be empty initially, as members of struct resource_entry
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
	return __acpi_dev_get_resources(adev, list, preproc, preproc_data,
					METHOD_NAME__CRS);
}
EXPORT_SYMBOL_GPL(acpi_dev_get_resources);

static int is_memory(struct acpi_resource *ares, void *not_used)
{
	struct resource_win win;
	struct resource *res = &win.res;

	memset(&win, 0, sizeof(win));

	if (acpi_dev_filter_resource_type(ares, IORESOURCE_MEM))
		return 1;

	return !(acpi_dev_resource_memory(ares, res)
	       || acpi_dev_resource_address_space(ares, &win)
	       || acpi_dev_resource_ext_address_space(ares, &win));
}

/**
 * acpi_dev_get_dma_resources - Get current DMA resources of a device.
 * @adev: ACPI device node to get the resources for.
 * @list: Head of the resultant list of resources (must be empty).
 *
 * Evaluate the _DMA method for the given device node and process its
 * output.
 *
 * The resultant struct resource objects are put on the list pointed to
 * by @list, that must be empty initially, as members of struct
 * resource_entry objects.  Callers of this routine should use
 * %acpi_dev_free_resource_list() to free that list.
 *
 * The number of resources in the output list is returned on success,
 * an error code reflecting the error condition is returned otherwise.
 */
int acpi_dev_get_dma_resources(struct acpi_device *adev, struct list_head *list)
{
	return __acpi_dev_get_resources(adev, list, is_memory, NULL,
					METHOD_NAME__DMA);
}
EXPORT_SYMBOL_GPL(acpi_dev_get_dma_resources);

/**
 * acpi_dev_get_memory_resources - Get current memory resources of a device.
 * @adev: ACPI device node to get the resources for.
 * @list: Head of the resultant list of resources (must be empty).
 *
 * This is a helper function that locates all memory type resources of @adev
 * with acpi_dev_get_resources().
 *
 * The number of resources in the output list is returned on success, an error
 * code reflecting the error condition is returned otherwise.
 */
int acpi_dev_get_memory_resources(struct acpi_device *adev, struct list_head *list)
{
	return acpi_dev_get_resources(adev, list, is_memory, NULL);
}
EXPORT_SYMBOL_GPL(acpi_dev_get_memory_resources);

/**
 * acpi_dev_filter_resource_type - Filter ACPI resource according to resource
 *				   types
 * @ares: Input ACPI resource object.
 * @types: Valid resource types of IORESOURCE_XXX
 *
 * This is a helper function to support acpi_dev_get_resources(), which filters
 * ACPI resource objects according to resource types.
 */
int acpi_dev_filter_resource_type(struct acpi_resource *ares,
				  unsigned long types)
{
	unsigned long type = 0;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_MEMORY24:
	case ACPI_RESOURCE_TYPE_MEMORY32:
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		type = IORESOURCE_MEM;
		break;
	case ACPI_RESOURCE_TYPE_IO:
	case ACPI_RESOURCE_TYPE_FIXED_IO:
		type = IORESOURCE_IO;
		break;
	case ACPI_RESOURCE_TYPE_IRQ:
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		type = IORESOURCE_IRQ;
		break;
	case ACPI_RESOURCE_TYPE_DMA:
	case ACPI_RESOURCE_TYPE_FIXED_DMA:
		type = IORESOURCE_DMA;
		break;
	case ACPI_RESOURCE_TYPE_GENERIC_REGISTER:
		type = IORESOURCE_REG;
		break;
	case ACPI_RESOURCE_TYPE_ADDRESS16:
	case ACPI_RESOURCE_TYPE_ADDRESS32:
	case ACPI_RESOURCE_TYPE_ADDRESS64:
	case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
		if (ares->data.address.resource_type == ACPI_MEMORY_RANGE)
			type = IORESOURCE_MEM;
		else if (ares->data.address.resource_type == ACPI_IO_RANGE)
			type = IORESOURCE_IO;
		else if (ares->data.address.resource_type ==
			 ACPI_BUS_NUMBER_RANGE)
			type = IORESOURCE_BUS;
		break;
	default:
		break;
	}

	return (type & types) ? 0 : 1;
}
EXPORT_SYMBOL_GPL(acpi_dev_filter_resource_type);

static int acpi_dev_consumes_res(struct acpi_device *adev, struct resource *res)
{
	struct list_head resource_list;
	struct resource_entry *rentry;
	int ret, found = 0;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, NULL, NULL);
	if (ret < 0)
		return 0;

	list_for_each_entry(rentry, &resource_list, node) {
		if (resource_contains(rentry->res, res)) {
			found = 1;
			break;
		}

	}

	acpi_dev_free_resource_list(&resource_list);
	return found;
}

static acpi_status acpi_res_consumer_cb(acpi_handle handle, u32 depth,
					 void *context, void **ret)
{
	struct resource *res = context;
	struct acpi_device **consumer = (struct acpi_device **) ret;
	struct acpi_device *adev = acpi_fetch_acpi_dev(handle);

	if (!adev)
		return AE_OK;

	if (acpi_dev_consumes_res(adev, res)) {
		*consumer = adev;
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

/**
 * acpi_resource_consumer - Find the ACPI device that consumes @res.
 * @res: Resource to search for.
 *
 * Search the current resource settings (_CRS) of every ACPI device node
 * for @res.  If we find an ACPI device whose _CRS includes @res, return
 * it.  Otherwise, return NULL.
 */
struct acpi_device *acpi_resource_consumer(struct resource *res)
{
	struct acpi_device *consumer = NULL;

	acpi_get_devices(NULL, acpi_res_consumer_cb, res, (void **) &consumer);
	return consumer;
}
