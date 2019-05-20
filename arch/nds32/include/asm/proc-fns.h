/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __NDS32_PROCFNS_H__
#define __NDS32_PROCFNS_H__

#ifdef __KERNEL__
#include <asm/page.h>

struct mm_struct;
struct vm_area_struct;
extern void cpu_proc_init(void);
extern void cpu_proc_fin(void);
extern void cpu_do_idle(void);
extern void cpu_reset(unsigned long reset);
extern void cpu_switch_mm(struct mm_struct *mm);

extern void cpu_dcache_inval_all(void);
extern void cpu_dcache_wbinval_all(void);
extern void cpu_dcache_inval_page(unsigned long page);
extern void cpu_dcache_wb_page(unsigned long page);
extern void cpu_dcache_wbinval_page(unsigned long page);
extern void cpu_dcache_inval_range(unsigned long start, unsigned long end);
extern void cpu_dcache_wb_range(unsigned long start, unsigned long end);
extern void cpu_dcache_wbinval_range(unsigned long start, unsigned long end);

extern void cpu_icache_inval_all(void);
extern void cpu_icache_inval_page(unsigned long page);
extern void cpu_icache_inval_range(unsigned long start, unsigned long end);

extern void cpu_cache_wbinval_page(unsigned long page, int flushi);
extern void cpu_cache_wbinval_range(unsigned long start,
				    unsigned long end, int flushi);
extern void cpu_cache_wbinval_range_check(struct vm_area_struct *vma,
					  unsigned long start,
					  unsigned long end, bool flushi,
					  bool wbd);

extern void cpu_dma_wb_range(unsigned long start, unsigned long end);
extern void cpu_dma_inval_range(unsigned long start, unsigned long end);
extern void cpu_dma_wbinval_range(unsigned long start, unsigned long end);

#endif /* __KERNEL__ */
#endif /* __NDS32_PROCFNS_H__ */
