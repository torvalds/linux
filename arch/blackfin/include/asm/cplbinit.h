/*
 * File:         include/asm-blackfin/cplbinit.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
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

#ifndef __ASM_CPLBINIT_H__
#define __ASM_CPLBINIT_H__

#include <asm/blackfin.h>
#include <asm/cplb.h>

#ifdef CONFIG_MPU

#include <asm/cplb-mpu.h>
extern void bfin_icache_init(struct cplb_entry *icplb_tbl);
extern void bfin_dcache_init(struct cplb_entry *icplb_tbl);

#else

#define INITIAL_T 0x1
#define SWITCH_T  0x2
#define I_CPLB    0x4
#define D_CPLB    0x8

#define ASYNC_MEMORY_CPLB_COVERAGE  ((ASYNC_BANK0_SIZE + ASYNC_BANK1_SIZE + \
				ASYNC_BANK2_SIZE + ASYNC_BANK3_SIZE) / SIZE_4M)

#define CPLB_MEM CONFIG_MAX_MEM_SIZE

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

/* Number of CPLB table entries, used for cplb-nompu. */
#define CPLB_TBL_ENTRIES (16 * 4)

enum {
	ZERO_P, L1I_MEM, L1D_MEM, L2_MEM, SDRAM_KERN, SDRAM_RAM_MTD, SDRAM_DMAZ,
	RES_MEM, ASYNC_MEM, OCB_ROM
};

struct cplb_desc {
	u32 start; /* start address */
	u32 end; /* end address */
	u32 psize; /* prefered size if any otherwise 1MB or 4MB*/
	u16 attr;/* attributes */
	u16 i_conf;/* I-CPLB DATA */
	u16 d_conf;/* D-CPLB DATA */
	u16 valid;/* valid */
	const s8 name[30];/* name */
};

struct cplb_tab {
  u_long *tab;
	u16 pos;
	u16 size;
};

extern u_long icplb_tables[NR_CPUS][CPLB_TBL_ENTRIES+1];
extern u_long dcplb_tables[NR_CPUS][CPLB_TBL_ENTRIES+1];

/* Till here we are discussing about the static memory management model.
 * However, the operating envoronments commonly define more CPLB
 * descriptors to cover the entire addressable memory than will fit into
 * the available on-chip 16 CPLB MMRs. When this happens, the below table
 * will be used which will hold all the potentially required CPLB descriptors
 *
 * This is how Page descriptor Table is implemented in uClinux/Blackfin.
 */

extern u_long ipdt_tables[NR_CPUS][MAX_SWITCH_I_CPLBS+1];
extern u_long dpdt_tables[NR_CPUS][MAX_SWITCH_D_CPLBS+1];
#ifdef CONFIG_CPLB_INFO
extern u_long ipdt_swapcount_tables[NR_CPUS][MAX_SWITCH_I_CPLBS];
extern u_long dpdt_swapcount_tables[NR_CPUS][MAX_SWITCH_D_CPLBS];
#endif
extern void bfin_icache_init(u_long icplbs[]);
extern void bfin_dcache_init(u_long dcplbs[]);

#endif /* CONFIG_MPU */

#if defined(CONFIG_BFIN_DCACHE) || defined(CONFIG_BFIN_ICACHE)
extern void generate_cplb_tables_cpu(unsigned int cpu);
#endif
#endif
