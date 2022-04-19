/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_NOSPEC_ASM_H
#define _ASM_S390_NOSPEC_ASM_H

#include <asm/alternative-asm.h>
#include <asm/asm-offsets.h>
#include <asm/dwarf.h>

#ifdef __ASSEMBLY__

#ifdef CC_USING_EXPOLINE

/*
 * The expoline macros are used to create thunks in the same format
 * as gcc generates them. The 'comdat' section flag makes sure that
 * the various thunks are merged into a single copy.
 */
	.macro __THUNK_PROLOG_NAME name
#ifdef CONFIG_EXPOLINE_EXTERN
	.pushsection .text,"ax",@progbits
	.align 16,0x07
#else
	.pushsection .text.\name,"axG",@progbits,\name,comdat
#endif
	.globl \name
	.hidden \name
	.type \name,@function
\name:
	CFI_STARTPROC
	.endm

	.macro __THUNK_EPILOG_NAME name
	CFI_ENDPROC
#ifdef CONFIG_EXPOLINE_EXTERN
	.size \name, .-\name
#endif
	.popsection
	.endm

	.macro __THUNK_PROLOG_BR r1
	__THUNK_PROLOG_NAME __s390_indirect_jump_r\r1
	.endm

	.macro __THUNK_EPILOG_BR r1
	__THUNK_EPILOG_NAME __s390_indirect_jump_r\r1
	.endm

	.macro __THUNK_BR r1
	jg	__s390_indirect_jump_r\r1
	.endm

	.macro __THUNK_BRASL r1,r2
	brasl	\r1,__s390_indirect_jump_r\r2
	.endm

	.macro	__DECODE_R expand,reg
	.set __decode_fail,1
	.irp r1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.ifc \reg,%r\r1
	\expand \r1
	.set __decode_fail,0
	.endif
	.endr
	.if __decode_fail == 1
	.error "__DECODE_R failed"
	.endif
	.endm

	.macro	__DECODE_RR expand,rsave,rtarget
	.set __decode_fail,1
	.irp r1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.ifc \rsave,%r\r1
	.irp r2,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.ifc \rtarget,%r\r2
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

	.macro __THUNK_EX_BR reg
	exrl	0,555f
	j	.
555:	br	\reg
	.endm

#ifdef CONFIG_EXPOLINE_EXTERN
	.macro GEN_BR_THUNK reg
	.endm
	.macro GEN_BR_THUNK_EXTERN reg
#else
	.macro GEN_BR_THUNK reg
#endif
	__DECODE_R __THUNK_PROLOG_BR,\reg
	__THUNK_EX_BR \reg
	__DECODE_R __THUNK_EPILOG_BR,\reg
	.endm

	.macro BR_EX reg
557:	__DECODE_R __THUNK_BR,\reg
	.pushsection .s390_indirect_branches,"a",@progbits
	.long	557b-.
	.popsection
	.endm

	.macro BASR_EX rsave,rtarget
559:	__DECODE_RR __THUNK_BRASL,\rsave,\rtarget
	.pushsection .s390_indirect_branches,"a",@progbits
	.long	559b-.
	.popsection
	.endm

#else
	.macro GEN_BR_THUNK reg
	.endm

	 .macro BR_EX reg
	br	\reg
	.endm

	.macro BASR_EX rsave,rtarget
	basr	\rsave,\rtarget
	.endm
#endif /* CC_USING_EXPOLINE */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_NOSPEC_ASM_H */
