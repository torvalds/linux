#ifndef __ASM_ALTERNATIVE_ASM_H
#define __ASM_ALTERNATIVE_ASM_H

#ifdef __ASSEMBLY__

.macro altinstruction_entry orig_offset alt_offset feature orig_len alt_len
	.word \orig_offset - .
	.word \alt_offset - .
	.hword \feature
	.byte \orig_len
	.byte \alt_len
.endm

.macro alternative_insn insn1 insn2 cap
661:	\insn1
662:	.pushsection .altinstructions, "a"
	altinstruction_entry 661b, 663f, \cap, 662b-661b, 664f-663f
	.popsection
	.pushsection .altinstr_replacement, "ax"
663:	\insn2
664:	.popsection
	.if ((664b-663b) != (662b-661b))
		.error "Alternatives instruction length mismatch"
	.endif
.endm

#endif  /*  __ASSEMBLY__  */

#endif /* __ASM_ALTERNATIVE_ASM_H */
