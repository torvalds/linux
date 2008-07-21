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
#include <asm/cplb.h>
#include <asm/cplbinit.h>

#ifdef CONFIG_MAX_MEM_SIZE
# define CPLB_MEM CONFIG_MAX_MEM_SIZE
#else
# define CPLB_MEM CONFIG_MEM_SIZE
#endif

/*
* Number of required data CPLB switchtable entries
* MEMSIZE / 4 (we mostly install 4M page size CPLBs
* approx 16 for smaller 1MB page size CPLBs for allignment purposes
* 1 for L1 Data Memory
* possibly 1 for L2 Data Memory
* 1 for CONFIG_DEBUG_HUNT_FOR_ZERO
* 1 for ASYNC Memory
*/
#define MAX_SWITCH_D_CPLBS (((CPLB_MEM / 4) + 16 + 1 + 1 + 1 \
				 + ASYNC_MEMORY_CPLB_COVERAGE) * 2)

/*
* Number of required instruction CPLB switchtable entries
* MEMSIZE / 4 (we mostly install 4M page size CPLBs
* approx 12 for smaller 1MB page size CPLBs for allignment purposes
* 1 for L1 Instruction Memory
* possibly 1 for L2 Instruction Memory
* 1 for CONFIG_DEBUG_HUNT_FOR_ZERO
*/
#define MAX_SWITCH_I_CPLBS (((CPLB_MEM / 4) + 12 + 1 + 1 + 1) * 2)


u_long icplb_table[MAX_CPLBS + 1];
u_long dcplb_table[MAX_CPLBS + 1];

#ifdef CONFIG_CPLB_SWITCH_TAB_L1
# define PDT_ATTR __attribute__((l1_data))
#else
# define PDT_ATTR
#endif

u_long ipdt_table[MAX_SWITCH_I_CPLBS + 1] PDT_ATTR;
u_long dpdt_table[MAX_SWITCH_D_CPLBS + 1] PDT_ATTR;

#ifdef CONFIG_CPLB_INFO
u_long ipdt_swapcount_table[MAX_SWITCH_I_CPLBS] PDT_ATTR;
u_long dpdt_swapcount_table[MAX_SWITCH_D_CPLBS] PDT_ATTR;
#endif

struct s_cplb {
	struct cplb_tab init_i;
	struct cplb_tab init_d;
	struct cplb_tab switch_i;
	struct cplb_tab switch_d;
};

#if defined(CONFIG_BFIN_DCACHE) || defined(CONFIG_BFIN_ICACHE)
static struct cplb_desc cplb_data[] = {
	{
		.start = 0,
		.end = SIZE_1K,
		.psize = SIZE_1K,
		.attr = INITIAL_T | SWITCH_T | I_CPLB | D_CPLB,
		.i_conf = SDRAM_OOPS,
		.d_conf = SDRAM_OOPS,
#if defined(CONFIG_DEBUG_HUNT_FOR_ZERO)
		.valid = 1,
#else
		.valid = 0,
#endif
		.name = "Zero Pointer Guard Page",
	},
	{
		.start = L1_CODE_START,
		.end = L1_CODE_START + L1_CODE_LENGTH,
		.psize = SIZE_4M,
		.attr = INITIAL_T | SWITCH_T | I_CPLB,
		.i_conf = L1_IMEMORY,
		.d_conf = 0,
		.valid = 1,
		.name = "L1 I-Memory",
	},
	{
		.start = L1_DATA_A_START,
		.end = L1_DATA_B_START + L1_DATA_B_LENGTH,
		.psize = SIZE_4M,
		.attr = INITIAL_T | SWITCH_T | D_CPLB,
		.i_conf = 0,
		.d_conf = L1_DMEMORY,
#if ((L1_DATA_A_LENGTH > 0) || (L1_DATA_B_LENGTH > 0))
		.valid = 1,
#else
		.valid = 0,
#endif
		.name = "L1 D-Memory",
	},
	{
		.start = 0,
		.end = 0,  /* dynamic */
		.psize = 0,
		.attr = INITIAL_T | SWITCH_T | I_CPLB | D_CPLB,
		.i_conf = SDRAM_IGENERIC,
		.d_conf = SDRAM_DGENERIC,
		.valid = 1,
		.name = "Kernel Memory",
	},
	{
		.start = 0, /* dynamic */
		.end = 0, /* dynamic */
		.psize = 0,
		.attr = INITIAL_T | SWITCH_T | D_CPLB,
		.i_conf = SDRAM_IGENERIC,
		.d_conf = SDRAM_DNON_CHBL,
		.valid = 1,
		.name = "uClinux MTD Memory",
	},
	{
		.start = 0, /* dynamic */
		.end = 0,   /* dynamic */
		.psize = SIZE_1M,
		.attr = INITIAL_T | SWITCH_T | D_CPLB,
		.d_conf = SDRAM_DNON_CHBL,
		.valid = 1,
		.name = "Uncached DMA Zone",
	},
	{
		.start = 0, /* dynamic */
		.end = 0, /* dynamic */
		.psize = 0,
		.attr = SWITCH_T | D_CPLB,
		.i_conf = 0, /* dynamic */
		.d_conf = 0, /* dynamic */
		.valid = 1,
		.name = "Reserved Memory",
	},
	{
		.start = ASYNC_BANK0_BASE,
		.end = ASYNC_BANK3_BASE + ASYNC_BANK3_SIZE,
		.psize = 0,
		.attr = SWITCH_T | D_CPLB,
		.d_conf = SDRAM_EBIU,
		.valid = 1,
		.name = "Asynchronous Memory Banks",
	},
	{
#ifdef L2_START
		.start = L2_START,
		.end = L2_START + L2_LENGTH,
		.psize = SIZE_1M,
		.attr = SWITCH_T | I_CPLB | D_CPLB,
		.i_conf = L2_MEMORY,
		.d_conf = L2_MEMORY,
		.valid = 1,
#else
		.valid = 0,
#endif
		.name = "L2 Memory",
	},
	{
		.start = BOOT_ROM_START,
		.end = BOOT_ROM_START + BOOT_ROM_LENGTH,
		.psize = SIZE_1M,
		.attr = SWITCH_T | I_CPLB | D_CPLB,
		.i_conf = SDRAM_IGENERIC,
		.d_conf = SDRAM_DGENERIC,
		.valid = 1,
		.name = "On-Chip BootROM",
	},
};

