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

#include <linux/config.h>
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

#define ACPI_MAX_TABLES		256

static char *acpi_table_signatures[ACPI_TABLE_COUNT] = {
	[ACPI_TABLE_UNKNOWN] = "????",
	[ACPI_APIC] = "APIC",
	[ACPI_BOOT] = "BOOT",
	[ACPI_DBGP] = "DBGP",
	[ACPI_DSDT] = "DSDT",
	[ACPI_ECDT] = "ECDT",
	[ACPI_ETDT] = "ETDT",
	[ACPI_FADT] = "FACP",
	[ACPI_FACS] = "FACS",
	[ACPI_OEMX] = "OEM",
	[ACPI_PSDT] = "PSDT",
	[ACPI_SBST] = "SBST",
	[ACPI_SLIT] = "SLIT",
	[ACPI_SPCR] = "SPCR",
	[ACPI_SRAT] = "SRAT",
	[ACPI_SSDT] = "SSDT",
	[ACPI_SPMI] = "SPMI",
	[ACPI_HPET] = "HPET",
	[ACPI_MCFG] = "MCFG",
};

static char *mps_inti_flags_polarity[] = { "dfl", "high", "res", "low" };
static char *mps_inti_flags_trigger[] = { "dfl", "edge", "res", "level" };

/* System Description Table (RSDT/XSDT) */
struct acpi_table_sdt {
	unsigned long pa;
	enum acpi_table_id id;
	unsigned long size;
} __attribute__ ((packed));

static unsigned long sdt_pa;	/* Physical Address */
static unsigned long sdt_count;	/* Table count */

static struct acpi_table_sdt sdt_entry[ACPI_MAX_TABLES];

void acpi_table_print(struct acpi_table_header *header, unsigned long phys_addr)
{
	char *name = NULL;

	if (!header)
		return;

	/* Some table signatures aren't good table names */

	if (!strncmp((char *)&header->signature,
		     acpi_table_signatures[ACPI_APIC],
		     sizeof(header->signature))) {
		name = "MADT";
	} else if (!strncmp((char *)&header->signature,
			    acpi_table_signatures[ACPI_FADT],
			    sizeof(header->signature))) {
		name = "FADT";
	} else
		name = header->signature;

	printk(KERN_DEBUG PREFIX
	       "%.4s (v%3.3d %6.6s %8.8s 0x%08x %.4s 0x%08x) @ 0x%p\n", name,
	       header->revision, header->oem_id, header->oem_table_id,
	       header->oem_revision, header->asl_compiler_id,
	       header->asl_compiler_revision, (void *)phys_addr);
}

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

static int
acpi_table_compute_checksum(void *table_pointer, unsigned long length)
{
	u8 *p = (u8 *) table_pointer;
	unsigned long remains = length;
	unsigned long sum = 0;

	if (!p || !length)
		return -EINVAL;

	while (remains--)
		sum += *p++;

	return (sum & 0xFF);
}

/*
 * acpi_get_table_header_early()
 * for acpi_blacklisted(), acpi_table_get_sdt()
 */
int __init
acpi_get_table_header_early(enum acpi_table_id id,
			    struct acpi_table_header **header)
{
	unsigned int i;
	enum acpi_table_id temp_id;

	/* DSDT is different from the rest */
	if (id == ACPI_DSDT)
		temp_id = ACPI_FADT;
	else
		temp_id = id;

	/* Locate the table. */

	for (i = 0; i < sdt_count; i++) {
		if (sdt_entry[i].id != temp_id)
			continue;
		*header = (void *)
		    __acpi_map_table(sdt_entry[i].pa, sdt_entry[i].size);
		if (!*header) {
			printk(KERN_WARNING PREFIX "Unable to map %s\n",
			       acpi_table_signatures[temp_id]);
			return -ENODEV;
		}
		break;
	}

	if (!*header) {
		printk(KERN_WARNING PREFIX "%s not present\n",
		       acpi_table_signatures[id]);
		return -ENODEV;
	}

	/* Map the DSDT header via the pointer in the FADT */
	if (id == ACPI_DSDT) {
		struct fadt_descriptor_rev2 *fadt =
		    (struct fadt_descriptor_rev2 *)*header;

		if (fadt->revision == 3 && fadt->Xdsdt) {
			*header = (void *)__acpi_map_table(fadt->Xdsdt,
							   sizeof(struct
								  acpi_table_header));
		} else if (fadt->V1_dsdt) {
			*header = (void *)__acpi_map_table(fadt->V1_dsdt,
							   sizeof(struct
								  acpi_table_header));
		} else
			*header = NULL;

		if (!*header) {
			printk(KERN_WARNING PREFIX "Unable to map DSDT\n");
			return -ENODEV;
		}
	}

