/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Keith M Wesolowski
 * Copyright (C) 2005 Ilya A. Volynets (Total Knowledge)
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/ip32/crime.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

extern void crime_init(void);

void __init prom_meminit (void)
{
	u64 base, size;
	int bank;

	crime_init();

	for (bank=0; bank < CRIME_MAXBANKS; bank++) {
		u64 bankctl = crime->bank_ctrl[bank];
		base = (bankctl & CRIME_MEM_BANK_CONTROL_ADDR) << 25;
		if (bank != 0 && base == 0)
			continue;
		size = (bankctl & CRIME_MEM_BANK_CONTROL_SDRAM_SIZE) ? 128 : 32;
		size <<= 20;
		if (base + size > (256 << 20))
			base += CRIME_HI_MEM_BASE;

		printk("CRIME MC: bank %u base 0x%016lx size %luMiB\n",
			bank, base, size >> 20);
		add_memory_region (base, size, BOOT_MEM_RAM);
	}
}


void __init prom_free_prom_memory(void)
{
}