static u16 __init lock_kernel_check(u32 start, u32 end)
{
	if ((end   <= (u32) _end && end   >= (u32)_stext) ||
	    (start <= (u32) _end && start >= (u32)_stext))
		return IN_KERNEL;
	return 0;
}

static unsigned short __init
fill_cplbtab(struct cplb_tab *table,
	     unsigned long start, unsigned long end,
	     unsigned long block_size, unsigned long cplb_data)
{
	int i;

	switch (block_size) {
	case SIZE_4M:
		i = 3;
		break;
	case SIZE_1M:
		i = 2;
		break;
	case SIZE_4K:
		i = 1;
		break;
	case SIZE_1K:
	default:
		i = 0;
		break;
	}

	cplb_data = (cplb_data & ~(3 << 16)) | (i << 16);

	while ((start < end) && (table->pos < table->size)) {

		table->tab[table->pos++] = start;

		if (lock_kernel_check(start, start + block_size) == IN_KERNEL)
			table->tab[table->pos++] =
			    cplb_data | CPLB_LOCK | CPLB_DIRTY;
		else
			table->tab[table->pos++] = cplb_data;

		start += block_size;
	}
	return 0;
}

static unsigned short __init
close_cplbtab(struct cplb_tab *table)
{

	while (table->pos < table->size) {

		table->tab[table->pos++] = 0;
		table->tab[table->pos++] = 0; /* !CPLB_VALID */
	}
	return 0;
}

/* helper function */
static void __init
__fill_code_cplbtab(struct cplb_tab *t, int i, u32 a_start, u32 a_end)
{
	if (cplb_data[i].psize) {
		fill_cplbtab(t,
				cplb_data[i].start,
				cplb_data[i].end,
				cplb_data[i].psize,
				cplb_data[i].i_conf);
	} else {
#if defined(CONFIG_BFIN_ICACHE)
		if (ANOMALY_05000263 && i == SDRAM_KERN) {
			fill_cplbtab(t,
					cplb_data[i].start,
					cplb_data[i].end,
					SIZE_4M,
					cplb_data[i].i_conf);
		} else
#endif
		{
			fill_cplbtab(t,
					cplb_data[i].start,
					a_start,
					SIZE_1M,
					cplb_data[i].i_conf);
			fill_cplbtab(t,
					a_start,
					a_end,
					SIZE_4M,
					cplb_data[i].i_conf);
			fill_cplbtab(t, a_end,
					cplb_data[i].end,
					SIZE_1M,
					cplb_data[i].i_conf);
		}
	}
}

static void __init
__fill_data_cplbtab(struct cplb_tab *t, int i, u32 a_start, u32 a_end)
{
	if (cplb_data[i].psize) {
		fill_cplbtab(t,
				cplb_data[i].start,
				cplb_data[i].end,
				cplb_data[i].psize,
				cplb_data[i].d_conf);
	} else {
		fill_cplbtab(t,
				cplb_data[i].start,
				a_start, SIZE_1M,
				cplb_data[i].d_conf);
		fill_cplbtab(t, a_start,
				a_end, SIZE_4M,
				cplb_data[i].d_conf);
		fill_cplbtab(t, a_end,
				cplb_data[i].end,
				SIZE_1M,
				cplb_data[i].d_conf);
	}
}