	return 0;
}

int __init
acpi_table_parse_madt_family(enum acpi_table_id id,
			     unsigned long madt_size,
			     int entry_id,
			     acpi_madt_entry_handler handler,
			     unsigned int max_entries)
{
	void *madt = NULL;
	acpi_table_entry_header *entry;
	unsigned int count = 0;
	unsigned long madt_end;
	unsigned int i;

	if (!handler)
		return -EINVAL;

	/* Locate the MADT (if exists). There should only be one. */

	for (i = 0; i < sdt_count; i++) {
		if (sdt_entry[i].id != id)
			continue;
		madt = (void *)
		    __acpi_map_table(sdt_entry[i].pa, sdt_entry[i].size);
		if (!madt) {
			printk(KERN_WARNING PREFIX "Unable to map %s\n",
			       acpi_table_signatures[id]);
			return -ENODEV;
		}
		break;
	}

	if (!madt) {
		printk(KERN_WARNING PREFIX "%s not present\n",
		       acpi_table_signatures[id]);
		return -ENODEV;
	}

	madt_end = (unsigned long)madt + sdt_entry[i].size;

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
		printk(KERN_WARNING PREFIX "[%s:0x%02x] ignored %i entries of "
		       "%i found\n", acpi_table_signatures[id], entry_id,
		       count - max_entries, count);
	}

	return count;
}

int __init
acpi_table_parse_madt(enum acpi_madt_entry_id id,
		      acpi_madt_entry_handler handler, unsigned int max_entries)
{
	return acpi_table_parse_madt_family(ACPI_APIC,
					    sizeof(struct acpi_table_madt), id,
					    handler, max_entries);
}

int __init acpi_table_parse(enum acpi_table_id id, acpi_table_handler handler)
{
	int count = 0;
	unsigned int i = 0;

	if (!handler)
		return -EINVAL;

	for (i = 0; i < sdt_count; i++) {
		if (sdt_entry[i].id != id)
			continue;
		count++;
		if (count == 1)
			handler(sdt_entry[i].pa, sdt_entry[i].size);

		else
			printk(KERN_WARNING PREFIX
			       "%d duplicate %s table ignored.\n", count,
			       acpi_table_signatures[id]);
	}

	return count;
}

