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
#include <linux/sched.h>
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

void acpi_table_print_madt_entry(acpi_table_entry_header * header)
{
	if (!header)
		return;

	switch (header->type) {

	case ACPI_MADT_LAPIC:
		{
			struct acpi_table_lapic *p =
			    (struct acpi_table_lapic *)header;
			printk(KERN_INFO PREFIX
			       "LAPIC (acpi_id[0x%02x] lapic_id[0x%02x] %s)\n",
			       p->acpi_id, p->id,
			       p->flags.enabled ? "enabled" : "disabled");
		}
		break;

	case ACPI_MADT_IOAPIC:
		{
			struct acpi_table_ioapic *p =
			    (struct acpi_table_ioapic *)header;
			printk(KERN_INFO PREFIX
			       "IOAPIC (id[0x%02x] address[0x%08x] gsi_base[%d])\n",
			       p->id, p->address, p->global_irq_base);
		}
		break;

	case ACPI_MADT_INT_SRC_OVR:
		{
			struct acpi_table_int_src_ovr *p =
			    (struct acpi_table_int_src_ovr *)header;
			printk(KERN_INFO PREFIX
			       "INT_SRC_OVR (bus %d bus_irq %d global_irq %d %s %s)\n",
			       p->bus, p->bus_irq, p->global_irq,
			       mps_inti_flags_polarity[p->flags.polarity],
			       mps_inti_flags_trigger[p->flags.trigger]);
			if (p->flags.reserved)
				printk(KERN_INFO PREFIX
				       "INT_SRC_OVR unexpected reserved flags: 0x%x\n",
				       p->flags.reserved);

		}
		break;

	case ACPI_MADT_NMI_SRC:
		{
			struct acpi_table_nmi_src *p =
			    (struct acpi_table_nmi_src *)header;
			printk(KERN_INFO PREFIX
			       "NMI_SRC (%s %s global_irq %d)\n",
			       mps_inti_flags_polarity[p->flags.polarity],
			       mps_inti_flags_trigger[p->flags.trigger],
			       p->global_irq);
		}
		break;

	case ACPI_MADT_LAPIC_NMI:
		{
			struct acpi_table_lapic_nmi *p =
			    (struct acpi_table_lapic_nmi *)header;
			printk(KERN_INFO PREFIX
			       "LAPIC_NMI (acpi_id[0x%02x] %s %s lint[0x%x])\n",
			       p->acpi_id,
			       mps_inti_flags_polarity[p->flags.polarity],
			       mps_inti_flags_trigger[p->flags.trigger],
			       p->lint);
		}
		break;

	case ACPI_MADT_LAPIC_ADDR_OVR:
		{
			struct acpi_table_lapic_addr_ovr *p =
			    (struct acpi_table_lapic_addr_ovr *)header;
			printk(KERN_INFO PREFIX
			       "LAPIC_ADDR_OVR (address[%p])\n",
			       (void *)(unsigned long)p->address);
		}
		break;

	case ACPI_MADT_IOSAPIC:
		{
			struct acpi_table_iosapic *p =
			    (struct acpi_table_iosapic *)header;
			printk(KERN_INFO PREFIX
			       "IOSAPIC (id[0x%x] address[%p] gsi_base[%d])\n",
			       p->id, (void *)(unsigned long)p->address,
			       p->global_irq_base);
		}
		break;

	case ACPI_MADT_LSAPIC:
		{
			struct acpi_table_lsapic *p =
			    (struct acpi_table_lsapic *)header;
			printk(KERN_INFO PREFIX
			       "LSAPIC (acpi_id[0x%02x] lsapic_id[0x%02x] lsapic_eid[0x%02x] %s)\n",
			       p->acpi_id, p->id, p->eid,
			       p->flags.enabled ? "enabled" : "disabled");
		}
		break;

	case ACPI_MADT_PLAT_INT_SRC:
		{
			struct acpi_table_plat_int_src *p =
			    (struct acpi_table_plat_int_src *)header;
			printk(KERN_INFO PREFIX
			       "PLAT_INT_SRC (%s %s type[0x%x] id[0x%04x] eid[0x%x] iosapic_vector[0x%x] global_irq[0x%x]\n",
			       mps_inti_flags_polarity[p->flags.polarity],
			       mps_inti_flags_trigger[p->flags.trigger],
			       p->type, p->id, p->eid, p->iosapic_vector,
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
acpi_table_parse_madt_family(char *id,
			     unsigned long madt_size,
			     int entry_id,
			     acpi_madt_entry_handler handler,
			     unsigned int max_entries)
{
	struct acpi_table_header *madt = NULL;
	acpi_table_entry_header *entry;
	unsigned int count = 0;
	unsigned long madt_end;

	if (!handler)
		return -EINVAL;

	/* Locate the MADT (if exists). There should only be one. */

	acpi_get_table(id, 0, &madt);

	if (!madt) {
		printk(KERN_WARNING PREFIX "%4.4s not present\n", id);
		return -ENODEV;
	}

	madt_end = (unsigned long)madt + madt->length;

	/* Parse all entries looking for a match. */

	entry = (acpi_table_entry_header *)
	    ((unsigned long)madt + madt_size);

	while (((unsigned long)entry) + sizeof(acpi_table_entry_header) <
	       madt_end) {
		if (entry->type == entry_id
		    && (!max_entries || count++ < max_entries))
			if (handler(entry, madt_end))
				return -EINVAL;

		entry = (acpi_table_entry_header *)
		    ((unsigned long)entry + entry->length);
	}
	if (max_entries && count > max_entries) {
		printk(KERN_WARNING PREFIX "[%4.4s:0x%02x] ignored %i entries of "
		       "%i found\n", id, entry_id, count - max_entries, count);
	}

	return count;
}

int __init
acpi_table_parse_madt(enum acpi_madt_entry_id id,
		      acpi_madt_entry_handler handler, unsigned int max_entries)
{
	return acpi_table_parse_madt_family("APIC",
					    sizeof(struct acpi_table_madt), id,
					    handler, max_entries);
}

int __init acpi_table_parse(char *id, acpi_table_handler handler)
{
	struct acpi_table_header *table = NULL;

	if (!handler)
		return -EINVAL;

	acpi_get_table(id, 0, &table);
	if (table) {
		handler(table);
		return 1;
	} else
		return 0;
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
	return 0;
}
