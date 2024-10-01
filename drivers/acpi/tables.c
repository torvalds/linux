// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  acpi_tables.c - ACPI Boot-Time Table Parsing
 *
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
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
#include <linux/memblock.h>
#include <linux/earlycpio.h>
#include <linux/initrd.h>
#include <linux/security.h>
#include <linux/kmemleak.h>
#include "internal.h"

#ifdef CONFIG_ACPI_CUSTOM_DSDT
#include CONFIG_ACPI_CUSTOM_DSDT_FILE
#endif

#define ACPI_MAX_TABLES		128

static char *mps_inti_flags_polarity[] = { "dfl", "high", "res", "low" };
static char *mps_inti_flags_trigger[] = { "dfl", "edge", "res", "level" };

static struct acpi_table_desc initial_tables[ACPI_MAX_TABLES] __initdata;

static int acpi_apic_instance __initdata_or_acpilib;

enum acpi_subtable_type {
	ACPI_SUBTABLE_COMMON,
	ACPI_SUBTABLE_HMAT,
	ACPI_SUBTABLE_PRMT,
	ACPI_SUBTABLE_CEDT,
};

struct acpi_subtable_entry {
	union acpi_subtable_headers *hdr;
	enum acpi_subtable_type type;
};

/*
 * Disable table checksum verification for the early stage due to the size
 * limitation of the current x86 early mapping implementation.
 */
static bool acpi_verify_table_checksum __initdata_or_acpilib = false;

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
			pr_info("LAPIC_ADDR_OVR (address[0x%llx])\n",
				p->address);
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

static unsigned long __init_or_acpilib
acpi_get_entry_type(struct acpi_subtable_entry *entry)
{
	switch (entry->type) {
	case ACPI_SUBTABLE_COMMON:
		return entry->hdr->common.type;
	case ACPI_SUBTABLE_HMAT:
		return entry->hdr->hmat.type;
	case ACPI_SUBTABLE_PRMT:
		return 0;
	case ACPI_SUBTABLE_CEDT:
		return entry->hdr->cedt.type;
	}
	return 0;
}

static unsigned long __init_or_acpilib
acpi_get_entry_length(struct acpi_subtable_entry *entry)
{
	switch (entry->type) {
	case ACPI_SUBTABLE_COMMON:
		return entry->hdr->common.length;
	case ACPI_SUBTABLE_HMAT:
		return entry->hdr->hmat.length;
	case ACPI_SUBTABLE_PRMT:
		return entry->hdr->prmt.length;
	case ACPI_SUBTABLE_CEDT:
		return entry->hdr->cedt.length;
	}
	return 0;
}

static unsigned long __init_or_acpilib
acpi_get_subtable_header_length(struct acpi_subtable_entry *entry)
{
	switch (entry->type) {
	case ACPI_SUBTABLE_COMMON:
		return sizeof(entry->hdr->common);
	case ACPI_SUBTABLE_HMAT:
		return sizeof(entry->hdr->hmat);
	case ACPI_SUBTABLE_PRMT:
		return sizeof(entry->hdr->prmt);
	case ACPI_SUBTABLE_CEDT:
		return sizeof(entry->hdr->cedt);
	}
	return 0;
}

static enum acpi_subtable_type __init_or_acpilib
acpi_get_subtable_type(char *id)
{
	if (strncmp(id, ACPI_SIG_HMAT, 4) == 0)
		return ACPI_SUBTABLE_HMAT;
	if (strncmp(id, ACPI_SIG_PRMT, 4) == 0)
		return ACPI_SUBTABLE_PRMT;
	if (strncmp(id, ACPI_SIG_CEDT, 4) == 0)
		return ACPI_SUBTABLE_CEDT;
	return ACPI_SUBTABLE_COMMON;
}

static __init_or_acpilib bool has_handler(struct acpi_subtable_proc *proc)
{
	return proc->handler || proc->handler_arg;
}

