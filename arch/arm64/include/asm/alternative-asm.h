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

#endif  /*  __ASSEMBLY__  */

#endif /* __ASM_ALTERNATIVE_ASM_H */
