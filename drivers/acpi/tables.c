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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/bootmem.h>

#define PREFIX			"ACPI: "

#define ACPI_MAX_TABLES		128

static char *mps_inti_flags_polarity[] = { "dfl", "high", "res", "low" };
static char *mps_inti_flags_trigger[] = { "dfl", "edge", "res", "level" };

static struct acpi_table_desc initial_tables[ACPI_MAX_TABLES] __initdata;

static int acpi_apic_instance __initdata;

void acpi_table_print_madt_entry(struct acpi_subtable_header *header)
{
	if (!header)
		return;

	switch (header->type) {

	case ACPI_MADT_TYPE_LOCAL_APIC:
		{
			struct acpi_madt_local_apic *p =
			    (struct acpi_madt_local_apic *)header;
			printk(KERN_INFO PREFIX
			       "LAPIC (acpi_id[0x%02x] lapic_id[0x%02x] %s)\n",
			       p->processor_id, p->id,
			       (p->lapic_flags & ACPI_MADT_ENABLED) ? "enabled" : "disabled");
		}
		break;

	case ACPI_MADT_TYPE_IO_APIC:
		{
			struct acpi_madt_io_apic *p =
			    (struct acpi_madt_io_apic *)header;
			printk(KERN_INFO PREFIX
			       "IOAPIC (id[0x%02x] address[0x%08x] gsi_base[%d])\n",
			       p->id, p->address, p->global_irq_base);
		}
		break;

	case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
		{
			struct acpi_madt_interrupt_override *p =
			    (struct acpi_madt_interrupt_override *)header;
			printk(KERN_INFO PREFIX
			       "INT_SRC_OVR (bus %d bus_irq %d global_irq %d %s %s)\n",
			       p->bus, p->source_irq, p->global_irq,
			       mps_inti_flags_polarity[p->inti_flags & ACPI_MADT_POLARITY_MASK],
			       mps_inti_flags_trigger[(p->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2]);
			if (p->inti_flags  &
			    ~(ACPI_MADT_POLARITY_MASK | ACPI_MADT_TRIGGER_MASK))
				printk(KERN_INFO PREFIX
				       "INT_SRC_OVR unexpected reserved flags: 0x%x\n",
				       p->inti_flags  &
					~(ACPI_MADT_POLARITY_MASK | ACPI_MADT_TRIGGER_MASK));

		}
		break;

	case ACPI_MADT_TYPE_NMI_SOURCE:
		{
			struct acpi_madt_nmi_source *p =
			    (struct acpi_madt_nmi_source *)header;
			printk(KERN_INFO PREFIX
			       "NMI_SRC (%s %s global_irq %d)\n",
			       mps_inti_flags_polarity[p->inti_flags & ACPI_MADT_POLARITY_MASK],
			       mps_inti_flags_trigger[(p->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2],
			       p->global_irq);
		}
		break;

	case ACPI_MADT_TYPE_LOCAL_APIC_NMI:
		{
			struct acpi_madt_local_apic_nmi *p =
			    (struct acpi_madt_local_apic_nmi *)header;
			printk(KERN_INFO PREFIX
			       "LAPIC_NMI (acpi_id[0x%02x] %s %s lint[0x%x])\n",
			       p->processor_id,
			       mps_inti_flags_polarity[p->inti_flags & ACPI_MADT_POLARITY_MASK	],
			       mps_inti_flags_trigger[(p->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2],
			       p->lint);
		}
		break;

	case ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE:
		{
			struct acpi_madt_local_apic_override *p =
			    (struct acpi_madt_local_apic_override *)header;
			printk(KERN_INFO PREFIX
			       "LAPIC_ADDR_OVR (address[%p])\n",
			       (void *)(unsigned long)p->address);
		}
		break;

	case ACPI_MADT_TYPE_IO_SAPIC:
		{
			struct acpi_madt_io_sapic *p =
			    (struct acpi_madt_io_sapic *)header;
			printk(KERN_INFO PREFIX
			       "IOSAPIC (id[0x%x] address[%p] gsi_base[%d])\n",
			       p->id, (void *)(unsigned long)p->address,
			       p->global_irq_base);
		}
		break;

	case ACPI_MADT_TYPE_LOCAL_SAPIC:
		{
			struct acpi_madt_local_sapic *p =
			    (struct acpi_madt_local_sapic *)header;
			printk(KERN_INFO PREFIX
			       "LSAPIC (acpi_id[0x%02x] lsapic_id[0x%02x] lsapic_eid[0x%02x] %s)\n",
			       p->processor_id, p->id, p->eid,
			       (p->lapic_flags & ACPI_MADT_ENABLED) ? "enabled" : "disabled");
		}
		break;

	case ACPI_MADT_TYPE_INTERRUPT_SOURCE:
		{
			struct acpi_madt_interrupt_source *p =
			    (struct acpi_madt_interrupt_source *)header;
			printk(KERN_INFO PREFIX
			       "PLAT_INT_SRC (%s %s type[0x%x] id[0x%04x] eid[0x%x] iosapic_vector[0x%x] global_irq[0x%x]\n",
			       mps_inti_flags_polarity[p->inti_flags & ACPI_MADT_POLARITY_MASK],
			       mps_inti_flags_trigger[(p->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2],
			       p->type, p->id, p->eid, p->io_sapic_vector,
			       p->global_irq);
		}
		break;

	default:
		printk(KERN_WARNING PREFIX
		       "Found unsupported MADT entry (type = 0x%x)\n",
		       header->type);
		break;
	}
}


int __init
acpi_table_parse_entries(char *id,
			     unsigned long table_size,
			     int entry_id,
			     acpi_table_entry_handler handler,
			     unsigned int max_entries)
{
	struct acpi_table_header *table_header = NULL;
	struct acpi_subtable_header *entry;
	unsigned int count = 0;
	unsigned long table_end;

	if (!handler)
		return -EINVAL;

	if (strncmp(id, ACPI_SIG_MADT, 4) == 0)
		acpi_get_table(id, acpi_apic_instance, &table_header);
	else
		acpi_get_table(id, 0, &table_header);

	if (!table_header) {
		printk(KERN_WARNING PREFIX "%4.4s not present\n", id);
		return -ENODEV;
	}

	table_end = (unsigned long)table_header + table_header->length;

	/* Parse all entries looking for a match. */

	entry = (struct acpi_subtable_header *)
	    ((unsigned long)table_header + table_size);

	while (((unsigned long)entry) + sizeof(struct acpi_subtable_header) <
	       table_end) {
		if (entry->type == entry_id
		    && (!max_entries || count++ < max_entries))
			if (handler(entry, table_end))
				return -EINVAL;

		entry = (struct acpi_subtable_header *)
		    ((unsigned long)entry + entry->length);
	}
	if (max_entries && count > max_entries) {
		printk(KERN_WARNING PREFIX "[%4.4s:0x%02x] ignored %i entries of "
		       "%i found\n", id, entry_id, count - max_entries, count);
	}

	return count;
}

int __init
acpi_table_parse_madt(enum acpi_madt_type id,
		      acpi_table_entry_handler handler, unsigned int max_entries)
{
	return acpi_table_parse_entries(ACPI_SIG_MADT,
					    sizeof(struct acpi_table_madt), id,
					    handler, max_entries);
}

/**
 * acpi_table_parse - find table with @id, run @handler on it
 *
 * @id: table id to find
 * @handler: handler to run
 *
 * Scan the ACPI System Descriptor Table (STD) for a table matching @id,
 * run @handler on it.  Return 0 if table found, return on if not.
 */
int __init acpi_table_parse(char *id, acpi_table_handler handler)
{
	struct acpi_table_header *table = NULL;

	if (!handler)
		return -EINVAL;

	if (strncmp(id, ACPI_SIG_MADT, 4) == 0)
		acpi_get_table(id, acpi_apic_instance, &table);
	else
		acpi_get_table(id, 0, &table);

	if (table) {
		handler(table);
		return 0;
	} else
		return 1;
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
		printk(KERN_WARNING PREFIX
		       "BIOS bug: multiple APIC/MADT found,"
		       " using %d\n", acpi_apic_instance);
		printk(KERN_WARNING PREFIX
		       "If \"acpi_apic_instance=%d\" works better, "
		       "notify linux-acpi@vger.kernel.org\n",
		       acpi_apic_instance ? 0 : 2);

	} else
		acpi_apic_instance = 0;

	return;
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
	acpi_initialize_tables(initial_tables, ACPI_MAX_TABLES, 0);
	check_multiple_madt();
	return 0;
}

static int __init acpi_parse_apic_instance(char *str)
{

	acpi_apic_instance = simple_strtoul(str, NULL, 0);

	printk(KERN_NOTICE PREFIX "Shall use APIC/MADT table %d\n",
	       acpi_apic_instance);

	return 0;
}

early_param("acpi_apic_instance", acpi_parse_apic_instance);
