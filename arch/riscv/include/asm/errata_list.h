/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Sifive.
 */
#ifndef ASM_ERRATA_LIST_H
#define ASM_ERRATA_LIST_H

#include <asm/alternative.h>
#include <asm/vendorid_list.h>

#ifdef CONFIG_ERRATA_SIFIVE
#define	ERRATA_SIFIVE_CIP_453 0
#define	ERRATA_SIFIVE_CIP_1200 1
#define	ERRATA_SIFIVE_NUMBER 2
#endif

#define	CPUFEATURE_SVPBMT 0
#define	CPUFEATURE_NUMBER 1

#ifdef __ASSEMBLY__

#define ALT_INSN_FAULT(x)						\
ALTERNATIVE(__stringify(RISCV_PTR do_trap_insn_fault),			\
	    __stringify(RISCV_PTR sifive_cip_453_insn_fault_trp),	\
	    SIFIVE_VENDOR_ID, ERRATA_SIFIVE_CIP_453,			\
	    CONFIG_ERRATA_SIFIVE_CIP_453)

#define ALT_PAGE_FAULT(x)						\
ALTERNATIVE(__stringify(RISCV_PTR do_page_fault),			\
	    __stringify(RISCV_PTR sifive_cip_453_page_fault_trp),	\
	    SIFIVE_VENDOR_ID, ERRATA_SIFIVE_CIP_453,			\
	    CONFIG_ERRATA_SIFIVE_CIP_453)
#else /* !__ASSEMBLY__ */

#define ALT_FLUSH_TLB_PAGE(x)						\
asm(ALTERNATIVE("sfence.vma %0", "sfence.vma", SIFIVE_VENDOR_ID,	\
		ERRATA_SIFIVE_CIP_1200, CONFIG_ERRATA_SIFIVE_CIP_1200)	\
		: : "r" (addr) : "memory")

/*
 * _val is marked as "will be overwritten", so need to set it to 0
 * in the default case.
 */
#define ALT_SVPBMT_SHIFT 61
#define ALT_SVPBMT(_val, prot)						\
asm(ALTERNATIVE("li %0, 0\t\nnop", "li %0, %1\t\nslli %0,%0,%2", 0,	\
		CPUFEATURE_SVPBMT, CONFIG_RISCV_ISA_SVPBMT)		\
		: "=r"(_val)						\
		: "I"(prot##_SVPBMT >> ALT_SVPBMT_SHIFT),		\
		  "I"(ALT_SVPBMT_SHIFT))

#endif /* __ASSEMBLY__ */

#endif
