/*
 * Common CPLB definitions for CPLB init
 *
 * Copyright 2006-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_CPLBINIT_H__
#define __ASM_CPLBINIT_H__

#include <asm/blackfin.h>
#include <asm/cplb.h>
#include <linux/threads.h>

#ifdef CONFIG_CPLB_SWITCH_TAB_L1
# define PDT_ATTR __attribute__((l1_data))
#else
# define PDT_ATTR
#endif

struct cplb_entry {
	unsigned long data, addr;
};

struct cplb_boundary {
	unsigned long eaddr; /* End of this region.  */
	unsigned long data; /* CPLB data value.  */
};

extern struct cplb_boundary dcplb_bounds[];
extern struct cplb_boundary icplb_bounds[];
extern int dcplb_nr_bounds, icplb_nr_bounds;

extern struct cplb_entry dcplb_tbl[NR_CPUS][MAX_CPLBS];
extern struct cplb_entry icplb_tbl[NR_CPUS][MAX_CPLBS];
extern int first_switched_icplb;
extern int first_switched_dcplb;

extern int nr_dcplb_miss[], nr_icplb_miss[], nr_icplb_supv_miss[];
extern int nr_dcplb_prot[], nr_cplb_flush[];

#ifdef CONFIG_MPU

extern int first_mask_dcplb;

extern int page_mask_order;
extern int page_mask_nelts;

extern unsigned long *current_rwx_mask[NR_CPUS];

extern void flush_switched_cplbs(unsigned int);
extern void set_mask_dcplbs(unsigned long *, unsigned int);

extern void __noreturn panic_cplb_error(int seqstat, struct pt_regs *);

#endif /* CONFIG_MPU */

extern void bfin_icache_init(struct cplb_entry *icplb_tbl);
extern void bfin_dcache_init(struct cplb_entry *icplb_tbl);

#if defined(CONFIG_BFIN_DCACHE) || defined(CONFIG_BFIN_ICACHE)
extern void generate_cplb_tables_all(void);
extern void generate_cplb_tables_cpu(unsigned int cpu);
#endif
#endif
