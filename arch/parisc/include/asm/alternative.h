/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PARISC_ALTERNATIVE_H
#define __ASM_PARISC_ALTERNATIVE_H

#define ALT_COND_NO_SMP		0x01	/* when running UP instead of SMP */
#define ALT_COND_NO_DCACHE	0x02	/* if system has no d-cache  */
#define ALT_COND_NO_ICACHE	0x04	/* if system has no i-cache  */
#define ALT_COND_NO_SPLIT_TLB	0x08	/* if split_tlb == 0  */
#define ALT_COND_NO_IOC_FDC	0x10	/* if I/O cache does not need flushes */

#define INSN_PxTLB	0x02		/* modify pdtlb, pitlb */
#define INSN_NOP	0x08000240	/* nop */

#ifndef __ASSEMBLY__

#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/stringify.h>

struct alt_instr {
	s32 orig_offset;	/* offset to original instructions */
	u32 len;		/* end of original instructions */
	u32 cond;		/* see ALT_COND_XXX */
	u32 replacement;	/* replacement instruction or code */
};

void set_kernel_text_rw(int enable_read_write);

/* Alternative SMP implementation. */
#define ALTERNATIVE(cond, replacement)		"!0:"	\
	".section .altinstructions, \"aw\"	!"	\
	".word (0b-4-.), 1, " __stringify(cond) ","	\
		__stringify(replacement) "	!"	\
	".previous"

#else

#define ALTERNATIVE(from, to, cond, replacement)\
	.section .altinstructions, "aw"	!	\
	.word (from - .), (to - from)/4	!	\
	.word cond, replacement		!	\
	.previous

#endif  /*  __ASSEMBLY__  */

#endif /* __ASM_PARISC_ALTERNATIVE_H */
