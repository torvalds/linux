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

#ifdef CONFIG_ERRATA_THEAD
#define	ERRATA_THEAD_PBMT 0
#define	ERRATA_THEAD_NUMBER 1
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
#define ALT_THEAD_PBMT_SHIFT 59
#define ALT_SVPBMT(_val, prot)						\
asm(ALTERNATIVE_2("li %0, 0\t\nnop",					\
		  "li %0, %1\t\nslli %0,%0,%3", 0,			\
			CPUFEATURE_SVPBMT, CONFIG_RISCV_ISA_SVPBMT,	\
		  "li %0, %2\t\nslli %0,%0,%4", THEAD_VENDOR_ID,	\
			ERRATA_THEAD_PBMT, CONFIG_ERRATA_THEAD_PBMT)	\
		: "=r"(_val)						\
		: "I"(prot##_SVPBMT >> ALT_SVPBMT_SHIFT),		\
		  "I"(prot##_THEAD >> ALT_THEAD_PBMT_SHIFT),		\
		  "I"(ALT_SVPBMT_SHIFT),				\
		  "I"(ALT_THEAD_PBMT_SHIFT))

#ifdef CONFIG_ERRATA_THEAD_PBMT
/*
 * IO/NOCACHE memory types are handled together with svpbmt,
 * so on T-Head chips, check if no other memory type is set,
 * and set the non-0 PMA type if applicable.
 */
#define ALT_THEAD_PMA(_val)						\
asm volatile(ALTERNATIVE(						\
	"nop\n\t"							\
	"nop\n\t"							\
	"nop\n\t"							\
	"nop\n\t"							\
	"nop\n\t"							\
	"nop\n\t"							\
	"nop",								\
	"li      t3, %2\n\t"						\
	"slli    t3, t3, %4\n\t"					\
	"and     t3, %0, t3\n\t"					\
	"bne     t3, zero, 2f\n\t"					\
	"li      t3, %3\n\t"						\
	"slli    t3, t3, %4\n\t"					\
	"or      %0, %0, t3\n\t"					\
	"2:",  THEAD_VENDOR_ID,						\
		ERRATA_THEAD_PBMT, CONFIG_ERRATA_THEAD_PBMT)		\
	: "+r"(_val)							\
	: "0"(_val),							\
	  "I"(_PAGE_MTMASK_THEAD >> ALT_THEAD_PBMT_SHIFT),		\
	  "I"(_PAGE_PMA_THEAD >> ALT_THEAD_PBMT_SHIFT),			\
	  "I"(ALT_THEAD_PBMT_SHIFT))
#else
#define ALT_THEAD_PMA(_val)
#endif

#endif /* __ASSEMBLY__ */

#endif
