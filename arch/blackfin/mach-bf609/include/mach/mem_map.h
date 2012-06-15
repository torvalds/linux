/*
 * BF60x memory map
 *
 * Copyright 2011 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#ifndef __BFIN_MACH_MEM_MAP_H__
#define __BFIN_MACH_MEM_MAP_H__

#ifndef __BFIN_MEM_MAP_H__
# error "do not include mach/mem_map.h directly -- use asm/mem_map.h"
#endif

/* Async Memory Banks */
#define ASYNC_BANK3_BASE	0xBC000000	 /* Async Bank 3 */
#define ASYNC_BANK3_SIZE	0x04000000	/* 64M */
#define ASYNC_BANK2_BASE	0xB8000000	 /* Async Bank 2 */
#define ASYNC_BANK2_SIZE	0x04000000	/* 64M */
#define ASYNC_BANK1_BASE	0xB4000000	 /* Async Bank 1 */
#define ASYNC_BANK1_SIZE	0x04000000	/* 64M */
#define ASYNC_BANK0_BASE	0xB0000000	 /* Async Bank 0 */
#define ASYNC_BANK0_SIZE	0x04000000	/* 64M */

/* Boot ROM Memory */

#define BOOT_ROM_START		0xC8000000
#define BOOT_ROM_LENGTH		0x8000

/* Level 1 Memory */

/* Memory Map for ADSP-BF60x processors */
#ifdef CONFIG_BFIN_ICACHE
#define BFIN_ICACHESIZE	(16*1024)
#define L1_CODE_LENGTH      0x10000
#else
#define BFIN_ICACHESIZE	(0*1024)
#define L1_CODE_LENGTH      0x14000
#endif

#define L1_CODE_START       0xFFA00000
#define L1_DATA_A_START     0xFF800000
#define L1_DATA_B_START     0xFF900000


#define COREA_L1_SCRATCH_START  0xFFB00000
#define COREB_L1_SCRATCH_START  0xFF700000

#define COREB_L1_CODE_START       0xFF600000
#define COREB_L1_DATA_A_START     0xFF400000
#define COREB_L1_DATA_B_START     0xFF500000

#define COREB_L1_CODE_LENGTH     0x14000
#define COREB_L1_DATA_A_LENGTH   0x8000
#define COREB_L1_DATA_B_LENGTH   0x8000


#ifdef CONFIG_BFIN_DCACHE

#ifdef CONFIG_BFIN_DCACHE_BANKA
#define DMEM_CNTR (ACACHE_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x8000 - 0x4000)
#define L1_DATA_B_LENGTH      0x8000
#define BFIN_DCACHESIZE	(16*1024)
#define BFIN_DSUPBANKS	1
#else
#define DMEM_CNTR (ACACHE_BCACHE | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x8000 - 0x4000)
#define L1_DATA_B_LENGTH      (0x8000 - 0x4000)
#define BFIN_DCACHESIZE	(32*1024)
#define BFIN_DSUPBANKS	2
#endif

#else
#define DMEM_CNTR (ASRAM_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      0x8000
#define L1_DATA_B_LENGTH      0x8000
#define BFIN_DCACHESIZE	(0*1024)
#define BFIN_DSUPBANKS	0
#endif /*CONFIG_BFIN_DCACHE*/

/* Level 2 Memory */
#define L2_START            0xC8080000
#define L2_LENGTH           0x40000

#endif
