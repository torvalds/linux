// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/kernel/asm-offsets.c
 *
 * Copyright (C) 1995-2003 Russell King
 *               2001-2002 Keith Owens
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/arm_sdei.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/kvm_host.h>
#include <linux/preempt.h>
#include <linux/suspend.h>
#include <asm/cpufeature.h>
#include <asm/fixmap.h>
#include <asm/thread_info.h>
#include <asm/memory.h>
#include <asm/signal32.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <linux/kbuild.h>
#include <linux/arm-smccc.h>

int main(void)
{
  DEFINE(TSK_ACTIVE_MM,		offsetof(struct task_struct, active_mm));
  DEFINE(TSK_CPU,		offsetof(struct task_struct, cpu));
  BLANK();
  DEFINE(TSK_TI_FLAGS,		offsetof(struct task_struct, thread_info.flags));
  DEFINE(TSK_TI_PREEMPT,	offsetof(struct task_struct, thread_info.preempt_count));
#ifdef CONFIG_ARM64_SW_TTBR0_PAN
  DEFINE(TSK_TI_TTBR0,		offsetof(struct task_struct, thread_info.ttbr0));
#endif
#ifdef CONFIG_SHADOW_CALL_STACK
  DEFINE(TSK_TI_SCS_BASE,	offsetof(struct task_struct, thread_info.scs_base));
  DEFINE(TSK_TI_SCS_SP,		offsetof(struct task_struct, thread_info.scs_sp));
#endif
  DEFINE(TSK_STACK,		offsetof(struct task_struct, stack));
#ifdef CONFIG_STACKPROTECTOR
  DEFINE(TSK_STACK_CANARY,	offsetof(struct task_struct, stack_canary));
#endif
  BLANK();
  DEFINE(THREAD_CPU_CONTEXT,	offsetof(struct task_struct, thread.cpu_context));
  DEFINE(THREAD_SCTLR_USER,	offsetof(struct task_struct, thread.sctlr_user));
#ifdef CONFIG_ARM64_PTR_AUTH
  DEFINE(THREAD_KEYS_USER,	offsetof(struct task_struct, thread.keys_user));
  DEFINE(THREAD_KEYS_KERNEL,	offsetof(struct task_struct, thread.keys_kernel));
#endif
#ifdef CONFIG_ARM64_MTE
  DEFINE(THREAD_GCR_EL1_USER,	offsetof(struct task_struct, thread.gcr_user_excl));
#endif
  BLANK();
  DEFINE(S_X0,			offsetof(struct pt_regs, regs[0]));
  DEFINE(S_X2,			offsetof(struct pt_regs, regs[2]));
  DEFINE(S_X4,			offsetof(struct pt_regs, regs[4]));
  DEFINE(S_X6,			offsetof(struct pt_regs, regs[6]));
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
  DEFINE(S_FP,			offsetof(struct pt_regs, regs[29]));
  DEFINE(S_LR,			offsetof(struct pt_regs, regs[30]));
  DEFINE(S_SP,			offsetof(struct pt_regs, sp));
  DEFINE(S_PSTATE,		offsetof(struct pt_regs, pstate));
  DEFINE(S_PC,			offsetof(struct pt_regs, pc));
  DEFINE(S_SYSCALLNO,		offsetof(struct pt_regs, syscallno));
  DEFINE(S_SDEI_TTBR1,		offsetof(struct pt_regs, sdei_ttbr1));
  DEFINE(S_PMR_SAVE,		offsetof(struct pt_regs, pmr_save));
  DEFINE(S_STACKFRAME,		offsetof(struct pt_regs, stackframe));
  DEFINE(PT_REGS_SIZE,		sizeof(struct pt_regs));
  BLANK();
#ifdef CONFIG_COMPAT
  DEFINE(COMPAT_SIGFRAME_REGS_OFFSET,		offsetof(struct compat_sigframe, uc.uc_mcontext.arm_r0));
  DEFINE(COMPAT_RT_SIGFRAME_REGS_OFFSET,	offsetof(struct compat_rt_sigframe, sig.uc.uc_mcontext.arm_r0));
  BLANK();
#endif
  DEFINE(MM_CONTEXT_ID,		offsetof(struct mm_struct, context.id.counter));
  BLANK();
  DEFINE(VMA_VM_MM,		offsetof(struct vm_area_struct, vm_mm));
  DEFINE(VMA_VM_FLAGS,		offsetof(struct vm_area_struct, vm_flags));
  BLANK();
  DEFINE(VM_EXEC,	       	VM_EXEC);
  BLANK();
  DEFINE(PAGE_SZ,	       	PAGE_SIZE);
  BLANK();
  DEFINE(DMA_TO_DEVICE,		DMA_TO_DEVICE);
  DEFINE(DMA_FROM_DEVICE,	DMA_FROM_DEVICE);
  BLANK();
  DEFINE(PREEMPT_DISABLE_OFFSET, PREEMPT_DISABLE_OFFSET);
  DEFINE(SOFTIRQ_SHIFT, SOFTIRQ_SHIFT);
  DEFINE(IRQ_CPUSTAT_SOFTIRQ_PENDING, offsetof(irq_cpustat_t, __softirq_pending));
  BLANK();
  DEFINE(CPU_BOOT_TASK,		offsetof(struct secondary_data, task));
  BLANK();
  DEFINE(FTR_OVR_VAL_OFFSET,	offsetof(struct arm64_ftr_override, val));
  DEFINE(FTR_OVR_MASK_OFFSET,	offsetof(struct arm64_ftr_override, mask));
  BLANK();
#ifdef CONFIG_KVM
  DEFINE(VCPU_CONTEXT,		offsetof(struct kvm_vcpu, arch.ctxt));
  DEFINE(VCPU_FAULT_DISR,	offsetof(struct kvm_vcpu, arch.fault.disr_el1));
  DEFINE(VCPU_WORKAROUND_FLAGS,	offsetof(struct kvm_vcpu, arch.workaround_flags));
  DEFINE(VCPU_HCR_EL2,		offsetof(struct kvm_vcpu, arch.hcr_el2));
  DEFINE(CPU_USER_PT_REGS,	offsetof(struct kvm_cpu_context, regs));
  DEFINE(CPU_APIAKEYLO_EL1,	offsetof(struct kvm_cpu_context, sys_regs[APIAKEYLO_EL1]));
  DEFINE(CPU_APIBKEYLO_EL1,	offsetof(struct kvm_cpu_context, sys_regs[APIBKEYLO_EL1]));
  DEFINE(CPU_APDAKEYLO_EL1,	offsetof(struct kvm_cpu_context, sys_regs[APDAKEYLO_EL1]));
  DEFINE(CPU_APDBKEYLO_EL1,	offsetof(struct kvm_cpu_context, sys_regs[APDBKEYLO_EL1]));
  DEFINE(CPU_APGAKEYLO_EL1,	offsetof(struct kvm_cpu_context, sys_regs[APGAKEYLO_EL1]));
  DEFINE(HOST_CONTEXT_VCPU,	offsetof(struct kvm_cpu_context, __hyp_running_vcpu));
  DEFINE(HOST_DATA_CONTEXT,	offsetof(struct kvm_host_data, host_ctxt));
  DEFINE(NVHE_INIT_MAIR_EL2,	offsetof(struct kvm_nvhe_init_params, mair_el2));
  DEFINE(NVHE_INIT_TCR_EL2,	offsetof(struct kvm_nvhe_init_params, tcr_el2));
  DEFINE(NVHE_INIT_TPIDR_EL2,	offsetof(struct kvm_nvhe_init_params, tpidr_el2));
  DEFINE(NVHE_INIT_STACK_HYP_VA,	offsetof(struct kvm_nvhe_init_params, stack_hyp_va));
  DEFINE(NVHE_INIT_PGD_PA,	offsetof(struct kvm_nvhe_init_params, pgd_pa));
  DEFINE(NVHE_INIT_HCR_EL2,	offsetof(struct kvm_nvhe_init_params, hcr_el2));
  DEFINE(NVHE_INIT_VTTBR,	offsetof(struct kvm_nvhe_init_params, vttbr));
  DEFINE(NVHE_INIT_VTCR,	offsetof(struct kvm_nvhe_init_params, vtcr));
#endif
#ifdef CONFIG_CPU_PM
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
#ifdef CONFIG_ARM64_PTR_AUTH
  DEFINE(PTRAUTH_USER_KEY_APIA,		offsetof(struct ptrauth_keys_user, apia));
  DEFINE(PTRAUTH_KERNEL_KEY_APIA,	offsetof(struct ptrauth_keys_kernel, apia));
  BLANK();
#endif
  return 0;
}
