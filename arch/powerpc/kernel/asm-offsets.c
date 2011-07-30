/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/hrtimer.h>
#ifdef CONFIG_PPC64
#include <linux/time.h>
#include <linux/hardirq.h>
#endif
#include <linux/kbuild.h>

#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/rtas.h>
#include <asm/vdso_datapage.h>
#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/lppaca.h>
#include <asm/cache.h>
#include <asm/compat.h>
#include <asm/mmu.h>
#include <asm/hvcall.h>
#endif
#ifdef CONFIG_PPC_ISERIES
#include <asm/iseries/alpaca.h>
#endif
#ifdef CONFIG_KVM
#include <linux/kvm_host.h>
#endif

#ifdef CONFIG_PPC32
#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
#include "head_booke.h"
#endif
#endif

#if defined(CONFIG_FSL_BOOKE)
#include "../mm/mmu_decl.h"
#endif

int main(void)
{
	DEFINE(THREAD, offsetof(struct task_struct, thread));
	DEFINE(MM, offsetof(struct task_struct, mm));
	DEFINE(MMCONTEXTID, offsetof(struct mm_struct, context.id));
#ifdef CONFIG_PPC64
	DEFINE(AUDITCONTEXT, offsetof(struct task_struct, audit_context));
	DEFINE(SIGSEGV, SIGSEGV);
	DEFINE(NMI_MASK, NMI_MASK);
#else
	DEFINE(THREAD_INFO, offsetof(struct task_struct, stack));
#endif /* CONFIG_PPC64 */

	DEFINE(KSP, offsetof(struct thread_struct, ksp));
	DEFINE(KSP_LIMIT, offsetof(struct thread_struct, ksp_limit));
	DEFINE(PT_REGS, offsetof(struct thread_struct, regs));
	DEFINE(THREAD_FPEXC_MODE, offsetof(struct thread_struct, fpexc_mode));
	DEFINE(THREAD_FPR0, offsetof(struct thread_struct, fpr[0]));
	DEFINE(THREAD_FPSCR, offsetof(struct thread_struct, fpscr));
#ifdef CONFIG_ALTIVEC
	DEFINE(THREAD_VR0, offsetof(struct thread_struct, vr[0]));
	DEFINE(THREAD_VRSAVE, offsetof(struct thread_struct, vrsave));
	DEFINE(THREAD_VSCR, offsetof(struct thread_struct, vscr));
	DEFINE(THREAD_USED_VR, offsetof(struct thread_struct, used_vr));
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_VSX
	DEFINE(THREAD_VSR0, offsetof(struct thread_struct, fpr));
	DEFINE(THREAD_USED_VSR, offsetof(struct thread_struct, used_vsr));
#endif /* CONFIG_VSX */
#ifdef CONFIG_PPC64
	DEFINE(KSP_VSID, offsetof(struct thread_struct, ksp_vsid));
#else /* CONFIG_PPC64 */
	DEFINE(PGDIR, offsetof(struct thread_struct, pgdir));
#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
	DEFINE(THREAD_DBCR0, offsetof(struct thread_struct, dbcr0));
#endif
#ifdef CONFIG_SPE
	DEFINE(THREAD_EVR0, offsetof(struct thread_struct, evr[0]));
	DEFINE(THREAD_ACC, offsetof(struct thread_struct, acc));
	DEFINE(THREAD_SPEFSCR, offsetof(struct thread_struct, spefscr));
	DEFINE(THREAD_USED_SPE, offsetof(struct thread_struct, used_spe));
#endif /* CONFIG_SPE */
#endif /* CONFIG_PPC64 */

	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_LOCAL_FLAGS, offsetof(struct thread_info, local_flags));
	DEFINE(TI_PREEMPT, offsetof(struct thread_info, preempt_count));
	DEFINE(TI_TASK, offsetof(struct thread_info, task));
	DEFINE(TI_CPU, offsetof(struct thread_info, cpu));

