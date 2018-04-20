/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_NOSPEC_ASM_H
#define _ASM_S390_NOSPEC_ASM_H

#include <asm/dwarf.h>

#ifdef __ASSEMBLY__

#ifdef CONFIG_EXPOLINE

/*
 * The expoline macros are used to create thunks in the same format
 * as gcc generates them. The 'comdat' section flag makes sure that
 * the various thunks are merged into a single copy.
 */
	.macro __THUNK_PROLOG_NAME name
	.pushsection .text.\name,"axG",@progbits,\name,comdat
	.globl \name
	.hidden \name
	.type \name,@function
\name:
	CFI_STARTPROC
	.endm

	.macro __THUNK_EPILOG
	CFI_ENDPROC
	.popsection
	.endm

	.macro __THUNK_PROLOG_BR r1,r2
	__THUNK_PROLOG_NAME __s390x_indirect_jump_r\r2\()use_r\r1
	.endm

	.macro __THUNK_BR r1,r2
	jg	__s390x_indirect_jump_r\r2\()use_r\r1
	.endm

	.macro __THUNK_BRASL r1,r2,r3
	brasl	\r1,__s390x_indirect_jump_r\r3\()use_r\r2
	.endm

	.macro	__DECODE_RR expand,reg,ruse
	.set __decode_fail,1
	.irp r1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.ifc \reg,%r\r1
	.irp r2,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.ifc \ruse,%r\r2
	\expand \r1,\r2
	.set __decode_fail,0
	.endif
	.endr
	.endif
	.endr
	.if __decode_fail == 1
	.error "__DECODE_RR failed"
	.endif
	.endm

	.macro	__DECODE_RRR expand,rsave,rtarget,ruse
	.set __decode_fail,1
	.irp r1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.ifc \rsave,%r\r1
	.irp r2,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.ifc \rtarget,%r\r2
	.irp r3,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.ifc \ruse,%r\r3
	\expand \r1,\r2,\r3
	.set __decode_fail,0
	.endif
	.endr
	.endif
	.endr
	.endif
	.endr
	.if __decode_fail == 1
	.error "__DECODE_RRR failed"
	.endif
	.endm

	.macro __THUNK_EX_BR reg,ruse
#ifdef CONFIG_HAVE_MARCH_Z10_FEATURES
	exrl	0,555f
	j	.
#else
	larl	\ruse,555f
	ex	0,0(\ruse)
	j	.
#endif
555:	br	\reg
	.endm

	.macro GEN_BR_THUNK reg,ruse=%r1
	__DECODE_RR __THUNK_PROLOG_BR,\reg,\ruse
	__THUNK_EX_BR \reg,\ruse
	__THUNK_EPILOG
	.endm

	.macro BR_EX reg,ruse=%r1
557:	__DECODE_RR __THUNK_BR,\reg,\ruse
	.pushsection .s390_indirect_branches,"a",@progbits
	.long	557b-.
	.popsection
	.endm

	.macro BASR_EX rsave,rtarget,ruse=%r1
559:	__DECODE_RRR __THUNK_BRASL,\rsave,\rtarget,\ruse
	.pushsection .s390_indirect_branches,"a",@progbits
	.long	559b-.
	.popsection
	.endm

#else
	.macro GEN_BR_THUNK reg,ruse=%r1
	.endm

	 .macro BR_EX reg,ruse=%r1
	br	\reg
	.endm

	.macro BASR_EX rsave,rtarget,ruse=%r1
	basr	\rsave,\rtarget
	.endm
#endif

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_NOSPEC_ASM_H */
