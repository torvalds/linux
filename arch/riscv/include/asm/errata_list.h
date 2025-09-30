/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Sifive.
 */
#ifndef ASM_ERRATA_LIST_H
#define ASM_ERRATA_LIST_H

#include <asm/csr.h>
#include <asm/insn-def.h>
#include <asm/hwcap.h>
#include <asm/vendorid_list.h>
#include <asm/errata_list_vendors.h>
#include <asm/vendor_extensions/mips.h>

#ifdef __ASSEMBLER__

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
#else /* !__ASSEMBLER__ */

#define ALT_SFENCE_VMA_ASID(asid)					\
asm(ALTERNATIVE("sfence.vma x0, %0", "sfence.vma", SIFIVE_VENDOR_ID,	\
		ERRATA_SIFIVE_CIP_1200, CONFIG_ERRATA_SIFIVE_CIP_1200)	\
		: : "r" (asid) : "memory")

#define ALT_SFENCE_VMA_ADDR(addr)					\
asm(ALTERNATIVE("sfence.vma %0", "sfence.vma", SIFIVE_VENDOR_ID,	\
		ERRATA_SIFIVE_CIP_1200, CONFIG_ERRATA_SIFIVE_CIP_1200)	\
		: : "r" (addr) : "memory")

#define ALT_SFENCE_VMA_ADDR_ASID(addr, asid)				\
asm(ALTERNATIVE("sfence.vma %0, %1", "sfence.vma", SIFIVE_VENDOR_ID,	\
		ERRATA_SIFIVE_CIP_1200, CONFIG_ERRATA_SIFIVE_CIP_1200)	\
		: : "r" (addr), "r" (asid) : "memory")

#define ALT_RISCV_PAUSE()					\
asm(ALTERNATIVE(	\
		RISCV_PAUSE, /* Original RISC‑V pause insn */	\
		MIPS_PAUSE, /* Replacement for MIPS P8700 */	\
		MIPS_VENDOR_ID, /* Vendor ID to match */	\
		ERRATA_MIPS_P8700_PAUSE_OPCODE, /* patch_id */	\
		CONFIG_ERRATA_MIPS_P8700_PAUSE_OPCODE)	\
	: /* no outputs */	\
	: /* no inputs */	\
	: "memory")

/*
 * _val is marked as "will be overwritten", so need to set it to 0
 * in the default case.
 */
#define ALT_SVPBMT_SHIFT 61
#define ALT_THEAD_MAE_SHIFT 59
#define ALT_SVPBMT(_val, prot)						\
asm(ALTERNATIVE_2("li %0, 0\t\nnop",					\
		  "li %0, %1\t\nslli %0,%0,%3", 0,			\
			RISCV_ISA_EXT_SVPBMT, CONFIG_RISCV_ISA_SVPBMT,	\
		  "li %0, %2\t\nslli %0,%0,%4", THEAD_VENDOR_ID,	\
			ERRATA_THEAD_MAE, CONFIG_ERRATA_THEAD_MAE)	\
		: "=r"(_val)						\
		: "I"(prot##_SVPBMT >> ALT_SVPBMT_SHIFT),		\
		  "I"(prot##_THEAD >> ALT_THEAD_MAE_SHIFT),		\
		  "I"(ALT_SVPBMT_SHIFT),				\
		  "I"(ALT_THEAD_MAE_SHIFT))

#ifdef CONFIG_ERRATA_THEAD_MAE
/*
 * IO/NOCACHE memory types are handled together with svpbmt,
 * so on T-Head chips, check if no other memory type is set,
 * and set the non-0 PMA type if applicable.
 */
#define ALT_THEAD_PMA(_val)						\
asm volatile(ALTERNATIVE(						\
	__nops(7),							\
	"li      t3, %1\n\t"						\
	"slli    t3, t3, %3\n\t"					\
	"and     t3, %0, t3\n\t"					\
	"bne     t3, zero, 2f\n\t"					\
	"li      t3, %2\n\t"						\
	"slli    t3, t3, %3\n\t"					\
	"or      %0, %0, t3\n\t"					\
	"2:",  THEAD_VENDOR_ID,						\
		ERRATA_THEAD_MAE, CONFIG_ERRATA_THEAD_MAE)		\
	: "+r"(_val)							\
	: "I"(_PAGE_MTMASK_THEAD >> ALT_THEAD_MAE_SHIFT),		\
	  "I"(_PAGE_PMA_THEAD >> ALT_THEAD_MAE_SHIFT),			\
	  "I"(ALT_THEAD_MAE_SHIFT)					\
	: "t3")
#else
#define ALT_THEAD_PMA(_val)
#endif

#define ALT_CMO_OP(_op, _start, _size, _cachesize)			\
asm volatile(ALTERNATIVE(						\
	__nops(5),							\
	"mv a0, %1\n\t"							\
	"j 2f\n\t"							\
	"3:\n\t"							\
	CBO_##_op(a0)							\
	"add a0, a0, %0\n\t"						\
	"2:\n\t"							\
	"bltu a0, %2, 3b\n\t",						\
	0, RISCV_ISA_EXT_ZICBOM, CONFIG_RISCV_ISA_ZICBOM)		\
	: : "r"(_cachesize),						\
	    "r"((unsigned long)(_start) & ~((_cachesize) - 1UL)),	\
	    "r"((unsigned long)(_start) + (_size))			\
	: "a0")

#define THEAD_C9XX_RV_IRQ_PMU			17
#define THEAD_C9XX_CSR_SCOUNTEROF		0x5c5

#endif /* __ASSEMBLER__ */

#endif