#ifdef CONFIG_PPC64
	DEFINE(DCACHEL1LINESIZE, offsetof(struct ppc64_caches, dline_size));
	DEFINE(DCACHEL1LOGLINESIZE, offsetof(struct ppc64_caches, log_dline_size));
	DEFINE(DCACHEL1LINESPERPAGE, offsetof(struct ppc64_caches, dlines_per_page));
	DEFINE(ICACHEL1LINESIZE, offsetof(struct ppc64_caches, iline_size));
	DEFINE(ICACHEL1LOGLINESIZE, offsetof(struct ppc64_caches, log_iline_size));
	DEFINE(ICACHEL1LINESPERPAGE, offsetof(struct ppc64_caches, ilines_per_page));
	/* paca */
	DEFINE(PACA_SIZE, sizeof(struct paca_struct));
	DEFINE(PACAPACAINDEX, offsetof(struct paca_struct, paca_index));
	DEFINE(PACAPROCSTART, offsetof(struct paca_struct, cpu_start));
	DEFINE(PACAKSAVE, offsetof(struct paca_struct, kstack));
	DEFINE(PACACURRENT, offsetof(struct paca_struct, __current));
	DEFINE(PACASAVEDMSR, offsetof(struct paca_struct, saved_msr));
	DEFINE(PACASTABRR, offsetof(struct paca_struct, stab_rr));
	DEFINE(PACAR1, offsetof(struct paca_struct, saved_r1));
	DEFINE(PACATOC, offsetof(struct paca_struct, kernel_toc));
	DEFINE(PACAKBASE, offsetof(struct paca_struct, kernelbase));
	DEFINE(PACAKMSR, offsetof(struct paca_struct, kernel_msr));
	DEFINE(PACASOFTIRQEN, offsetof(struct paca_struct, soft_enabled));
	DEFINE(PACAHARDIRQEN, offsetof(struct paca_struct, hard_enabled));
	DEFINE(PACAPERFPEND, offsetof(struct paca_struct, perf_event_pending));
	DEFINE(PACACONTEXTID, offsetof(struct paca_struct, context.id));
#ifdef CONFIG_PPC_MM_SLICES
	DEFINE(PACALOWSLICESPSIZE, offsetof(struct paca_struct,
					    context.low_slices_psize));
	DEFINE(PACAHIGHSLICEPSIZE, offsetof(struct paca_struct,
					    context.high_slices_psize));
	DEFINE(MMUPSIZEDEFSIZE, sizeof(struct mmu_psize_def));
#endif /* CONFIG_PPC_MM_SLICES */

#ifdef CONFIG_PPC_BOOK3E
	DEFINE(PACAPGD, offsetof(struct paca_struct, pgd));
	DEFINE(PACA_KERNELPGD, offsetof(struct paca_struct, kernel_pgd));
	DEFINE(PACA_EXGEN, offsetof(struct paca_struct, exgen));
	DEFINE(PACA_EXTLB, offsetof(struct paca_struct, extlb));
	DEFINE(PACA_EXMC, offsetof(struct paca_struct, exmc));
	DEFINE(PACA_EXCRIT, offsetof(struct paca_struct, excrit));
	DEFINE(PACA_EXDBG, offsetof(struct paca_struct, exdbg));
	DEFINE(PACA_MC_STACK, offsetof(struct paca_struct, mc_kstack));
	DEFINE(PACA_CRIT_STACK, offsetof(struct paca_struct, crit_kstack));
	DEFINE(PACA_DBG_STACK, offsetof(struct paca_struct, dbg_kstack));
#endif /* CONFIG_PPC_BOOK3E */

#ifdef CONFIG_PPC_STD_MMU_64
	DEFINE(PACASTABREAL, offsetof(struct paca_struct, stab_real));
	DEFINE(PACASTABVIRT, offsetof(struct paca_struct, stab_addr));
	DEFINE(PACASLBCACHE, offsetof(struct paca_struct, slb_cache));
	DEFINE(PACASLBCACHEPTR, offsetof(struct paca_struct, slb_cache_ptr));
	DEFINE(PACAVMALLOCSLLP, offsetof(struct paca_struct, vmalloc_sllp));
#ifdef CONFIG_PPC_MM_SLICES
	DEFINE(MMUPSIZESLLP, offsetof(struct mmu_psize_def, sllp));