static int __init acpi_table_get_sdt(struct acpi_table_rsdp *rsdp)
{
	struct acpi_table_header *header = NULL;
	unsigned int i, id = 0;

	if (!rsdp)
		return -EINVAL;

	/* First check XSDT (but only on ACPI 2.0-compatible systems) */

	if ((rsdp->revision >= 2) &&
	    (((struct acpi20_table_rsdp *)rsdp)->xsdt_address)) {

		struct acpi_table_xsdt *mapped_xsdt = NULL;

		sdt_pa = ((struct acpi20_table_rsdp *)rsdp)->xsdt_address;

		/* map in just the header */
		header = (struct acpi_table_header *)
		    __acpi_map_table(sdt_pa, sizeof(struct acpi_table_header));

		if (!header) {
			printk(KERN_WARNING PREFIX
			       "Unable to map XSDT header\n");
			return -ENODEV;
		}

		/* remap in the entire table before processing */
		mapped_xsdt = (struct acpi_table_xsdt *)
		    __acpi_map_table(sdt_pa, header->length);
		if (!mapped_xsdt) {
			printk(KERN_WARNING PREFIX "Unable to map XSDT\n");
			return -ENODEV;
		}
		header = &mapped_xsdt->header;

		if (strncmp(header->signature, "XSDT", 4)) {
			printk(KERN_WARNING PREFIX
			       "XSDT signature incorrect\n");
			return -ENODEV;
		}

		if (acpi_table_compute_checksum(header, header->length)) {
			printk(KERN_WARNING PREFIX "Invalid XSDT checksum\n");
			return -ENODEV;
		}

		sdt_count =
		    (header->length - sizeof(struct acpi_table_header)) >> 3;
		if (sdt_count > ACPI_MAX_TABLES) {
			printk(KERN_WARNING PREFIX
			       "Truncated %lu XSDT entries\n",
			       (sdt_count - ACPI_MAX_TABLES));
			sdt_count = ACPI_MAX_TABLES;
		}

		for (i = 0; i < sdt_count; i++)
			sdt_entry[i].pa = (unsigned long)mapped_xsdt->entry[i];
	}

	/* Then check RSDT */

	else if (rsdp->rsdt_address) {

		struct acpi_table_rsdt *mapped_rsdt = NULL;

		sdt_pa = rsdp->rsdt_address;

		/* map in just the header */
		header = (struct acpi_table_header *)
		    __acpi_map_table(sdt_pa, sizeof(struct acpi_table_header));
		if (!header) {
			printk(KERN_WARNING PREFIX
			       "Unable to map RSDT header\n");
			return -ENODEV;
		}

		/* remap in the entire table before processing */
		mapped_rsdt = (struct acpi_table_rsdt *)
		    __acpi_map_table(sdt_pa, header->length);
		if (!mapped_rsdt) {
			printk(KERN_WARNING PREFIX "Unable to map RSDT\n");
			return -ENODEV;
		}
		header = &mapped_rsdt->header;

		if (strncmp(header->signature, "RSDT", 4)) {
			printk(KERN_WARNING PREFIX
			       "RSDT signature incorrect\n");
			return -ENODEV;
		}

		if (acpi_table_compute_checksum(header, header->length)) {
			printk(KERN_WARNING PREFIX "Invalid RSDT checksum\n");
			return -ENODEV;
		}

		sdt_count =
		    (header->length - sizeof(struct acpi_table_header)) >> 2;
		if (sdt_count > ACPI_MAX_TABLES) {
			printk(KERN_WARNING PREFIX
			       "Truncated %lu RSDT entries\n",
			       (sdt_count - ACPI_MAX_TABLES));
			sdt_count = ACPI_MAX_TABLES;
		}

		for (i = 0; i < sdt_count; i++)
			sdt_entry[i].pa = (unsigned long)mapped_rsdt->entry[i];
	}

	else {
		printk(KERN_WARNING PREFIX
		       "No System Description Table (RSDT/XSDT) specified in RSDP\n");
		return -ENODEV;
	}

	acpi_table_print(header, sdt_pa);

	for (i = 0; i < sdt_count; i++) {

		/* map in just the header */
		header = (struct acpi_table_header *)
		    __acpi_map_table(sdt_entry[i].pa,
				     sizeof(struct acpi_table_header));
		if (!header)
			continue;

		/* remap in the entire table before processing */
		header = (struct acpi_table_header *)
		    __acpi_map_table(sdt_entry[i].pa, header->length);
		if (!header)
			continue;

		acpi_table_print(header, sdt_entry[i].pa);

		if (acpi_table_compute_checksum(header, header->length)) {
			printk(KERN_WARNING "  >>> ERROR: Invalid checksum\n");
			continue;
		}

		sdt_entry[i].size = header->length;

		for (id = 0; id < ACPI_TABLE_COUNT; id++) {
			if (!strncmp((char *)&header->signature,
				     acpi_table_signatures[id],
				     sizeof(header->signature))) {
				sdt_entry[i].id = id;
			}
		}
	}

	/* 
	 * The DSDT is *not* in the RSDT (why not? no idea.) but we want
	 * to print its info, because this is what people usually blacklist
	 * against. Unfortunately, we don't know the phys_addr, so just
	 * print 0. Maybe no one will notice.
	 */
	if (!acpi_get_table_header_early(ACPI_DSDT, &header))
		acpi_table_print(header, 0);

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
	struct acpi_table_rsdp *rsdp = NULL;
	unsigned long rsdp_phys = 0;
	int result = 0;

	/* Locate and map the Root System Description Table (RSDP) */

	rsdp_phys = acpi_find_rsdp();
	if (!rsdp_phys) {
		printk(KERN_ERR PREFIX "Unable to locate RSDP\n");
		return -ENODEV;
	}

	rsdp = (struct acpi_table_rsdp *)__va(rsdp_phys);
	if (!rsdp) {
		printk(KERN_WARNING PREFIX "Unable to map RSDP\n");
		return -ENODEV;
	}

	printk(KERN_DEBUG PREFIX
	       "RSDP (v%3.3d %6.6s                                ) @ 0x%p\n",
	       rsdp->revision, rsdp->oem_id, (void *)rsdp_phys);

	if (rsdp->revision < 2)
		result =
		    acpi_table_compute_checksum(rsdp,
						sizeof(struct acpi_table_rsdp));
	else
		result =
		    acpi_table_compute_checksum(rsdp,
						((struct acpi20_table_rsdp *)
						 rsdp)->length);

	if (result) {
		printk(KERN_WARNING "  >>> ERROR: Invalid checksum\n");
		return -ENODEV;
	}

	/* Locate and map the System Description table (RSDT/XSDT) */

	if (acpi_table_get_sdt(rsdp))
		return -ENODEV;

	return 0;
}
