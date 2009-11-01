#ifndef _BLACKFIN_SECTIONS_H
#define _BLACKFIN_SECTIONS_H

/* only used when MTD_UCLINUX */
extern unsigned long memory_mtd_start, memory_mtd_end, mtd_size;

extern unsigned long _ramstart, _ramend, _rambase;
extern unsigned long memory_start, memory_end, physical_mem_end;

extern char _stext_l1[], _etext_l1[], _sdata_l1[], _edata_l1[], _sbss_l1[],
	_ebss_l1[], _l1_lma_start[], _sdata_b_l1[], _sbss_b_l1[], _ebss_b_l1[],
	_stext_l2[], _etext_l2[], _sdata_l2[], _edata_l2[], _sbss_l2[],
	_ebss_l2[], _l2_lma_start[];

#include <asm/mem_map.h>

/* Blackfin systems have discontinuous memory map and no virtualized memory */
static inline int arch_is_kernel_text(unsigned long addr)
{
	return
		(L1_CODE_LENGTH &&
		 addr >= (unsigned long)_stext_l1 &&
		 addr <  (unsigned long)_etext_l1)
		||
		(L2_LENGTH &&
		 addr >= (unsigned long)_stext_l2 &&
		 addr <  (unsigned long)_etext_l2);
}
#define arch_is_kernel_text(addr) arch_is_kernel_text(addr)

static inline int arch_is_kernel_data(unsigned long addr)
{
	return
		(L1_DATA_A_LENGTH &&
		 addr >= (unsigned long)_sdata_l1 &&
		 addr <  (unsigned long)_ebss_l1)
		||
		(L1_DATA_B_LENGTH &&
		 addr >= (unsigned long)_sdata_b_l1 &&
		 addr <  (unsigned long)_ebss_b_l1)
		||
		(L2_LENGTH &&
		 addr >= (unsigned long)_sdata_l2 &&
		 addr <  (unsigned long)_ebss_l2);
}
#define arch_is_kernel_data(addr) arch_is_kernel_data(addr)

#include <asm-generic/sections.h>

#endif