#else
	DEFINE(PACACONTEXTSLLP, offsetof(struct paca_struct, context.sllp));
#endif /* CONFIG_PPC_MM_SLICES */
	DEFINE(PACA_EXGEN, offsetof(struct paca_struct, exgen));
	DEFINE(PACA_EXMC, offsetof(struct paca_struct, exmc));
	DEFINE(PACA_EXSLB, offsetof(struct paca_struct, exslb));
	DEFINE(PACALPPACAPTR, offsetof(struct paca_struct, lppaca_ptr));
	DEFINE(PACA_SLBSHADOWPTR, offsetof(struct paca_struct, slb_shadow_ptr));
	DEFINE(SLBSHADOW_STACKVSID,
	       offsetof(struct slb_shadow, save_area[SLB_NUM_BOLTED - 1].vsid));
	DEFINE(SLBSHADOW_STACKESID,
	       offsetof(struct slb_shadow, save_area[SLB_NUM_BOLTED - 1].esid));
	DEFINE(LPPACASRR0, offsetof(struct lppaca, saved_srr0));
	DEFINE(LPPACASRR1, offsetof(struct lppaca, saved_srr1));
	DEFINE(LPPACAANYINT, offsetof(struct lppaca, int_dword.any_int));
	DEFINE(LPPACADECRINT, offsetof(struct lppaca, int_dword.fields.decr_int));
	DEFINE(SLBSHADOW_SAVEAREA, offsetof(struct slb_shadow, save_area));
#endif /* CONFIG_PPC_STD_MMU_64 */
	DEFINE(PACAEMERGSP, offsetof(struct paca_struct, emergency_sp));
	DEFINE(PACAHWCPUID, offsetof(struct paca_struct, hw_cpu_id));
	DEFINE(PACA_STARTPURR, offsetof(struct paca_struct, startpurr));
	DEFINE(PACA_STARTSPURR, offsetof(struct paca_struct, startspurr));
	DEFINE(PACA_USER_TIME, offsetof(struct paca_struct, user_time));
	DEFINE(PACA_SYSTEM_TIME, offsetof(struct paca_struct, system_time));
	DEFINE(PACA_DATA_OFFSET, offsetof(struct paca_struct, data_offset));
	DEFINE(PACA_TRAP_SAVE, offsetof(struct paca_struct, trap_save));
#endif /* CONFIG_PPC64 */

	/* RTAS */
	DEFINE(RTASBASE, offsetof(struct rtas_t, base));
	DEFINE(RTASENTRY, offsetof(struct rtas_t, entry));

	/* Interrupt register frame */
	DEFINE(STACK_FRAME_OVERHEAD, STACK_FRAME_OVERHEAD);
	DEFINE(INT_FRAME_SIZE, STACK_INT_FRAME_SIZE);
#ifdef CONFIG_PPC64
	DEFINE(SWITCH_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs));
	/* Create extra stack space for SRR0 and SRR1 when calling prom/rtas. */
	DEFINE(PROM_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 16);
	DEFINE(RTAS_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 16);

	/* hcall statistics */
	DEFINE(HCALL_STAT_SIZE, sizeof(struct hcall_stats));
	DEFINE(HCALL_STAT_CALLS, offsetof(struct hcall_stats, num_calls));
	DEFINE(HCALL_STAT_TB, offsetof(struct hcall_stats, tb_total));
	DEFINE(HCALL_STAT_PURR, offsetof(struct hcall_stats, purr_total));
#endif /* CONFIG_PPC64 */
	DEFINE(GPR0, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[0]));
	DEFINE(GPR1, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[1]));
	DEFINE(GPR2, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[2]));
	DEFINE(GPR3, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[3]));
	DEFINE(GPR4, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[4]));
	DEFINE(GPR5, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[5]));
	DEFINE(GPR6, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[6]));
	DEFINE(GPR7, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[7]));
	DEFINE(GPR8, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[8]));
	DEFINE(GPR9, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[9]));
	DEFINE(GPR10, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[10]));
	DEFINE(GPR11, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[11]));
	DEFINE(GPR12, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[12]));
	DEFINE(GPR13, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[13]));
