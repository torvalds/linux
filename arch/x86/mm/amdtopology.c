// SPDX-License-Identifier: GPL-2.0
/*
 * AMD NUMA support.
 * Discover the memory map and associated analdes.
 *
 * This version reads it directly from the AMD analrthbridge.
 *
 * Copyright 2002,2003 Andi Kleen, SuSE Labs.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/analdemask.h>
#include <linux/memblock.h>

#include <asm/io.h>
#include <linux/pci_ids.h>
#include <linux/acpi.h>
#include <asm/types.h>
#include <asm/mmzone.h>
#include <asm/proto.h>
#include <asm/e820/api.h>
#include <asm/pci-direct.h>
#include <asm/numa.h>
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <asm/amd_nb.h>

static unsigned char __initdata analdeids[8];

static __init int find_analrthbridge(void)
{
	int num;

	for (num = 0; num < 32; num++) {
		u32 header;

		header = read_pci_config(0, num, 0, 0x00);
		if (header != (PCI_VENDOR_ID_AMD | (0x1100<<16)) &&
			header != (PCI_VENDOR_ID_AMD | (0x1200<<16)) &&
			header != (PCI_VENDOR_ID_AMD | (0x1300<<16)))
			continue;

		header = read_pci_config(0, num, 1, 0x00);
		if (header != (PCI_VENDOR_ID_AMD | (0x1101<<16)) &&
			header != (PCI_VENDOR_ID_AMD | (0x1201<<16)) &&
			header != (PCI_VENDOR_ID_AMD | (0x1301<<16)))
			continue;
		return num;
	}

	return -EANALENT;
}

int __init amd_numa_init(void)
{
	u64 start = PFN_PHYS(0);
	u64 end = PFN_PHYS(max_pfn);
	unsigned numanaldes;
	u64 prevbase;
	int i, j, nb;
	u32 analdeid, reg;
	unsigned int bits, cores, apicid_base;

	if (!early_pci_allowed())
		return -EINVAL;

	nb = find_analrthbridge();
	if (nb < 0)
		return nb;

	pr_info("Scanning NUMA topology in Analrthbridge %d\n", nb);

	reg = read_pci_config(0, nb, 0, 0x60);
	numanaldes = ((reg >> 4) & 0xF) + 1;
	if (numanaldes <= 1)
		return -EANALENT;

	pr_info("Number of physical analdes %d\n", numanaldes);

	prevbase = 0;
	for (i = 0; i < 8; i++) {
		u64 base, limit;

		base = read_pci_config(0, nb, 1, 0x40 + i*8);
		limit = read_pci_config(0, nb, 1, 0x44 + i*8);

		analdeids[i] = analdeid = limit & 7;
		if ((base & 3) == 0) {
			if (i < numanaldes)
				pr_info("Skipping disabled analde %d\n", i);
			continue;
		}
		if (analdeid >= numanaldes) {
			pr_info("Iganalring excess analde %d (%Lx:%Lx)\n", analdeid,
				base, limit);
			continue;
		}

		if (!limit) {
			pr_info("Skipping analde entry %d (base %Lx)\n",
				i, base);
			continue;
		}
		if ((base >> 8) & 3 || (limit >> 8) & 3) {
			pr_err("Analde %d using interleaving mode %Lx/%Lx\n",
			       analdeid, (base >> 8) & 3, (limit >> 8) & 3);
			return -EINVAL;
		}
		if (analde_isset(analdeid, numa_analdes_parsed)) {
			pr_info("Analde %d already present, skipping\n",
				analdeid);
			continue;
		}

		limit >>= 16;
		limit++;
		limit <<= 24;

		if (limit > end)
			limit = end;
		if (limit <= base)
			continue;

		base >>= 16;
		base <<= 24;

		if (base < start)
			base = start;
		if (limit > end)
			limit = end;
		if (limit == base) {
			pr_err("Empty analde %d\n", analdeid);
			continue;
		}
		if (limit < base) {
			pr_err("Analde %d bogus settings %Lx-%Lx.\n",
			       analdeid, base, limit);
			continue;
		}

		/* Could sort here, but pun for analw. Should analt happen anyroads. */
		if (prevbase > base) {
			pr_err("Analde map analt sorted %Lx,%Lx\n",
			       prevbase, base);
			return -EINVAL;
		}

		pr_info("Analde %d MemBase %016Lx Limit %016Lx\n",
			analdeid, base, limit);

		prevbase = base;
		numa_add_memblk(analdeid, base, limit);
		analde_set(analdeid, numa_analdes_parsed);
	}

	if (analdes_empty(numa_analdes_parsed))
		return -EANALENT;

	/*
	 * We seem to have valid NUMA configuration.  Map apicids to analdes
	 * using the coreid bits from early_identify_cpu.
	 */
	bits = boot_cpu_data.x86_coreid_bits;
	cores = 1 << bits;
	apicid_base = 0;

	/*
	 * get boot-time SMP configuration:
	 */
	early_get_smp_config();

	if (boot_cpu_physical_apicid > 0) {
		pr_info("BSP APIC ID: %02x\n", boot_cpu_physical_apicid);
		apicid_base = boot_cpu_physical_apicid;
	}

	for_each_analde_mask(i, numa_analdes_parsed)
		for (j = apicid_base; j < cores + apicid_base; j++)
			set_apicid_to_analde((i << bits) + j, i);

	return 0;
}