void __init generate_cpl_tables(void)
{

	u16 i, j, process;
	u32 a_start, a_end, as, ae, as_1m;

	struct cplb_tab *t_i = NULL;
	struct cplb_tab *t_d = NULL;
	struct s_cplb cplb;

	printk(KERN_INFO "NOMPU: setting up cplb tables for global access\n");

	cplb.init_i.size = MAX_CPLBS;
	cplb.init_d.size = MAX_CPLBS;
	cplb.switch_i.size = MAX_SWITCH_I_CPLBS;
	cplb.switch_d.size = MAX_SWITCH_D_CPLBS;

	cplb.init_i.pos = 0;
	cplb.init_d.pos = 0;
	cplb.switch_i.pos = 0;
	cplb.switch_d.pos = 0;

	cplb.init_i.tab = icplb_table;
	cplb.init_d.tab = dcplb_table;
	cplb.switch_i.tab = ipdt_table;
	cplb.switch_d.tab = dpdt_table;

	cplb_data[SDRAM_KERN].end = memory_end;

#ifdef CONFIG_MTD_UCLINUX
	cplb_data[SDRAM_RAM_MTD].start = memory_mtd_start;
	cplb_data[SDRAM_RAM_MTD].end = memory_mtd_start + mtd_size;
	cplb_data[SDRAM_RAM_MTD].valid = mtd_size > 0;
# if defined(CONFIG_ROMFS_FS)
	cplb_data[SDRAM_RAM_MTD].attr |= I_CPLB;

	/*
	 * The ROMFS_FS size is often not multiple of 1MB.
	 * This can cause multiple CPLB sets covering the same memory area.
	 * This will then cause multiple CPLB hit exceptions.
	 * Workaround: We ensure a contiguous memory area by extending the kernel
	 * memory section over the mtd section.
	 * For ROMFS_FS memory must be covered with ICPLBs anyways.
	 * So there is no difference between kernel and mtd memory setup.
	 */

	cplb_data[SDRAM_KERN].end = memory_mtd_start + mtd_size;;
	cplb_data[SDRAM_RAM_MTD].valid = 0;

# endif
#else
	cplb_data[SDRAM_RAM_MTD].valid = 0;
#endif

	cplb_data[SDRAM_DMAZ].start = _ramend - DMA_UNCACHED_REGION;
	cplb_data[SDRAM_DMAZ].end = _ramend;

	cplb_data[RES_MEM].start = _ramend;
	cplb_data[RES_MEM].end = physical_mem_end;

	if (reserved_mem_dcache_on)
		cplb_data[RES_MEM].d_conf = SDRAM_DGENERIC;
	else
		cplb_data[RES_MEM].d_conf = SDRAM_DNON_CHBL;

	if (reserved_mem_icache_on)
		cplb_data[RES_MEM].i_conf = SDRAM_IGENERIC;
	else
		cplb_data[RES_MEM].i_conf = SDRAM_INON_CHBL;

	for (i = ZERO_P; i < ARRAY_SIZE(cplb_data); ++i) {
		if (!cplb_data[i].valid)
			continue;

		as_1m = cplb_data[i].start % SIZE_1M;

		/* We need to make sure all sections are properly 1M aligned
		 * However between Kernel Memory and the Kernel mtd section, depending on the
		 * rootfs size, there can be overlapping memory areas.
		 */

		if (as_1m && i != L1I_MEM && i != L1D_MEM) {
#ifdef CONFIG_MTD_UCLINUX
			if (i == SDRAM_RAM_MTD) {
				if ((cplb_data[SDRAM_KERN].end + 1) > cplb_data[SDRAM_RAM_MTD].start)
					cplb_data[SDRAM_RAM_MTD].start = (cplb_data[i].start & (-2*SIZE_1M)) + SIZE_1M;
				else
					cplb_data[SDRAM_RAM_MTD].start = (cplb_data[i].start & (-2*SIZE_1M));
			} else
#endif
				printk(KERN_WARNING "Unaligned Start of %s at 0x%X\n",
				       cplb_data[i].name, cplb_data[i].start);
		}

		as = cplb_data[i].start % SIZE_4M;
		ae = cplb_data[i].end % SIZE_4M;

		if (as)
			a_start = cplb_data[i].start + (SIZE_4M - (as));
		else
			a_start = cplb_data[i].start;

		a_end = cplb_data[i].end - ae;

		for (j = INITIAL_T; j <= SWITCH_T; j++) {

			switch (j) {
			case INITIAL_T:
				if (cplb_data[i].attr & INITIAL_T) {
					t_i = &cplb.init_i;
					t_d = &cplb.init_d;
					process = 1;
				} else
					process = 0;
				break;
			case SWITCH_T:
				if (cplb_data[i].attr & SWITCH_T) {
					t_i = &cplb.switch_i;
					t_d = &cplb.switch_d;
					process = 1;
				} else
					process = 0;
				break;
			default:
					process = 0;
				break;
			}

			if (!process)
				continue;
			if (cplb_data[i].attr & I_CPLB)
				__fill_code_cplbtab(t_i, i, a_start, a_end);

			if (cplb_data[i].attr & D_CPLB)
				__fill_data_cplbtab(t_d, i, a_start, a_end);
		}
	}

/* close tables */

	close_cplbtab(&cplb.init_i);
	close_cplbtab(&cplb.init_d);

	cplb.init_i.tab[cplb.init_i.pos] = -1;
	cplb.init_d.tab[cplb.init_d.pos] = -1;
	cplb.switch_i.tab[cplb.switch_i.pos] = -1;
	cplb.switch_d.tab[cplb.switch_d.pos] = -1;

}

#endif