#ifndef CONFIG_PPC64
	DEFINE(GPR14, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[14]));
	DEFINE(GPR15, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[15]));
	DEFINE(GPR16, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[16]));
	DEFINE(GPR17, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[17]));
	DEFINE(GPR18, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[18]));
	DEFINE(GPR19, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[19]));
	DEFINE(GPR20, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[20]));
	DEFINE(GPR21, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[21]));
	DEFINE(GPR22, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[22]));
	DEFINE(GPR23, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[23]));
	DEFINE(GPR24, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[24]));
	DEFINE(GPR25, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[25]));
	DEFINE(GPR26, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[26]));
	DEFINE(GPR27, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[27]));
	DEFINE(GPR28, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[28]));
	DEFINE(GPR29, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[29]));
	DEFINE(GPR30, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[30]));
	DEFINE(GPR31, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[31]));
#endif /* CONFIG_PPC64 */
	/*
	 * Note: these symbols include _ because they overlap with special
	 * register names
	 */
	DEFINE(_NIP, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, nip));
	DEFINE(_MSR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, msr));
	DEFINE(_CTR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, ctr));
	DEFINE(_LINK, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, link));
	DEFINE(_CCR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, ccr));
	DEFINE(_XER, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, xer));
	DEFINE(_DAR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dar));
	DEFINE(_DSISR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dsisr));
	DEFINE(ORIG_GPR3, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, orig_gpr3));
	DEFINE(RESULT, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, result));
	DEFINE(_TRAP, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, trap));
#ifndef CONFIG_PPC64
	DEFINE(_MQ, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, mq));
	/*
	 * The PowerPC 400-class & Book-E processors have neither the DAR
	 * nor the DSISR SPRs. Hence, we overload them to hold the similar
	 * DEAR and ESR SPRs for such processors.  For critical interrupts
	 * we use them to hold SRR0 and SRR1.
	 */
	DEFINE(_DEAR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dar));
	DEFINE(_ESR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dsisr));
#else /* CONFIG_PPC64 */
	DEFINE(SOFTE, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, softe));

	/* These _only_ to be used with {PROM,RTAS}_FRAME_SIZE!!! */
	DEFINE(_SRR0, STACK_FRAME_OVERHEAD+sizeof(struct pt_regs));
	DEFINE(_SRR1, STACK_FRAME_OVERHEAD+sizeof(struct pt_regs)+8);
#endif /* CONFIG_PPC64 */

#if defined(CONFIG_PPC32)
#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
	DEFINE(EXC_LVL_SIZE, STACK_EXC_LVL_FRAME_SIZE);
	DEFINE(MAS0, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, mas0));
	/* we overload MMUCR for 44x on MAS0 since they are mutually exclusive */
	DEFINE(MMUCR, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, mas0));
	DEFINE(MAS1, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, mas1));
	DEFINE(MAS2, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, mas2));
	DEFINE(MAS3, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, mas3));
	DEFINE(MAS6, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, mas6));
	DEFINE(MAS7, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, mas7));
	DEFINE(_SRR0, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, srr0));
	DEFINE(_SRR1, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, srr1));
	DEFINE(_CSRR0, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, csrr0));
	DEFINE(_CSRR1, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, csrr1));
	DEFINE(_DSRR0, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, dsrr0));
	DEFINE(_DSRR1, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, dsrr1));
	DEFINE(SAVED_KSP_LIMIT, STACK_INT_FRAME_SIZE+offsetof(struct exception_regs, saved_ksp_limit));
#endif
#endif
	DEFINE(CLONE_VM, CLONE_VM);
	DEFINE(CLONE_UNTRACED, CLONE_UNTRACED);

#ifndef CONFIG_PPC64
	DEFINE(MM_PGD, offsetof(struct mm_struct, pgd));
#endif /* ! CONFIG_PPC64 */

	/* About the CPU features table */
	DEFINE(CPU_SPEC_FEATURES, offsetof(struct cpu_spec, cpu_features));
	DEFINE(CPU_SPEC_SETUP, offsetof(struct cpu_spec, cpu_setup));
	DEFINE(CPU_SPEC_RESTORE, offsetof(struct cpu_spec, cpu_restore));

	DEFINE(pbe_address, offsetof(struct pbe, address));
	DEFINE(pbe_orig_address, offsetof(struct pbe, orig_address));
	DEFINE(pbe_next, offsetof(struct pbe, next));

