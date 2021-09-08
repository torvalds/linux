/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_EXTABLE_FIXUP_TYPES_H
#define _ASM_X86_EXTABLE_FIXUP_TYPES_H

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

#endif
