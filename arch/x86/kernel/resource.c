// SPDX-License-Identifier: GPL-2.0
#include <linux/dmi.h>
#include <linux/ioport.h>
#include <asm/e820/api.h>

static void resource_clip(struct resource *res, resource_size_t start,
			  resource_size_t end)
{
	resource_size_t low = 0, high = 0;

	if (res->end < start || res->start > end)
		return;		/* no conflict */

	if (res->start < start)
		low = start - res->start;

	if (res->end > end)
		high = res->end - end;

	/* Keep the area above or below the conflict, whichever is larger */
	if (low > high)
		res->end = start - 1;
	else
		res->start = end + 1;
}

/*
 * Some BIOS-es contain a bug where they add addresses which map to
 * system RAM in the PCI host bridge window returned by the ACPI _CRS
 * method, see commit 4dc2287c1805 ("x86: avoid E820 regions when
 * allocating address space"). To avoid this Linux by default excludes
 * E820 reservations when allocating addresses since 2010.
 * In 2019 some systems have shown-up with E820 reservations which cover
 * the entire _CRS returned PCI host bridge window, causing all attempts
 * to assign memory to PCI BARs to fail if Linux uses E820 reservations.
 *
 * Ideally Linux would fully stop using E820 reservations, but then
 * the old systems this was added for will regress.
 * Instead keep the old behavior for old systems, while ignoring the
 * E820 reservations for any systems from now on.
 */
static void remove_e820_regions(struct resource *avail)
{
	int i, year = dmi_get_bios_year();
	struct e820_entry *entry;

	if (year >= 2018)
		return;

	pr_info_once("PCI: Removing E820 reservations from host bridge windows\n");

	for (i = 0; i < e820_table->nr_entries; i++) {
		entry = &e820_table->entries[i];

		resource_clip(avail, entry->addr,
			      entry->addr + entry->size - 1);
	}
}

void arch_remove_reservations(struct resource *avail)
{
	/*
	 * Trim out BIOS area (high 2MB) and E820 regions. We do not remove
	 * the low 1MB unconditionally, as this area is needed for some ISA
	 * cards requiring a memory range, e.g. the i82365 PCMCIA controller.
	 */
	if (avail->flags & IORESOURCE_MEM) {
		resource_clip(avail, BIOS_ROM_BASE, BIOS_ROM_END);

		remove_e820_regions(avail);
	}
}
