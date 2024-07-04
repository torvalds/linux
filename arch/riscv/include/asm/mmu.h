/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */


#ifndef _ASM_RISCV_MMU_H
#define _ASM_RISCV_MMU_H

#ifndef __ASSEMBLY__

typedef struct {
#ifndef CONFIG_MMU
	unsigned long	end_brk;
#else
	atomic_long_t id;
#endif
	void *vdso;
#ifdef CONFIG_SMP
	/* A local icache flush is needed before user execution can resume. */
	cpumask_t icache_stale_mask;
	/* Force local icache flush on all migrations. */
	bool force_icache_flush;
#endif
#ifdef CONFIG_BINFMT_ELF_FDPIC
	unsigned long exec_fdpic_loadmap;
	unsigned long interp_fdpic_loadmap;
#endif
} mm_context_t;

#define cntx2asid(cntx)		((cntx) & SATP_ASID_MASK)
#define cntx2version(cntx)	((cntx) & ~SATP_ASID_MASK)

void __init create_pgd_mapping(pgd_t *pgdp, uintptr_t va, phys_addr_t pa,
			       phys_addr_t sz, pgprot_t prot);
#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_MMU_H */
