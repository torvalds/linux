// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/unicore32/kernel/asm-offsets.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/kbuild.h>
#include <linux/suspend.h>
#include <linux/thread_info.h>
#include <asm/memory.h>
#include <asm/suspend.h>

/*
 * GCC 3.0, 3.1: general bad code generation.
 * GCC 3.2.0: incorrect function argument offset calculation.
 * GCC 3.2.x: miscompiles NEW_AUX_ENT in fs/binfmt_elf.c
 *	(http://gcc.gnu.org/PR8896) and incorrect structure
 *		initialisation in fs/jffs2/erase.c
 */
#if (__GNUC__ < 4)
#error Your compiler should upgrade to uc4
#error	Known good compilers: 4.2.2
#endif

int main(void)
{
	DEFINE(TSK_ACTIVE_MM,	offsetof(struct task_struct, active_mm));
	BLANK();
	DEFINE(TI_FLAGS,	offsetof(struct thread_info, flags));
	DEFINE(TI_PREEMPT,	offsetof(struct thread_info, preempt_count));
	DEFINE(TI_ADDR_LIMIT,	offsetof(struct thread_info, addr_limit));
	DEFINE(TI_TASK,		offsetof(struct thread_info, task));
	DEFINE(TI_CPU,		offsetof(struct thread_info, cpu));
	DEFINE(TI_CPU_SAVE,	offsetof(struct thread_info, cpu_context));
	DEFINE(TI_USED_CP,	offsetof(struct thread_info, used_cp));
#ifdef CONFIG_UNICORE_FPU_F64
	DEFINE(TI_FPSTATE,	offsetof(struct thread_info, fpstate));
#endif
	BLANK();
	DEFINE(S_R0,		offsetof(struct pt_regs, UCreg_00));
	DEFINE(S_R1,		offsetof(struct pt_regs, UCreg_01));
	DEFINE(S_R2,		offsetof(struct pt_regs, UCreg_02));
	DEFINE(S_R3,		offsetof(struct pt_regs, UCreg_03));
	DEFINE(S_R4,		offsetof(struct pt_regs, UCreg_04));
	DEFINE(S_R5,		offsetof(struct pt_regs, UCreg_05));
	DEFINE(S_R6,		offsetof(struct pt_regs, UCreg_06));
	DEFINE(S_R7,		offsetof(struct pt_regs, UCreg_07));
	DEFINE(S_R8,		offsetof(struct pt_regs, UCreg_08));
	DEFINE(S_R9,		offsetof(struct pt_regs, UCreg_09));
	DEFINE(S_R10,		offsetof(struct pt_regs, UCreg_10));
	DEFINE(S_R11,		offsetof(struct pt_regs, UCreg_11));
	DEFINE(S_R12,		offsetof(struct pt_regs, UCreg_12));
	DEFINE(S_R13,		offsetof(struct pt_regs, UCreg_13));
	DEFINE(S_R14,		offsetof(struct pt_regs, UCreg_14));
	DEFINE(S_R15,		offsetof(struct pt_regs, UCreg_15));
	DEFINE(S_R16,		offsetof(struct pt_regs, UCreg_16));
	DEFINE(S_R17,		offsetof(struct pt_regs, UCreg_17));
	DEFINE(S_R18,		offsetof(struct pt_regs, UCreg_18));
	DEFINE(S_R19,		offsetof(struct pt_regs, UCreg_19));
	DEFINE(S_R20,		offsetof(struct pt_regs, UCreg_20));
	DEFINE(S_R21,		offsetof(struct pt_regs, UCreg_21));
	DEFINE(S_R22,		offsetof(struct pt_regs, UCreg_22));
	DEFINE(S_R23,		offsetof(struct pt_regs, UCreg_23));
	DEFINE(S_R24,		offsetof(struct pt_regs, UCreg_24));
	DEFINE(S_R25,		offsetof(struct pt_regs, UCreg_25));
	DEFINE(S_R26,		offsetof(struct pt_regs, UCreg_26));
	DEFINE(S_FP,		offsetof(struct pt_regs, UCreg_fp));
	DEFINE(S_IP,		offsetof(struct pt_regs, UCreg_ip));
	DEFINE(S_SP,		offsetof(struct pt_regs, UCreg_sp));
	DEFINE(S_LR,		offsetof(struct pt_regs, UCreg_lr));
	DEFINE(S_PC,		offsetof(struct pt_regs, UCreg_pc));
	DEFINE(S_PSR,		offsetof(struct pt_regs, UCreg_asr));
	DEFINE(S_OLD_R0,	offsetof(struct pt_regs, UCreg_ORIG_00));
	DEFINE(S_FRAME_SIZE,	sizeof(struct pt_regs));
	BLANK();
	DEFINE(VMA_VM_MM,	offsetof(struct vm_area_struct, vm_mm));
	DEFINE(VMA_VM_FLAGS,	offsetof(struct vm_area_struct, vm_flags));
	BLANK();
	DEFINE(VM_EXEC,		VM_EXEC);
	BLANK();
	DEFINE(PAGE_SZ,		PAGE_SIZE);
	BLANK();
	DEFINE(SYS_ERROR0,	0x9f0000);
	BLANK();
	DEFINE(PBE_ADDRESS,		offsetof(struct pbe, address));
	DEFINE(PBE_ORIN_ADDRESS,	offsetof(struct pbe, orig_address));
	DEFINE(PBE_NEXT,		offsetof(struct pbe, next));
	DEFINE(SWSUSP_CPU,		offsetof(struct swsusp_arch_regs, \
							cpu_context));
#ifdef	CONFIG_UNICORE_FPU_F64
	DEFINE(SWSUSP_FPSTATE,		offsetof(struct swsusp_arch_regs, \
							fpstate));
#endif
	BLANK();
	DEFINE(DMA_BIDIRECTIONAL,	DMA_BIDIRECTIONAL);
	DEFINE(DMA_TO_DEVICE,		DMA_TO_DEVICE);
	DEFINE(DMA_FROM_DEVICE,		DMA_FROM_DEVICE);
	return 0;
}
