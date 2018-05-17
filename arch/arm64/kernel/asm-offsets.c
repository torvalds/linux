/*
 * Based on arch/arm/kernel/asm-offsets.c
 *
 * Copyright (C) 1995-2003 Russell King
 *               2001-2002 Keith Owens
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/arm_sdei.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/kvm_host.h>
#include <linux/suspend.h>
#include <asm/cpufeature.h>
#include <asm/fixmap.h>
#include <asm/thread_info.h>
#include <asm/memory.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <asm/vdso_datapage.h>
#include <linux/kbuild.h>
#include <linux/arm-smccc.h>

int main(void)
{
  DEFINE(TSK_ACTIVE_MM,		offsetof(struct task_struct, active_mm));
  BLANK();
  DEFINE(TSK_TI_FLAGS,		offsetof(struct task_struct, thread_info.flags));
  DEFINE(TSK_TI_PREEMPT,	offsetof(struct task_struct, thread_info.preempt_count));
  DEFINE(TSK_TI_ADDR_LIMIT,	offsetof(struct task_struct, thread_info.addr_limit));
#ifdef CONFIG_ARM64_SW_TTBR0_PAN
  DEFINE(TSK_TI_TTBR0,		offsetof(struct task_struct, thread_info.ttbr0));
#endif
  DEFINE(TSK_STACK,		offsetof(struct task_struct, stack));
  BLANK();
  DEFINE(THREAD_CPU_CONTEXT,	offsetof(struct task_struct, thread.cpu_context));
  BLANK();
  DEFINE(S_X0,			offsetof(struct pt_regs, regs[0]));
  DEFINE(S_X1,			offsetof(struct pt_regs, regs[1]));
  DEFINE(S_X2,			offsetof(struct pt_regs, regs[2]));
  DEFINE(S_X3,			offsetof(struct pt_regs, regs[3]));
  DEFINE(S_X4,			offsetof(struct pt_regs, regs[4]));
  DEFINE(S_X5,			offsetof(struct pt_regs, regs[5]));
  DEFINE(S_X6,			offsetof(struct pt_regs, regs[6]));
  DEFINE(S_X7,			offsetof(struct pt_regs, regs[7]));
  DEFINE(S_X8,			offsetof(struct pt_regs, regs[8]));
  DEFINE(S_X10,			offsetof(struct pt_regs, regs[10]));
  DEFINE(S_X12,			offsetof(struct pt_regs, regs[12]));
  DEFINE(S_X14,			offsetof(struct pt_regs, regs[14]));
  DEFINE(S_X16,			offsetof(struct pt_regs, regs[16]));
  DEFINE(S_X18,			offsetof(struct pt_regs, regs[18]));
  DEFINE(S_X20,			offsetof(struct pt_regs, regs[20]));
  DEFINE(S_X22,			offsetof(struct pt_regs, regs[22]));
  DEFINE(S_X24,			offsetof(struct pt_regs, regs[24]));
  DEFINE(S_X26,			offsetof(struct pt_regs, regs[26]));
  DEFINE(S_X28,			offsetof(struct pt_regs, regs[28]));
  DEFINE(S_LR,			offsetof(struct pt_regs, regs[30]));
  DEFINE(S_SP,			offsetof(struct pt_regs, sp));
#ifdef CONFIG_COMPAT
  DEFINE(S_COMPAT_SP,		offsetof(struct pt_regs, compat_sp));
#endif
  DEFINE(S_PSTATE,		offsetof(struct pt_regs, pstate));
  DEFINE(S_PC,			offsetof(struct pt_regs, pc));
  DEFINE(S_ORIG_X0,		offsetof(struct pt_regs, orig_x0));
  DEFINE(S_SYSCALLNO,		offsetof(struct pt_regs, syscallno));
  DEFINE(S_ORIG_ADDR_LIMIT,	offsetof(struct pt_regs, orig_addr_limit));
  DEFINE(S_STACKFRAME,		offsetof(struct pt_regs, stackframe));
  DEFINE(S_FRAME_SIZE,		sizeof(struct pt_regs));
  BLANK();
  DEFINE(MM_CONTEXT_ID,		offsetof(struct mm_struct, context.id.counter));
  BLANK();
  DEFINE(VMA_VM_MM,		offsetof(struct vm_area_struct, vm_mm));
  DEFINE(VMA_VM_FLAGS,		offsetof(struct vm_area_struct, vm_flags));
  BLANK();
  DEFINE(VM_EXEC,	       	VM_EXEC);
  BLANK();
  DEFINE(PAGE_SZ,	       	PAGE_SIZE);
  BLANK();
  DEFINE(DMA_BIDIRECTIONAL,	DMA_BIDIRECTIONAL);
  DEFINE(DMA_TO_DEVICE,		DMA_TO_DEVICE);
  DEFINE(DMA_FROM_DEVICE,	DMA_FROM_DEVICE);
  BLANK();
  DEFINE(CLOCK_REALTIME,	CLOCK_REALTIME);
  DEFINE(CLOCK_MONOTONIC,	CLOCK_MONOTONIC);
  DEFINE(CLOCK_MONOTONIC_RAW,	CLOCK_MONOTONIC_RAW);
  DEFINE(CLOCK_REALTIME_RES,	MONOTONIC_RES_NSEC);
  DEFINE(CLOCK_REALTIME_COARSE,	CLOCK_REALTIME_COARSE);
  DEFINE(CLOCK_MONOTONIC_COARSE,CLOCK_MONOTONIC_COARSE);
  DEFINE(CLOCK_COARSE_RES,	LOW_RES_NSEC);
  DEFINE(NSEC_PER_SEC,		NSEC_PER_SEC);
  BLANK();
  DEFINE(VDSO_CS_CYCLE_LAST,	offsetof(struct vdso_data, cs_cycle_last));
  DEFINE(VDSO_RAW_TIME_SEC,	offsetof(struct vdso_data, raw_time_sec));
  DEFINE(VDSO_RAW_TIME_NSEC,	offsetof(struct vdso_data, raw_time_nsec));
  DEFINE(VDSO_XTIME_CLK_SEC,	offsetof(struct vdso_data, xtime_clock_sec));
  DEFINE(VDSO_XTIME_CLK_NSEC,	offsetof(struct vdso_data, xtime_clock_nsec));
  DEFINE(VDSO_XTIME_CRS_SEC,	offsetof(struct vdso_data, xtime_coarse_sec));
  DEFINE(VDSO_XTIME_CRS_NSEC,	offsetof(struct vdso_data, xtime_coarse_nsec));
  DEFINE(VDSO_WTM_CLK_SEC,	offsetof(struct vdso_data, wtm_clock_sec));
  DEFINE(VDSO_WTM_CLK_NSEC,	offsetof(struct vdso_data, wtm_clock_nsec));
  DEFINE(VDSO_TB_SEQ_COUNT,	offsetof(struct vdso_data, tb_seq_count));
  DEFINE(VDSO_CS_MONO_MULT,	offsetof(struct vdso_data, cs_mono_mult));
  DEFINE(VDSO_CS_RAW_MULT,	offsetof(struct vdso_data, cs_raw_mult));
  DEFINE(VDSO_CS_SHIFT,		offsetof(struct vdso_data, cs_shift));
  DEFINE(VDSO_TZ_MINWEST,	offsetof(struct vdso_data, tz_minuteswest));
  DEFINE(VDSO_TZ_DSTTIME,	offsetof(struct vdso_data, tz_dsttime));
  DEFINE(VDSO_USE_SYSCALL,	offsetof(struct vdso_data, use_syscall));
  BLANK();
  DEFINE(TVAL_TV_SEC,		offsetof(struct timeval, tv_sec));
  DEFINE(TVAL_TV_USEC,		offsetof(struct timeval, tv_usec));
  DEFINE(TSPEC_TV_SEC,		offsetof(struct timespec, tv_sec));
  DEFINE(TSPEC_TV_NSEC,		offsetof(struct timespec, tv_nsec));
  BLANK();
  DEFINE(TZ_MINWEST,		offsetof(struct timezone, tz_minuteswest));
  DEFINE(TZ_DSTTIME,		offsetof(struct timezone, tz_dsttime));
  BLANK();
  DEFINE(CPU_BOOT_STACK,	offsetof(struct secondary_data, stack));
  DEFINE(CPU_BOOT_TASK,		offsetof(struct secondary_data, task));
  BLANK();
#ifdef CONFIG_KVM_ARM_HOST
  DEFINE(VCPU_CONTEXT,		offsetof(struct kvm_vcpu, arch.ctxt));
  DEFINE(VCPU_FAULT_DISR,	offsetof(struct kvm_vcpu, arch.fault.disr_el1));
  DEFINE(CPU_GP_REGS,		offsetof(struct kvm_cpu_context, gp_regs));
  DEFINE(CPU_USER_PT_REGS,	offsetof(struct kvm_regs, regs));
  DEFINE(CPU_FP_REGS,		offsetof(struct kvm_regs, fp_regs));
  DEFINE(VCPU_FPEXC32_EL2,	offsetof(struct kvm_vcpu, arch.ctxt.sys_regs[FPEXC32_EL2]));
  DEFINE(VCPU_HOST_CONTEXT,	offsetof(struct kvm_vcpu, arch.host_cpu_context));
#endif
#ifdef CONFIG_CPU_PM
  DEFINE(CPU_SUSPEND_SZ,	sizeof(struct cpu_suspend_ctx));
  DEFINE(CPU_CTX_SP,		offsetof(struct cpu_suspend_ctx, sp));
  DEFINE(MPIDR_HASH_MASK,	offsetof(struct mpidr_hash, mask));
  DEFINE(MPIDR_HASH_SHIFTS,	offsetof(struct mpidr_hash, shift_aff));
  DEFINE(SLEEP_STACK_DATA_SYSTEM_REGS,	offsetof(struct sleep_stack_data, system_regs));
  DEFINE(SLEEP_STACK_DATA_CALLEE_REGS,	offsetof(struct sleep_stack_data, callee_saved_regs));
#endif
  DEFINE(ARM_SMCCC_RES_X0_OFFS,		offsetof(struct arm_smccc_res, a0));
  DEFINE(ARM_SMCCC_RES_X2_OFFS,		offsetof(struct arm_smccc_res, a2));
  DEFINE(ARM_SMCCC_QUIRK_ID_OFFS,	offsetof(struct arm_smccc_quirk, id));
  DEFINE(ARM_SMCCC_QUIRK_STATE_OFFS,	offsetof(struct arm_smccc_quirk, state));
  BLANK();
  DEFINE(HIBERN_PBE_ORIG,	offsetof(struct pbe, orig_address));
  DEFINE(HIBERN_PBE_ADDR,	offsetof(struct pbe, address));
  DEFINE(HIBERN_PBE_NEXT,	offsetof(struct pbe, next));
  DEFINE(ARM64_FTR_SYSVAL,	offsetof(struct arm64_ftr_reg, sys_val));
  BLANK();
#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
  DEFINE(TRAMP_VALIAS,		TRAMP_VALIAS);
#endif
#ifdef CONFIG_ARM_SDE_INTERFACE
  DEFINE(SDEI_EVENT_INTREGS,	offsetof(struct sdei_registered_event, interrupted_regs));
  DEFINE(SDEI_EVENT_PRIORITY,	offsetof(struct sdei_registered_event, priority));
#endif
  return 0;
}
