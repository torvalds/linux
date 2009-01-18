/*
 * Blackfin CPLB initialization
 *
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>

#include <asm/blackfin.h>
#include <asm/cacheflush.h>
#include <asm/cplb.h>
#include <asm/cplbinit.h>
#include <asm/mem_map.h>

struct cplb_entry icplb_tbl[NR_CPUS][MAX_CPLBS] PDT_ATTR;
struct cplb_entry dcplb_tbl[NR_CPUS][MAX_CPLBS] PDT_ATTR;

int first_switched_icplb PDT_ATTR;
int first_switched_dcplb PDT_ATTR;

struct cplb_boundary dcplb_bounds[9] PDT_ATTR;
struct cplb_boundary icplb_bounds[7] PDT_ATTR;

int icplb_nr_bounds PDT_ATTR;
int dcplb_nr_bounds PDT_ATTR;

void __init generate_cplb_tables_cpu(unsigned int cpu)
{
	int i_d, i_i;
	unsigned long addr;

	struct cplb_entry *d_tbl = dcplb_tbl[cpu];
	struct cplb_entry *i_tbl = icplb_tbl[cpu];

	printk(KERN_INFO "NOMPU: setting up cplb tables\n");

	i_d = i_i = 0;

	/* Set up the zero page.  */
	d_tbl[i_d].addr = 0;
	d_tbl[i_d++].data = SDRAM_OOPS | PAGE_SIZE_1KB;

	/* Cover kernel memory with 4M pages.  */
	addr = 0;

	for (; addr < memory_start; addr += 4 * 1024 * 1024) {
		d_tbl[i_d].addr = addr;
		d_tbl[i_d++].data = SDRAM_DGENERIC | PAGE_SIZE_4MB;
		i_tbl[i_i].addr = addr;
		i_tbl[i_i++].data = SDRAM_IGENERIC | PAGE_SIZE_4MB;
	}

	/* Cover L1 memory.  One 4M area for code and data each is enough.  */
	if (L1_DATA_A_LENGTH || L1_DATA_B_LENGTH) {
		d_tbl[i_d].addr = L1_DATA_A_START;
		d_tbl[i_d++].data = L1_DMEMORY | PAGE_SIZE_4MB;
	}
	i_tbl[i_i].addr = L1_CODE_START;
	i_tbl[i_i++].data = L1_IMEMORY | PAGE_SIZE_4MB;

	first_switched_dcplb = i_d;
	first_switched_icplb = i_i;

	BUG_ON(first_switched_dcplb > MAX_CPLBS);
	BUG_ON(first_switched_icplb > MAX_CPLBS);

	while (i_d < MAX_CPLBS)
		d_tbl[i_d++].data = 0;
	while (i_i < MAX_CPLBS)
		i_tbl[i_i++].data = 0;
}

void __init generate_cplb_tables_all(void)
{
	int i_d, i_i;

	i_d = 0;
	/* Normal RAM, including MTD FS.  */
#ifdef CONFIG_MTD_UCLINUX
	dcplb_bounds[i_d].eaddr = memory_mtd_start + mtd_size;
#else
	dcplb_bounds[i_d].eaddr = memory_end;
#endif
	dcplb_bounds[i_d++].data = SDRAM_DGENERIC;
	/* DMA uncached region.  */
	if (DMA_UNCACHED_REGION) {
		dcplb_bounds[i_d].eaddr = _ramend;
		dcplb_bounds[i_d++].data = SDRAM_DNON_CHBL;
	}
	if (_ramend != physical_mem_end) {
		/* Reserved memory.  */
		dcplb_bounds[i_d].eaddr = physical_mem_end;
		dcplb_bounds[i_d++].data = (reserved_mem_dcache_on ?
					    SDRAM_DGENERIC : SDRAM_DNON_CHBL);
	}
	/* Addressing hole up to the async bank.  */
	dcplb_bounds[i_d].eaddr = ASYNC_BANK0_BASE;
	dcplb_bounds[i_d++].data = 0;
	/* ASYNC banks.  */
	dcplb_bounds[i_d].eaddr = ASYNC_BANK3_BASE + ASYNC_BANK3_SIZE;
	dcplb_bounds[i_d++].data = SDRAM_EBIU;
	/* Addressing hole up to BootROM.  */
	dcplb_bounds[i_d].eaddr = BOOT_ROM_START;
	dcplb_bounds[i_d++].data = 0;
	/* BootROM -- largest one should be less than 1 meg.  */
	dcplb_bounds[i_d].eaddr = BOOT_ROM_START + (1 * 1024 * 1024);
	dcplb_bounds[i_d++].data = SDRAM_DGENERIC;
	if (L2_LENGTH) {
		/* Addressing hole up to L2 SRAM.  */
		dcplb_bounds[i_d].eaddr = L2_START;
		dcplb_bounds[i_d++].data = 0;
		/* L2 SRAM.  */
		dcplb_bounds[i_d].eaddr = L2_START + L2_LENGTH;
		dcplb_bounds[i_d++].data = L2_DMEMORY;
	}
	dcplb_nr_bounds = i_d;
	BUG_ON(dcplb_nr_bounds > ARRAY_SIZE(dcplb_bounds));

	i_i = 0;
	/* Normal RAM, including MTD FS.  */
#ifdef CONFIG_MTD_UCLINUX
	icplb_bounds[i_i].eaddr = memory_mtd_start + mtd_size;
#else
	icplb_bounds[i_i].eaddr = memory_end;
#endif
	icplb_bounds[i_i++].data = SDRAM_IGENERIC;
	/* DMA uncached region.  */
	if (DMA_UNCACHED_REGION) {
		icplb_bounds[i_i].eaddr = _ramend;
		icplb_bounds[i_i++].data = 0;
	}
	if (_ramend != physical_mem_end) {
		/* Reserved memory.  */
		icplb_bounds[i_i].eaddr = physical_mem_end;
		icplb_bounds[i_i++].data = (reserved_mem_icache_on ?
					    SDRAM_IGENERIC : SDRAM_INON_CHBL);
	}
	/* Addressing hole up to BootROM.  */
	icplb_bounds[i_i].eaddr = BOOT_ROM_START;
	icplb_bounds[i_i++].data = 0;
	/* BootROM -- largest one should be less than 1 meg.  */
	icplb_bounds[i_i].eaddr = BOOT_ROM_START + (1 * 1024 * 1024);
	icplb_bounds[i_i++].data = SDRAM_IGENERIC;
	if (L2_LENGTH) {
		/* Addressing hole up to L2 SRAM, including the async bank.  */
		icplb_bounds[i_i].eaddr = L2_START;
		icplb_bounds[i_i++].data = 0;
		/* L2 SRAM.  */
		icplb_bounds[i_i].eaddr = L2_START + L2_LENGTH;
		icplb_bounds[i_i++].data = L2_IMEMORY;
	}
	icplb_nr_bounds = i_i;
	BUG_ON(icplb_nr_bounds > ARRAY_SIZE(icplb_bounds));
}
