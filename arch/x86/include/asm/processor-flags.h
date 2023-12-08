/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PROCESSOR_FLAGS_H
#define _ASM_X86_PROCESSOR_FLAGS_H

#include <uapi/asm/processor-flags.h>
#include <linux/mem_encrypt.h>

#ifdef CONFIG_VM86
#define X86_VM_MASK	X86_EFLAGS_VM
#else
#define X86_VM_MASK	0 /* No VM86 support */
#endif

/*
 * CR3's layout varies depending on several things.
 *
 * If CR4.PCIDE is set (64-bit only), then CR3[11:0] is the address space ID.
 * If PAE is enabled, then CR3[11:5] is part of the PDPT address
 * (i.e. it's 32-byte aligned, not page-aligned) and CR3[4:0] is ignored.
 * Otherwise (non-PAE, non-PCID), CR3[3] is PWT, CR3[4] is PCD, and
 * CR3[2:0] and CR3[11:5] are ignored.
 *
 * In all cases, Linux puts zeros in the low ignored bits and in PWT and PCD.
 *
 * CR3[63] is always read as zero.  If CR4.PCIDE is set, then CR3[63] may be
 * written as 1 to prevent the write to CR3 from flushing the TLB.
 *
 * On systems with SME, one bit (in a variable position!) is stolen to indicate
 * that the top-level paging structure is encrypted.
 *
 * All of the remaining bits indicate the physical address of the top-level
 * paging structure.
 *
 * CR3_ADDR_MASK is the mask used by read_cr3_pa().
 */
#ifdef CONFIG_X86_64
/* Mask off the address space ID and SME encryption bits. */
#define CR3_ADDR_MASK	__sme_clr(0x7FFFFFFFFFFFF000ull)
#define CR3_PCID_MASK	0xFFFull
#define CR3_NOFLUSH	BIT_ULL(63)

#else
/*
 * CR3_ADDR_MASK needs at least bits 31:5 set on PAE systems, and we save
 * a tiny bit of code size by setting all the bits.
 */
#define CR3_ADDR_MASK	0xFFFFFFFFull
#define CR3_PCID_MASK	0ull
#define CR3_NOFLUSH	0
#endif

#ifdef CONFIG_PAGE_TABLE_ISOLATION
# define X86_CR3_PTI_PCID_USER_BIT	11
#endif

#endif /* _ASM_X86_PROCESSOR_FLAGS_H */
