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

#define EX_DATA_REG(reg)		((reg) << EX_DATA_REG_SHIFT)
#define EX_DATA_FLAG(flag)		((flag) << EX_DATA_FLAG_SHIFT)
#define EX_DATA_IMM(imm)		((imm) << EX_DATA_IMM_SHIFT)

/* segment regs */
#define EX_REG_DS			EX_DATA_REG(8)
#define EX_REG_ES			EX_DATA_REG(9)
#define EX_REG_FS			EX_DATA_REG(10)
#define EX_REG_GS			EX_DATA_REG(11)

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
#define	EX_TYPE_BPF			 7
#define	EX_TYPE_WRMSR			 8
#define	EX_TYPE_RDMSR			 9
#define	EX_TYPE_WRMSR_SAFE		10 /* reg := -EIO */
#define	EX_TYPE_RDMSR_SAFE		11 /* reg := -EIO */
#define	EX_TYPE_WRMSR_IN_MCE		12
#define	EX_TYPE_RDMSR_IN_MCE		13
#define	EX_TYPE_DEFAULT_MCE_SAFE	14
#define	EX_TYPE_FAULT_MCE_SAFE		15

#define	EX_TYPE_POP_REG			16 /* sp += sizeof(long) */
#define EX_TYPE_POP_ZERO		(EX_TYPE_POP_REG | EX_DATA_IMM(0))

#define	EX_TYPE_IMM_REG			17 /* reg := (long)imm */
#define	EX_TYPE_EFAULT_REG		(EX_TYPE_IMM_REG | EX_DATA_IMM(-EFAULT))
#define	EX_TYPE_ZERO_REG		(EX_TYPE_IMM_REG | EX_DATA_IMM(0))
#define	EX_TYPE_ONE_REG			(EX_TYPE_IMM_REG | EX_DATA_IMM(1))

#define	EX_TYPE_FAULT_SGX		18

#define	EX_TYPE_UCOPY_LEN		19 /* cx := reg + imm*cx */
#define	EX_TYPE_UCOPY_LEN1		(EX_TYPE_UCOPY_LEN | EX_DATA_IMM(1))
#define	EX_TYPE_UCOPY_LEN4		(EX_TYPE_UCOPY_LEN | EX_DATA_IMM(4))
#define	EX_TYPE_UCOPY_LEN8		(EX_TYPE_UCOPY_LEN | EX_DATA_IMM(8))

#endif
