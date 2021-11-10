/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_EXTABLE_FIXUP_TYPES_H
#define _ASM_X86_EXTABLE_FIXUP_TYPES_H

/*
 * Our IMM is signed, as such it must live at the top end of the word. Also,
 * since C99 hex constants are of ambigious type, force cast the mask to 'int'
 * so that FIELD_GET() will DTRT and sign extend the value when it extracts it.
 */
#define EX_DATA_TYPE_MASK		((int)0x000000FF)
#define EX_DATA_REG_MASK		((int)0x00000F00)
#define EX_DATA_FLAG_MASK		((int)0x0000F000)
#define EX_DATA_IMM_MASK		((int)0xFFFF0000)

#define EX_DATA_REG_SHIFT		8
#define EX_DATA_FLAG_SHIFT		12
#define EX_DATA_IMM_SHIFT		16

#define EX_DATA_FLAG(flag)		((flag) << EX_DATA_FLAG_SHIFT)
#define EX_DATA_IMM(imm)		((imm) << EX_DATA_IMM_SHIFT)

/* flags */
#define EX_FLAG_CLEAR_AX		EX_DATA_FLAG(1)
#define EX_FLAG_CLEAR_DX		EX_DATA_FLAG(2)
#define EX_FLAG_CLEAR_AX_DX		EX_DATA_FLAG(3)

/* types */
#define	EX_TYPE_NONE			 0
#define	EX_TYPE_DEFAULT			 1
#define	EX_TYPE_FAULT			 2
#define	EX_TYPE_UACCESS			 3
#define	EX_TYPE_COPY			 4
#define	EX_TYPE_CLEAR_FS		 5
#define	EX_TYPE_FPU_RESTORE		 6
#define	EX_TYPE_WRMSR			 7
#define	EX_TYPE_RDMSR			 8
#define	EX_TYPE_BPF			 9

#define	EX_TYPE_WRMSR_IN_MCE		10
#define	EX_TYPE_RDMSR_IN_MCE		11

#define	EX_TYPE_DEFAULT_MCE_SAFE	12
#define	EX_TYPE_FAULT_MCE_SAFE		13

#define	EX_TYPE_POP_ZERO		14
#define	EX_TYPE_IMM_REG			15 /* reg := (long)imm */

#endif
