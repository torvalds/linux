/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IBT_H
#define _ASM_X86_IBT_H

#include <linux/types.h>

/*
 * The rules for enabling IBT are:
 *
 *  - CC_HAS_IBT:         the toolchain supports it
 *  - X86_KERNEL_IBT:     it is selected in Kconfig
 *  - !__DISABLE_EXPORTS: this is regular kernel code
 *
 * Esp. that latter one is a bit non-obvious, but some code like compressed,
 * purgatory, realmode etc.. is built with custom CFLAGS that do not include
 * -fcf-protection=branch and things will go *bang*.
 *
 * When all the above are satisfied, HAS_KERNEL_IBT will be 1, otherwise 0.
 */
#if defined(CONFIG_X86_KERNEL_IBT) && !defined(__DISABLE_EXPORTS)

#define HAS_KERNEL_IBT	1

#ifndef __ASSEMBLER__

#ifdef CONFIG_X86_64
#define ASM_ENDBR	"endbr64\n\t"
#else
#define ASM_ENDBR	"endbr32\n\t"
#endif

#define __noendbr	__attribute__((nocf_check))

/*
 * Create a dummy function pointer reference to prevent objtool from marking
 * the function as needing to be "sealed" (i.e. ENDBR converted to NOP by
 * apply_seal_endbr()).
 */
#define IBT_NOSEAL(fname)				\
	".pushsection .discard.ibt_endbr_noseal\n\t"	\
	_ASM_PTR fname "\n\t"				\
	".popsection\n\t"

static __always_inline __attribute_const__ u32 gen_endbr(void)
{
	u32 endbr;

	/*
	 * Generate ENDBR64 in a way that is sure to not result in
	 * an ENDBR64 instruction as immediate.
	 */
	asm ( "mov $~0xfa1e0ff3, %[endbr]\n\t"
	      "not %[endbr]\n\t"
	       : [endbr] "=&r" (endbr) );

	return endbr;
}

static __always_inline __attribute_const__ u32 gen_endbr_poison(void)
{
	/*
	 * 4 byte NOP that isn't NOP4, such that it will be unique to (former)
	 * ENDBR sites. Additionally it carries UDB as immediate.
	 */
	return 0xd6401f0f; /* nopl -42(%rax) */
}

static inline bool __is_endbr(u32 val)
{
	if (val == gen_endbr_poison())
		return true;

	val &= ~0x01000000U; /* ENDBR32 -> ENDBR64 */
	return val == gen_endbr();
}

extern __noendbr bool is_endbr(u32 *val);
extern __noendbr u64 ibt_save(bool disable);
extern __noendbr void ibt_restore(u64 save);

#else /* __ASSEMBLER__ */

#ifdef CONFIG_X86_64
#define ENDBR	endbr64
#else
#define ENDBR	endbr32
#endif

#endif /* __ASSEMBLER__ */

#else /* !IBT */

#define HAS_KERNEL_IBT	0

#ifndef __ASSEMBLER__

#define ASM_ENDBR
#define IBT_NOSEAL(name)

#define __noendbr

static inline bool is_endbr(u32 *val) { return false; }

static inline u64 ibt_save(bool disable) { return 0; }
static inline void ibt_restore(u64 save) { }

#else /* __ASSEMBLER__ */

#define ENDBR

#endif /* __ASSEMBLER__ */

#endif /* CONFIG_X86_KERNEL_IBT */

#define ENDBR_INSN_SIZE		(4*HAS_KERNEL_IBT)

#endif /* _ASM_X86_IBT_H */