#ifndef CONFIG_PPC64
	DEFINE(TASK_SIZE, TASK_SIZE);
	DEFINE(NUM_USER_SEGMENTS, TASK_SIZE>>28);
#endif /* ! CONFIG_PPC64 */

	/* datapage offsets for use by vdso */
	DEFINE(CFG_TB_ORIG_STAMP, offsetof(struct vdso_data, tb_orig_stamp));
	DEFINE(CFG_TB_TICKS_PER_SEC, offsetof(struct vdso_data, tb_ticks_per_sec));
	DEFINE(CFG_TB_TO_XS, offsetof(struct vdso_data, tb_to_xs));
	DEFINE(CFG_STAMP_XSEC, offsetof(struct vdso_data, stamp_xsec));
	DEFINE(CFG_TB_UPDATE_COUNT, offsetof(struct vdso_data, tb_update_count));
	DEFINE(CFG_TZ_MINUTEWEST, offsetof(struct vdso_data, tz_minuteswest));
	DEFINE(CFG_TZ_DSTTIME, offsetof(struct vdso_data, tz_dsttime));
	DEFINE(CFG_SYSCALL_MAP32, offsetof(struct vdso_data, syscall_map_32));
	DEFINE(WTOM_CLOCK_SEC, offsetof(struct vdso_data, wtom_clock_sec));
	DEFINE(WTOM_CLOCK_NSEC, offsetof(struct vdso_data, wtom_clock_nsec));
	DEFINE(STAMP_XTIME, offsetof(struct vdso_data, stamp_xtime));
	DEFINE(CFG_ICACHE_BLOCKSZ, offsetof(struct vdso_data, icache_block_size));
	DEFINE(CFG_DCACHE_BLOCKSZ, offsetof(struct vdso_data, dcache_block_size));
	DEFINE(CFG_ICACHE_LOGBLOCKSZ, offsetof(struct vdso_data, icache_log_block_size));
	DEFINE(CFG_DCACHE_LOGBLOCKSZ, offsetof(struct vdso_data, dcache_log_block_size));
#ifdef CONFIG_PPC64
	DEFINE(CFG_SYSCALL_MAP64, offsetof(struct vdso_data, syscall_map_64));
	DEFINE(TVAL64_TV_SEC, offsetof(struct timeval, tv_sec));
	DEFINE(TVAL64_TV_USEC, offsetof(struct timeval, tv_usec));
	DEFINE(TVAL32_TV_SEC, offsetof(struct compat_timeval, tv_sec));
	DEFINE(TVAL32_TV_USEC, offsetof(struct compat_timeval, tv_usec));
	DEFINE(TSPC64_TV_SEC, offsetof(struct timespec, tv_sec));
	DEFINE(TSPC64_TV_NSEC, offsetof(struct timespec, tv_nsec));
	DEFINE(TSPC32_TV_SEC, offsetof(struct compat_timespec, tv_sec));
	DEFINE(TSPC32_TV_NSEC, offsetof(struct compat_timespec, tv_nsec));
#else
	DEFINE(TVAL32_TV_SEC, offsetof(struct timeval, tv_sec));
	DEFINE(TVAL32_TV_USEC, offsetof(struct timeval, tv_usec));
	DEFINE(TSPC32_TV_SEC, offsetof(struct timespec, tv_sec));
	DEFINE(TSPC32_TV_NSEC, offsetof(struct timespec, tv_nsec));
#endif
	/* timeval/timezone offsets for use by vdso */
	DEFINE(TZONE_TZ_MINWEST, offsetof(struct timezone, tz_minuteswest));
	DEFINE(TZONE_TZ_DSTTIME, offsetof(struct timezone, tz_dsttime));

	/* Other bits used by the vdso */
	DEFINE(CLOCK_REALTIME, CLOCK_REALTIME);
	DEFINE(CLOCK_MONOTONIC, CLOCK_MONOTONIC);
	DEFINE(NSEC_PER_SEC, NSEC_PER_SEC);
	DEFINE(CLOCK_REALTIME_RES, MONOTONIC_RES_NSEC);

