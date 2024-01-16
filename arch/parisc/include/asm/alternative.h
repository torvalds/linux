/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PARISC_ALTERNATIVE_H
#define __ASM_PARISC_ALTERNATIVE_H

#define ALT_COND_ALWAYS		0x80	/* always replace instruction */
#define ALT_COND_NO_SMP		0x01	/* when running UP instead of SMP */
#define ALT_COND_NO_DCACHE	0x02	/* if system has no d-cache  */
#define ALT_COND_NO_ICACHE	0x04	/* if system has no i-cache  */
#define ALT_COND_NO_SPLIT_TLB	0x08	/* if split_tlb == 0  */
#define ALT_COND_NO_IOC_FDC	0x10	/* if I/O cache does not need flushes */
#define ALT_COND_RUN_ON_QEMU	0x20	/* if running on QEMU */

#define INSN_PxTLB	0x02		/* modify pdtlb, pitlb */
#define INSN_NOP	0x08000240	/* nop */

#ifndef __ASSEMBLY__

#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/stringify.h>

struct alt_instr {
	s32 orig_offset;	/* offset to original instructions */
	s16 len;		/* end of original instructions */
	u16 cond;		/* see ALT_COND_XXX */
	u32 replacement;	/* replacement instruction or code */
} __packed;

void set_kernel_text_rw(int enable_read_write);
void apply_alternatives_all(void);
void apply_alternatives(struct alt_instr *start, struct alt_instr *end,
	const char *module_name);

/* Alternative SMP implementation. */
#define ALTERNATIVE(cond, replacement)		"!0:"	\
	".section .altinstructions, \"aw\"	!"	\
	".word (0b-4-.)				!"	\
	".hword 1, " __stringify(cond) "	!"	\
	".word " __stringify(replacement) "	!"	\
	".previous"

#else

/* to replace one single instructions by a new instruction */
#define ALTERNATIVE(from, to, cond, replacement)\
	.section .altinstructions, "aw"	!	\
	.word (from - .)		!	\
	.hword (to - from)/4, cond	!	\
	.word replacement		!	\
	.previous

/* to replace multiple instructions by new code */
#define ALTERNATIVE_CODE(from, num_instructions, cond, new_instr_ptr)\
	.section .altinstructions, "aw"	!	\
	.word (from - .)		!	\
	.hword -num_instructions, cond	!	\
	.word (new_instr_ptr - .)	!	\
	.previous

#endif  /*  __ASSEMBLY__  */

#endif /* __ASM_PARISC_ALTERNATIVE_H */
