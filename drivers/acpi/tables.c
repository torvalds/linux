/*
 *  acpi_tables.c - ACPI Boot-Time Table Parsing
 *
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

/* Uncomment next line to get verbose printout */
/* #define DEBUG */
#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/earlycpio.h>
#include <linux/memblock.h>
#include <linux/initrd.h>
#include "internal.h"

#ifdef CONFIG_ACPI_CUSTOM_DSDT
#include CONFIG_ACPI_CUSTOM_DSDT_FILE
#endif

#define ACPI_MAX_TABLES		128

static char *mps_inti_flags_polarity[] = { "dfl", "high", "res", "low" };
static char *mps_inti_flags_trigger[] = { "dfl", "edge", "res", "level" };

static struct acpi_table_desc initial_tables[ACPI_MAX_TABLES] __initdata;

static int acpi_apic_instance __initdata;

/*
 * Disable table checksum verification for the early stage due to the size
 * limitation of the current x86 early mapping implementation.
 */
static bool acpi_verify_table_checksum __initdata = false;

void acpi_table_print_madt_entry(struct acpi_subtable_header *header)
{
	if (!header)
		return;

	switch (header->type) {

	case ACPI_MADT_TYPE_LOCAL_APIC:
		{
			struct acpi_madt_local_apic *p =
			    (struct acpi_madt_local_apic *)header;
			pr_debug("LAPIC (acpi_id[0x%02x] lapic_id[0x%02x] %s)\n",
				 p->processor_id, p->id,
				 (p->lapic_flags & ACPI_MADT_ENABLED) ? "enabled" : "disabled");
		}
		break;

	case ACPI_MADT_TYPE_LOCAL_X2APIC:
		{
			struct acpi_madt_local_x2apic *p =
			    (struct acpi_madt_local_x2apic *)header;
			pr_debug("X2APIC (apic_id[0x%02x] uid[0x%02x] %s)\n",
				 p->local_apic_id, p->uid,
				 (p->lapic_flags & ACPI_MADT_ENABLED) ? "enabled" : "disabled");
		}
		break;

	case ACPI_MADT_TYPE_IO_APIC:
		{
			struct acpi_madt_io_apic *p =
			    (struct acpi_madt_io_apic *)header;
			pr_debug("IOAPIC (id[0x%02x] address[0x%08x] gsi_base[%d])\n",
				 p->id, p->address, p->global_irq_base);
		}
		break;

	case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
		{
			struct acpi_madt_interrupt_override *p =
			    (struct acpi_madt_interrupt_override *)header;
			pr_info("INT_SRC_OVR (bus %d bus_irq %d global_irq %d %s %s)\n",
				p->bus, p->source_irq, p->global_irq,
				mps_inti_flags_polarity[p->inti_flags & ACPI_MADT_POLARITY_MASK],
				mps_inti_flags_trigger[(p->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2]);
			if (p->inti_flags  &
			    ~(ACPI_MADT_POLARITY_MASK | ACPI_MADT_TRIGGER_MASK))
				pr_info("INT_SRC_OVR unexpected reserved flags: 0x%x\n",
					p->inti_flags  &
					~(ACPI_MADT_POLARITY_MASK | ACPI_MADT_TRIGGER_MASK));
		}
		break;

	case ACPI_MADT_TYPE_NMI_SOURCE:
		{
			struct acpi_madt_nmi_source *p =
			    (struct acpi_madt_nmi_source *)header;
			pr_info("NMI_SRC (%s %s global_irq %d)\n",
				mps_inti_flags_polarity[p->inti_flags & ACPI_MADT_POLARITY_MASK],
				mps_inti_flags_trigger[(p->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2],
				p->global_irq);
		}
		break;

	case ACPI_MADT_TYPE_LOCAL_APIC_NMI:
		{
			struct acpi_madt_local_apic_nmi *p =
			    (struct acpi_madt_local_apic_nmi *)header;
			pr_info("LAPIC_NMI (acpi_id[0x%02x] %s %s lint[0x%x])\n",
				p->processor_id,
				mps_inti_flags_polarity[p->inti_flags & ACPI_MADT_POLARITY_MASK	],
				mps_inti_flags_trigger[(p->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2],
				p->lint);
		}
		break;

	case ACPI_MADT_TYPE_LOCAL_X2APIC_NMI:
		{
			u16 polarity, trigger;
			struct acpi_madt_local_x2apic_nmi *p =
			    (struct acpi_madt_local_x2apic_nmi *)header;

			polarity = p->inti_flags & ACPI_MADT_POLARITY_MASK;
			trigger = (p->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2;

			pr_info("X2APIC_NMI (uid[0x%02x] %s %s lint[0x%x])\n",
				p->uid,
				mps_inti_flags_polarity[polarity],
				mps_inti_flags_trigger[trigger],
				p->lint);
		}
		break;

	case ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE:
		{
			struct acpi_madt_local_apic_override *p =
			    (struct acpi_madt_local_apic_override *)header;
			pr_info("LAPIC_ADDR_OVR (address[%p])\n",
				(void *)(unsigned long)p->address);
		}
		break;

	case ACPI_MADT_TYPE_IO_SAPIC:
		{
			struct acpi_madt_io_sapic *p =
			    (struct acpi_madt_io_sapic *)header;
			pr_debug("IOSAPIC (id[0x%x] address[%p] gsi_base[%d])\n",
				 p->id, (void *)(unsigned long)p->address,
				 p->global_irq_base);
		}
		break;

	case ACPI_MADT_TYPE_LOCAL_SAPIC:
		{
			struct acpi_madt_local_sapic *p =
			    (struct acpi_madt_local_sapic *)header;
			pr_debug("LSAPIC (acpi_id[0x%02x] lsapic_id[0x%02x] lsapic_eid[0x%02x] %s)\n",
				 p->processor_id, p->id, p->eid,
				 (p->lapic_flags & ACPI_MADT_ENABLED) ? "enabled" : "disabled");
		}
		break;

	case ACPI_MADT_TYPE_INTERRUPT_SOURCE:
		{
			struct acpi_madt_interrupt_source *p =
			    (struct acpi_madt_interrupt_source *)header;
			pr_info("PLAT_INT_SRC (%s %s type[0x%x] id[0x%04x] eid[0x%x] iosapic_vector[0x%x] global_irq[0x%x]\n",
				mps_inti_flags_polarity[p->inti_flags & ACPI_MADT_POLARITY_MASK],
				mps_inti_flags_trigger[(p->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2],
				p->type, p->id, p->eid, p->io_sapic_vector,
				p->global_irq);
		}
		break;

	case ACPI_MADT_TYPE_GENERIC_INTERRUPT:
		{
			struct acpi_madt_generic_interrupt *p =
				(struct acpi_madt_generic_interrupt *)header;
			pr_debug("GICC (acpi_id[0x%04x] address[%llx] MPIDR[0x%llx] %s)\n",
				 p->uid, p->base_address,
				 p->arm_mpidr,
				 (p->flags & ACPI_MADT_ENABLED) ? "enabled" : "disabled");

		}
		break;

	case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:
		{
			struct acpi_madt_generic_distributor *p =
				(struct acpi_madt_generic_distributor *)header;
			pr_debug("GIC Distributor (gic_id[0x%04x] address[%llx] gsi_base[%d])\n",
				 p->gic_id, p->base_address,
				 p->global_irq_base);
		}
		break;

	default:
		pr_warn("Found unsupported MADT entry (type = 0x%x)\n",
			header->type);
		break;
	}
}

/**
 * acpi_parse_entries_array - for each proc_num find a suitable subtable
 *
 * @id: table id (for debugging purposes)
 * @table_size: single entry size
 * @table_header: where does the table start?
 * @proc: array of acpi_subtable_proc struct containing entry id
 *        and associated handler with it
 * @proc_num: how big proc is?
 * @max_entries: how many entries can we process?
 *
 * For each proc_num find a subtable with proc->id and run proc->handler
 * on it. Assumption is that there's only single handler for particular
 * entry id.
 *
 * On success returns sum of all matching entries for all proc handlers.
 * Otherwise, -ENODEV or -EINVAL is returned.
 */
static int __init
acpi_parse_entries_array(char *id, unsigned long table_size,
		struct acpi_table_header *table_header,
		struct acpi_subtable_proc *proc, int proc_num,
		unsigned int max_entries)
{
	struct acpi_subtable_header *entry;
	unsigned long table_end;
	int count = 0;
	int errs = 0;
	int i;

	if (acpi_disabled)
		return -ENODEV;

	if (!id)
		return -EINVAL;

	if (!table_size)
		return -EINVAL;

	if (!table_header) {
		pr_warn("%4.4s not present\n", id);
		return -ENODEV;
	}

	table_end = (unsigned long)table_header + table_header->length;

	/* Parse all entries looking for a match. */

	entry = (struct acpi_subtable_header *)
	    ((unsigned long)table_header + table_size);

	while (((unsigned long)entry) + sizeof(struct acpi_subtable_header) <
	       table_end) {
		if (max_entries && count >= max_entries)
			break;

		for (i = 0; i < proc_num; i++) {
			if (entry->type != proc[i].id)
				continue;
			if (!proc[i].handler ||
			     (!errs && proc[i].handler(entry, table_end))) {
				errs++;
				continue;
			}

			proc[i].count++;
			break;
		}
		if (i != proc_num)
			count++;

		/*
		 * If entry->length is 0, break from this loop to avoid
		 * infinite loop.
		 */
		if (entry->length == 0) {
			pr_err("[%4.4s:0x%02x] Invalid zero length\n", id, proc->id);
			return -EINVAL;
		}

		entry = (struct acpi_subtable_header *)
		    ((unsigned long)entry + entry->length);
	}

	if (max_entries && count > max_entries) {
		pr_warn("[%4.4s:0x%02x] found the maximum %i entries\n",
			id, proc->id, count);
	}

	return errs ? -EINVAL : count;
}

int __init
acpi_parse_entries(char *id,
			unsigned long table_size,
			acpi_tbl_entry_handler handler,
			struct acpi_table_header *table_header,
			int entry_id, unsigned int max_entries)
{
	struct acpi_subtable_proc proc = {
		.id		= entry_id,
		.handler	= handler,
	};

	return acpi_parse_entries_array(id, table_size, table_header,
			&proc, 1, max_entries);
}

int __init
acpi_table_parse_entries_array(char *id,
			 unsigned long table_size,
			 struct acpi_subtable_proc *proc, int proc_num,
			 unsigned int max_entries)
{
	struct acpi_table_header *table_header = NULL;
	acpi_size tbl_size;
	int count;
	u32 instance = 0;

	if (acpi_disabled)
		return -ENODEV;

	if (!id)
		return -EINVAL;

	if (!strncmp(id, ACPI_SIG_MADT, 4))
		instance = acpi_apic_instance;

	acpi_get_table_with_size(id, instance, &table_header, &tbl_size);
	if (!table_header) {
		pr_warn("%4.4s not present\n", id);
		return -ENODEV;
	}

	count = acpi_parse_entries_array(id, table_size, table_header,
			proc, proc_num, max_entries);

	early_acpi_os_unmap_memory((char *)table_header, tbl_size);
	return count;
}

int __init
acpi_table_parse_entries(char *id,
			unsigned long table_size,
			int entry_id,
			acpi_tbl_entry_handler handler,
			unsigned int max_entries)
{
	struct acpi_subtable_proc proc = {
		.id		= entry_id,
		.handler	= handler,
	};

	return acpi_table_parse_entries_array(id, table_size, &proc, 1,
						max_entries);
}

int __init
acpi_table_parse_madt(enum acpi_madt_type id,
		      acpi_tbl_entry_handler handler, unsigned int max_entries)
{
	return acpi_table_parse_entries(ACPI_SIG_MADT,
					    sizeof(struct acpi_table_madt), id,
					    handler, max_entries);
}

/**
 * acpi_table_parse - find table with @id, run @handler on it
 * @id: table id to find
 * @handler: handler to run
 *
 * Scan the ACPI System Descriptor Table (STD) for a table matching @id,
 * run @handler on it.
 *
 * Return 0 if table found, -errno if not.
 */
int __init acpi_table_parse(char *id, acpi_tbl_table_handler handler)
{
	struct acpi_table_header *table = NULL;
	acpi_size tbl_size;

	if (acpi_disabled)
		return -ENODEV;

	if (!id || !handler)
		return -EINVAL;

	if (strncmp(id, ACPI_SIG_MADT, 4) == 0)
		acpi_get_table_with_size(id, acpi_apic_instance, &table, &tbl_size);
	else
		acpi_get_table_with_size(id, 0, &table, &tbl_size);

	if (table) {
		handler(table);
		early_acpi_os_unmap_memory(table, tbl_size);
		return 0;
	} else
		return -ENODEV;
}

/* 
 * The BIOS is supposed to supply a single APIC/MADT,
 * but some report two.  Provide a knob to use either.
 * (don't you wish instance 0 and 1 were not the same?)
 */
static void __init check_multiple_madt(void)
{
	struct acpi_table_header *table = NULL;
	acpi_size tbl_size;

	acpi_get_table_with_size(ACPI_SIG_MADT, 2, &table, &tbl_size);
	if (table) {
		pr_warn("BIOS bug: multiple APIC/MADT found, using %d\n",
			acpi_apic_instance);
		pr_warn("If \"acpi_apic_instance=%d\" works better, "
			"notify linux-acpi@vger.kernel.org\n",
			acpi_apic_instance ? 0 : 2);
		early_acpi_os_unmap_memory(table, tbl_size);

	} else
		acpi_apic_instance = 0;

	return;
}

static void acpi_table_taint(struct acpi_table_header *table)
{
	pr_warn("Override [%4.4s-%8.8s], this is unsafe: tainting kernel\n",
		table->signature, table->oem_table_id);
	add_taint(TAINT_OVERRIDDEN_ACPI_TABLE, LOCKDEP_NOW_UNRELIABLE);
}

#ifdef CONFIG_ACPI_TABLE_UPGRADE
static u64 acpi_tables_addr;
static int all_tables_size;

/* Copied from acpica/tbutils.c:acpi_tb_checksum() */
static u8 __init acpi_table_checksum(u8 *buffer, u32 length)
{
	u8 sum = 0;
	u8 *end = buffer + length;

	while (buffer < end)
		sum = (u8) (sum + *(buffer++));
	return sum;
}

/* All but ACPI_SIG_RSDP and ACPI_SIG_FACS: */
static const char * const table_sigs[] = {
	ACPI_SIG_BERT, ACPI_SIG_CPEP, ACPI_SIG_ECDT, ACPI_SIG_EINJ,
	ACPI_SIG_ERST, ACPI_SIG_HEST, ACPI_SIG_MADT, ACPI_SIG_MSCT,
	ACPI_SIG_SBST, ACPI_SIG_SLIT, ACPI_SIG_SRAT, ACPI_SIG_ASF,
	ACPI_SIG_BOOT, ACPI_SIG_DBGP, ACPI_SIG_DMAR, ACPI_SIG_HPET,
	ACPI_SIG_IBFT, ACPI_SIG_IVRS, ACPI_SIG_MCFG, ACPI_SIG_MCHI,
	ACPI_SIG_SLIC, ACPI_SIG_SPCR, ACPI_SIG_SPMI, ACPI_SIG_TCPA,
	ACPI_SIG_UEFI, ACPI_SIG_WAET, ACPI_SIG_WDAT, ACPI_SIG_WDDT,
	ACPI_SIG_WDRT, ACPI_SIG_DSDT, ACPI_SIG_FADT, ACPI_SIG_PSDT,
	ACPI_SIG_RSDT, ACPI_SIG_XSDT, ACPI_SIG_SSDT, NULL };

#define ACPI_HEADER_SIZE sizeof(struct acpi_table_header)

#define NR_ACPI_INITRD_TABLES 64
static struct cpio_data __initdata acpi_initrd_files[NR_ACPI_INITRD_TABLES];
static DECLARE_BITMAP(acpi_initrd_installed, NR_ACPI_INITRD_TABLES);

#define MAP_CHUNK_SIZE   (NR_FIX_BTMAPS << PAGE_SHIFT)

void __init acpi_table_upgrade(void)
{
	void *data = (void *)initrd_start;
	size_t size = initrd_end - initrd_start;
	int sig, no, table_nr = 0, total_offset = 0;
	long offset = 0;
	struct acpi_table_header *table;
	char cpio_path[32] = "kernel/firmware/acpi/";
	struct cpio_data file;

	if (data == NULL || size == 0)
		return;

	for (no = 0; no < NR_ACPI_INITRD_TABLES; no++) {
		file = find_cpio_data(cpio_path, data, size, &offset);
		if (!file.data)
			break;

		data += offset;
		size -= offset;

		if (file.size < sizeof(struct acpi_table_header)) {
			pr_err("ACPI OVERRIDE: Table smaller than ACPI header [%s%s]\n",
				cpio_path, file.name);
			continue;
		}

		table = file.data;

		for (sig = 0; table_sigs[sig]; sig++)
			if (!memcmp(table->signature, table_sigs[sig], 4))
				break;

		if (!table_sigs[sig]) {
			pr_err("ACPI OVERRIDE: Unknown signature [%s%s]\n",
				cpio_path, file.name);
			continue;
		}
		if (file.size != table->length) {
			pr_err("ACPI OVERRIDE: File length does not match table length [%s%s]\n",
				cpio_path, file.name);
			continue;
		}
		if (acpi_table_checksum(file.data, table->length)) {
			pr_err("ACPI OVERRIDE: Bad table checksum [%s%s]\n",
				cpio_path, file.name);
			continue;
		}

		pr_info("%4.4s ACPI table found in initrd [%s%s][0x%x]\n",
			table->signature, cpio_path, file.name, table->length);

		all_tables_size += table->length;
		acpi_initrd_files[table_nr].data = file.data;
		acpi_initrd_files[table_nr].size = file.size;
		table_nr++;
	}
	if (table_nr == 0)
		return;

	acpi_tables_addr =
		memblock_find_in_range(0, ACPI_TABLE_UPGRADE_MAX_PHYS,
				       all_tables_size, PAGE_SIZE);
	if (!acpi_tables_addr) {
		WARN_ON(1);
		return;
	}
	/*
	 * Only calling e820_add_reserve does not work and the
	 * tables are invalid (memory got used) later.
	 * memblock_reserve works as expected and the tables won't get modified.
	 * But it's not enough on X86 because ioremap will
	 * complain later (used by acpi_os_map_memory) that the pages
	 * that should get mapped are not marked "reserved".
	 * Both memblock_reserve and e820_add_region (via arch_reserve_mem_area)
	 * works fine.
	 */
	memblock_reserve(acpi_tables_addr, all_tables_size);
	arch_reserve_mem_area(acpi_tables_addr, all_tables_size);

	/*
	 * early_ioremap only can remap 256k one time. If we map all
	 * tables one time, we will hit the limit. Need to map chunks
	 * one by one during copying the same as that in relocate_initrd().
	 */
	for (no = 0; no < table_nr; no++) {
		unsigned char *src_p = acpi_initrd_files[no].data;
		phys_addr_t size = acpi_initrd_files[no].size;
		phys_addr_t dest_addr = acpi_tables_addr + total_offset;
		phys_addr_t slop, clen;
		char *dest_p;

		total_offset += size;

		while (size) {
			slop = dest_addr & ~PAGE_MASK;
			clen = size;
			if (clen > MAP_CHUNK_SIZE - slop)
				clen = MAP_CHUNK_SIZE - slop;
			dest_p = early_memremap(dest_addr & PAGE_MASK,
						clen + slop);
			memcpy(dest_p + slop, src_p, clen);
			early_memunmap(dest_p, clen + slop);
			src_p += clen;
			dest_addr += clen;
			size -= clen;
		}
	}
}

static acpi_status
acpi_table_initrd_override(struct acpi_table_header *existing_table,
			   acpi_physical_address *address, u32 *length)
{
	int table_offset = 0;
	int table_index = 0;
	struct acpi_table_header *table;
	u32 table_length;

	*length = 0;
	*address = 0;
	if (!acpi_tables_addr)
		return AE_OK;

	while (table_offset + ACPI_HEADER_SIZE <= all_tables_size) {
		table = acpi_os_map_memory(acpi_tables_addr + table_offset,
					   ACPI_HEADER_SIZE);
		if (table_offset + table->length > all_tables_size) {
			acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
			WARN_ON(1);
			return AE_OK;
		}

		table_length = table->length;

		/* Only override tables matched */
		if (memcmp(existing_table->signature, table->signature, 4) ||
		    memcmp(table->oem_id, existing_table->oem_id,
			   ACPI_OEM_ID_SIZE) ||
		    memcmp(table->oem_table_id, existing_table->oem_table_id,
			   ACPI_OEM_TABLE_ID_SIZE)) {
			acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
			goto next_table;
		}
		/*
		 * Mark the table to avoid being used in
		 * acpi_table_initrd_scan() and check the revision.
		 */
		if (test_and_set_bit(table_index, acpi_initrd_installed) ||
		    existing_table->oem_revision >= table->oem_revision) {
			acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
			goto next_table;
		}

		*length = table_length;
		*address = acpi_tables_addr + table_offset;
		pr_info("Table Upgrade: override [%4.4s-%6.6s-%8.8s]\n",
			table->signature, table->oem_id,
			table->oem_table_id);
		acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
		break;

next_table:
		table_offset += table_length;
		table_index++;
	}
	return AE_OK;
}

static void __init acpi_table_initrd_scan(void)
{
	int table_offset = 0;
	int table_index = 0;
	u32 table_length;
	struct acpi_table_header *table;

	if (!acpi_tables_addr)
		return;

	while (table_offset + ACPI_HEADER_SIZE <= all_tables_size) {
		table = acpi_os_map_memory(acpi_tables_addr + table_offset,
					   ACPI_HEADER_SIZE);
		if (table_offset + table->length > all_tables_size) {
			acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
			WARN_ON(1);
			return;
		}

		table_length = table->length;

		/* Skip RSDT/XSDT which should only be used for override */
		if (ACPI_COMPARE_NAME(table->signature, ACPI_SIG_RSDT) ||
		    ACPI_COMPARE_NAME(table->signature, ACPI_SIG_XSDT)) {
			acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
			goto next_table;
		}
		/*
		 * Mark the table to avoid being used in
		 * acpi_table_initrd_override(). Though this is not possible
		 * because override is disabled in acpi_install_table().
		 */
		if (test_and_set_bit(table_index, acpi_initrd_installed)) {
			acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
			goto next_table;
		}

		pr_info("Table Upgrade: install [%4.4s-%6.6s-%8.8s]\n",
			table->signature, table->oem_id,
			table->oem_table_id);
		acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
		acpi_install_table(acpi_tables_addr + table_offset, TRUE);
next_table:
		table_offset += table_length;
		table_index++;
	}
}
#else
static acpi_status
acpi_table_initrd_override(struct acpi_table_header *existing_table,
			   acpi_physical_address *address,
			   u32 *table_length)
{
	*table_length = 0;
	*address = 0;
	return AE_OK;
}

static void __init acpi_table_initrd_scan(void)
{
}
#endif /* CONFIG_ACPI_TABLE_UPGRADE */

acpi_status
acpi_os_physical_table_override(struct acpi_table_header *existing_table,
				acpi_physical_address *address,
				u32 *table_length)
{
	return acpi_table_initrd_override(existing_table, address,
					  table_length);
}

acpi_status
acpi_os_table_override(struct acpi_table_header *existing_table,
		       struct acpi_table_header **new_table)
{
	if (!existing_table || !new_table)
		return AE_BAD_PARAMETER;

	*new_table = NULL;

#ifdef CONFIG_ACPI_CUSTOM_DSDT
	if (strncmp(existing_table->signature, "DSDT", 4) == 0)
		*new_table = (struct acpi_table_header *)AmlCode;
#endif
	if (*new_table != NULL)
		acpi_table_taint(existing_table);
	return AE_OK;
}

/*
 * acpi_table_init()
 *
 * find RSDP, find and checksum SDT/XSDT.
 * checksum all tables, print SDT/XSDT
 *
 * result: sdt_entry[] is initialized
 */

int __init acpi_table_init(void)
{
	acpi_status status;

	if (acpi_verify_table_checksum) {
		pr_info("Early table checksum verification enabled\n");
		acpi_gbl_verify_table_checksum = TRUE;
	} else {
		pr_info("Early table checksum verification disabled\n");
		acpi_gbl_verify_table_checksum = FALSE;
	}

	status = acpi_initialize_tables(initial_tables, ACPI_MAX_TABLES, 0);
	if (ACPI_FAILURE(status))
		return -EINVAL;
	acpi_table_initrd_scan();

	check_multiple_madt();
	return 0;
}

static int __init acpi_parse_apic_instance(char *str)
{
	if (!str)
		return -EINVAL;

	if (kstrtoint(str, 0, &acpi_apic_instance))
		return -EINVAL;

	pr_notice("Shall use APIC/MADT table %d\n", acpi_apic_instance);

	return 0;
}

early_param("acpi_apic_instance", acpi_parse_apic_instance);

static int __init acpi_force_table_verification_setup(char *s)
{
	acpi_verify_table_checksum = true;

	return 0;
}

early_param("acpi_force_table_verification", acpi_force_table_verification_setup);

static int __init acpi_force_32bit_fadt_addr(char *s)
{
	pr_info("Forcing 32 Bit FADT addresses\n");
	acpi_gbl_use32_bit_fadt_addresses = TRUE;

	return 0;
}

early_param("acpi_force_32bit_fadt_addr", acpi_force_32bit_fadt_addr);