#ifdef CONFIG_BUG
	DEFINE(BUG_ENTRY_SIZE, sizeof(struct bug_entry));
#endif

#ifdef CONFIG_PPC_ISERIES
	/* the assembler miscalculates the VSID values */
	DEFINE(PAGE_OFFSET_ESID, GET_ESID(PAGE_OFFSET));
	DEFINE(PAGE_OFFSET_VSID, KERNEL_VSID(PAGE_OFFSET));
	DEFINE(VMALLOC_START_ESID, GET_ESID(VMALLOC_START));
	DEFINE(VMALLOC_START_VSID, KERNEL_VSID(VMALLOC_START));

	/* alpaca */
	DEFINE(ALPACA_SIZE, sizeof(struct alpaca));
#endif

	DEFINE(PGD_TABLE_SIZE, PGD_TABLE_SIZE);
	DEFINE(PTE_SIZE, sizeof(pte_t));

#ifdef CONFIG_KVM
	DEFINE(VCPU_HOST_STACK, offsetof(struct kvm_vcpu, arch.host_stack));
	DEFINE(VCPU_HOST_PID, offsetof(struct kvm_vcpu, arch.host_pid));
	DEFINE(VCPU_GPRS, offsetof(struct kvm_vcpu, arch.gpr));
	DEFINE(VCPU_LR, offsetof(struct kvm_vcpu, arch.lr));
	DEFINE(VCPU_CR, offsetof(struct kvm_vcpu, arch.cr));
	DEFINE(VCPU_XER, offsetof(struct kvm_vcpu, arch.xer));
	DEFINE(VCPU_CTR, offsetof(struct kvm_vcpu, arch.ctr));
	DEFINE(VCPU_PC, offsetof(struct kvm_vcpu, arch.pc));
	DEFINE(VCPU_MSR, offsetof(struct kvm_vcpu, arch.msr));
	DEFINE(VCPU_SPRG4, offsetof(struct kvm_vcpu, arch.sprg4));
	DEFINE(VCPU_SPRG5, offsetof(struct kvm_vcpu, arch.sprg5));
	DEFINE(VCPU_SPRG6, offsetof(struct kvm_vcpu, arch.sprg6));
	DEFINE(VCPU_SPRG7, offsetof(struct kvm_vcpu, arch.sprg7));
	DEFINE(VCPU_SHADOW_PID, offsetof(struct kvm_vcpu, arch.shadow_pid));

	DEFINE(VCPU_LAST_INST, offsetof(struct kvm_vcpu, arch.last_inst));
	DEFINE(VCPU_FAULT_DEAR, offsetof(struct kvm_vcpu, arch.fault_dear));
	DEFINE(VCPU_FAULT_ESR, offsetof(struct kvm_vcpu, arch.fault_esr));
#endif
#ifdef CONFIG_44x
	DEFINE(PGD_T_LOG2, PGD_T_LOG2);
	DEFINE(PTE_T_LOG2, PTE_T_LOG2);
#endif
#ifdef CONFIG_FSL_BOOKE
	DEFINE(TLBCAM_SIZE, sizeof(struct tlbcam));
#endif

#ifdef CONFIG_KVM_EXIT_TIMING
	DEFINE(VCPU_TIMING_EXIT_TBU, offsetof(struct kvm_vcpu,
						arch.timing_exit.tv32.tbu));
	DEFINE(VCPU_TIMING_EXIT_TBL, offsetof(struct kvm_vcpu,
						arch.timing_exit.tv32.tbl));
	DEFINE(VCPU_TIMING_LAST_ENTER_TBU, offsetof(struct kvm_vcpu,
					arch.timing_last_enter.tv32.tbu));
	DEFINE(VCPU_TIMING_LAST_ENTER_TBL, offsetof(struct kvm_vcpu,
					arch.timing_last_enter.tv32.tbl));
#endif

	return 0;
}