static __init_or_acpilib int call_handler(struct acpi_subtable_proc *proc,
					  union acpi_subtable_headers *hdr,
					  unsigned long end)
{
	if (proc->handler)
		return proc->handler(hdr, end);
	if (proc->handler_arg)
		return proc->handler_arg(hdr, proc->arg, end);
	return -EINVAL;
}

/**
 * acpi_parse_entries_array - for each proc_num find a suitable subtable
 *
 * @id: table id (for debugging purposes)
 * @table_size: size of the root table
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
 * The table_size is not the size of the complete ACPI table (the length
 * field in the header struct), but only the size of the root table; i.e.,
 * the offset from the very first byte of the complete ACPI table, to the
 * first byte of the very first subtable.
 *
 * On success returns sum of all matching entries for all proc handlers.
 * Otherwise, -ENODEV or -EINVAL is returned.
 */
static int __init_or_acpilib acpi_parse_entries_array(
	char *id, unsigned long table_size,
	struct acpi_table_header *table_header, struct acpi_subtable_proc *proc,
	int proc_num, unsigned int max_entries)
{
	struct acpi_subtable_entry entry;
	unsigned long table_end, subtable_len, entry_len;
	int count = 0;
	int errs = 0;
	int i;

	table_end = (unsigned long)table_header + table_header->length;

	/* Parse all entries looking for a match. */

	entry.type = acpi_get_subtable_type(id);
	entry.hdr = (union acpi_subtable_headers *)
	    ((unsigned long)table_header + table_size);
	subtable_len = acpi_get_subtable_header_length(&entry);

	while (((unsigned long)entry.hdr) + subtable_len  < table_end) {
		if (max_entries && count >= max_entries)
			break;

		for (i = 0; i < proc_num; i++) {
			if (acpi_get_entry_type(&entry) != proc[i].id)
				continue;
			if (!has_handler(&proc[i]) ||
			    (!errs &&
			     call_handler(&proc[i], entry.hdr, table_end))) {
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
		entry_len = acpi_get_entry_length(&entry);
		if (entry_len == 0) {
			pr_err("[%4.4s:0x%02x] Invalid zero length\n", id, proc->id);
			return -EINVAL;
		}

		entry.hdr = (union acpi_subtable_headers *)
		    ((unsigned long)entry.hdr + entry_len);
	}

	if (max_entries && count > max_entries) {
		pr_warn("[%4.4s:0x%02x] found the maximum %i entries\n",
			id, proc->id, count);
	}

	return errs ? -EINVAL : count;
}

int __init_or_acpilib acpi_table_parse_entries_array(
	char *id, unsigned long table_size, struct acpi_subtable_proc *proc,
	int proc_num, unsigned int max_entries)
{
	struct acpi_table_header *table_header = NULL;
	int count;
	u32 instance = 0;

	if (acpi_disabled)
		return -ENODEV;

	if (!id)
		return -EINVAL;

	if (!table_size)
		return -EINVAL;

	if (!strncmp(id, ACPI_SIG_MADT, 4))
		instance = acpi_apic_instance;

	acpi_get_table(id, instance, &table_header);
	if (!table_header) {
		pr_debug("%4.4s not present\n", id);
		return -ENODEV;
	}

	count = acpi_parse_entries_array(id, table_size, table_header,
			proc, proc_num, max_entries);

	acpi_put_table(table_header);
	return count;
}

static int __init_or_acpilib __acpi_table_parse_entries(
	char *id, unsigned long table_size, int entry_id,
	acpi_tbl_entry_handler handler, acpi_tbl_entry_handler_arg handler_arg,
	void *arg, unsigned int max_entries)
{
	struct acpi_subtable_proc proc = {
		.id		= entry_id,
		.handler	= handler,
		.handler_arg	= handler_arg,
		.arg		= arg,
	};

	return acpi_table_parse_entries_array(id, table_size, &proc, 1,
						max_entries);
}

int __init_or_acpilib
acpi_table_parse_cedt(enum acpi_cedt_type id,
		      acpi_tbl_entry_handler_arg handler_arg, void *arg)
{
	return __acpi_table_parse_entries(ACPI_SIG_CEDT,
					  sizeof(struct acpi_table_cedt), id,
					  NULL, handler_arg, arg, 0);
}
EXPORT_SYMBOL_ACPI_LIB(acpi_table_parse_cedt);

int __init acpi_table_parse_entries(char *id, unsigned long table_size,
				    int entry_id,
				    acpi_tbl_entry_handler handler,
				    unsigned int max_entries)
{
	return __acpi_table_parse_entries(id, table_size, entry_id, handler,
					  NULL, NULL, max_entries);
}

int __init acpi_table_parse_madt(enum acpi_madt_type id,
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

	if (acpi_disabled)
		return -ENODEV;

	if (!id || !handler)
		return -EINVAL;

	if (strncmp(id, ACPI_SIG_MADT, 4) == 0)
		acpi_get_table(id, acpi_apic_instance, &table);
	else
		acpi_get_table(id, 0, &table);

	if (table) {
		handler(table);
		acpi_put_table(table);
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

	acpi_get_table(ACPI_SIG_MADT, 2, &table);
	if (table) {
		pr_warn("BIOS bug: multiple APIC/MADT found, using %d\n",
			acpi_apic_instance);
		pr_warn("If \"acpi_apic_instance=%d\" works better, "
			"notify linux-acpi@vger.kernel.org\n",
			acpi_apic_instance ? 0 : 2);
		acpi_put_table(table);

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
static const char table_sigs[][ACPI_NAMESEG_SIZE] __initconst = {
	ACPI_SIG_BERT, ACPI_SIG_BGRT, ACPI_SIG_CPEP, ACPI_SIG_ECDT,
	ACPI_SIG_EINJ, ACPI_SIG_ERST, ACPI_SIG_HEST, ACPI_SIG_MADT,
	ACPI_SIG_MSCT, ACPI_SIG_SBST, ACPI_SIG_SLIT, ACPI_SIG_SRAT,
	ACPI_SIG_ASF,  ACPI_SIG_BOOT, ACPI_SIG_DBGP, ACPI_SIG_DMAR,
	ACPI_SIG_HPET, ACPI_SIG_IBFT, ACPI_SIG_IVRS, ACPI_SIG_MCFG,
	ACPI_SIG_MCHI, ACPI_SIG_SLIC, ACPI_SIG_SPCR, ACPI_SIG_SPMI,
	ACPI_SIG_TCPA, ACPI_SIG_UEFI, ACPI_SIG_WAET, ACPI_SIG_WDAT,
	ACPI_SIG_WDDT, ACPI_SIG_WDRT, ACPI_SIG_DSDT, ACPI_SIG_FADT,
	ACPI_SIG_PSDT, ACPI_SIG_RSDT, ACPI_SIG_XSDT, ACPI_SIG_SSDT,
	ACPI_SIG_IORT, ACPI_SIG_NFIT, ACPI_SIG_HMAT, ACPI_SIG_PPTT,
	ACPI_SIG_NHLT, ACPI_SIG_AEST, ACPI_SIG_CEDT, ACPI_SIG_AGDI };

#define ACPI_HEADER_SIZE sizeof(struct acpi_table_header)

#define NR_ACPI_INITRD_TABLES 64
static struct cpio_data __initdata acpi_initrd_files[NR_ACPI_INITRD_TABLES];
static DECLARE_BITMAP(acpi_initrd_installed, NR_ACPI_INITRD_TABLES);

#define MAP_CHUNK_SIZE   (NR_FIX_BTMAPS << PAGE_SHIFT)

void __init acpi_table_upgrade(void)
{
	void *data;
	size_t size;
	int sig, no, table_nr = 0, total_offset = 0;
	long offset = 0;
	struct acpi_table_header *table;
	char cpio_path[32] = "kernel/firmware/acpi/";
	struct cpio_data file;

	if (IS_ENABLED(CONFIG_ACPI_TABLE_OVERRIDE_VIA_BUILTIN_INITRD)) {
		data = __initramfs_start;
		size = __initramfs_size;
	} else {
		data = (void *)initrd_start;
		size = initrd_end - initrd_start;
	}

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

		for (sig = 0; sig < ARRAY_SIZE(table_sigs); sig++)
			if (!memcmp(table->signature, table_sigs[sig], 4))
				break;

		if (sig >= ARRAY_SIZE(table_sigs)) {
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

	if (security_locked_down(LOCKDOWN_ACPI_TABLES)) {
		pr_notice("kernel is locked down, ignoring table override\n");
		return;
	}

	acpi_tables_addr =
		memblock_phys_alloc_range(all_tables_size, PAGE_SIZE,
					  0, ACPI_TABLE_UPGRADE_MAX_PHYS);
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
	 * Both memblock_reserve and e820__range_add (via arch_reserve_mem_area)
	 * works fine.
	 */
	arch_reserve_mem_area(acpi_tables_addr, all_tables_size);

	kmemleak_ignore_phys(acpi_tables_addr);

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
		if (ACPI_COMPARE_NAMESEG(table->signature, ACPI_SIG_RSDT) ||
		    ACPI_COMPARE_NAMESEG(table->signature, ACPI_SIG_XSDT)) {
			acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
			goto next_table;
		}
		/*
		 * Mark the table to avoid being used in
		 * acpi_table_initrd_override(). Though this is not possible
		 * because override is disabled in acpi_install_physical_table().
		 */
		if (test_and_set_bit(table_index, acpi_initrd_installed)) {
			acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
			goto next_table;
		}

		pr_info("Table Upgrade: install [%4.4s-%6.6s-%8.8s]\n",
			table->signature, table->oem_id,
			table->oem_table_id);
		acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
		acpi_install_physical_table(acpi_tables_addr + table_offset);
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

#ifdef CONFIG_ACPI_CUSTOM_DSDT
static void *amlcode __attribute__ ((weakref("AmlCode")));
static void *dsdt_amlcode __attribute__ ((weakref("dsdt_aml_code")));
#endif

acpi_status acpi_os_table_override(struct acpi_table_header *existing_table,
		       struct acpi_table_header **new_table)
{
	if (!existing_table || !new_table)
		return AE_BAD_PARAMETER;

	*new_table = NULL;

#ifdef CONFIG_ACPI_CUSTOM_DSDT
	if (!strncmp(existing_table->signature, "DSDT", 4)) {
		*new_table = (struct acpi_table_header *)&amlcode;
		if (!(*new_table))
			*new_table = (struct acpi_table_header *)&dsdt_amlcode;
	}
#endif
	if (*new_table != NULL)
		acpi_table_taint(existing_table);
	return AE_OK;
}

/*
 * acpi_locate_initial_tables()
 *
 * find RSDP, find and checksum SDT/XSDT.
 * checksum all tables, print SDT/XSDT
 *
 * result: sdt_entry[] is initialized
 */

int __init acpi_locate_initial_tables(void)
{
	acpi_status status;

	if (acpi_verify_table_checksum) {
		pr_info("Early table checksum verification enabled\n");
		acpi_gbl_enable_table_validation = TRUE;
	} else {
		pr_info("Early table checksum verification disabled\n");
		acpi_gbl_enable_table_validation = FALSE;
	}

	status = acpi_initialize_tables(initial_tables, ACPI_MAX_TABLES, 0);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	return 0;
}

void __init acpi_reserve_initial_tables(void)
{
	int i;

	for (i = 0; i < ACPI_MAX_TABLES; i++) {
		struct acpi_table_desc *table_desc = &initial_tables[i];
		u64 start = table_desc->address;
		u64 size = table_desc->length;

		if (!start || !size)
			break;

		pr_info("Reserving %4s table memory at [mem 0x%llx-0x%llx]\n",
			table_desc->signature.ascii, start, start + size - 1);

		memblock_reserve(start, size);
	}
}

void __init acpi_table_init_complete(void)
{
	acpi_table_initrd_scan();
	check_multiple_madt();
}

int __init acpi_table_init(void)
{
	int ret;

	ret = acpi_locate_initial_tables();
	if (ret)
		return ret;

	acpi_table_init_complete();

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
