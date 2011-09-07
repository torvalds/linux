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
#if defined(CONFIG_KVM) || defined(CONFIG_KVM_GUEST)
#include <linux/kvm_host.h>
#endif
#if defined(CONFIG_KVM) && defined(CONFIG_PPC_BOOK3S)
#include <asm/kvm_book3s.h>
#endif

#ifdef CONFIG_PPC32
#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
#include "head_booke.h"
#endif
#endif

#if defined(CONFIG_PPC_FSL_BOOK3E)
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
	DEFINE(THREAD_DSCR, offsetof(struct thread_struct, dscr));
#else
	DEFINE(THREAD_INFO, offsetof(struct task_struct, stack));
#endif /* CONFIG_PPC64 */

	DEFINE(KSP, offsetof(struct thread_struct, ksp));
	DEFINE(KSP_LIMIT, offsetof(struct thread_struct, ksp_limit));
	DEFINE(PT_REGS, offsetof(struct thread_struct, regs));
#ifdef CONFIG_BOOKE
	DEFINE(THREAD_NORMSAVES, offsetof(struct thread_struct, normsave[0]));
#endif
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
#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
	DEFINE(THREAD_KVM_SVCPU, offsetof(struct thread_struct, kvm_shadow_vcpu));
#endif

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
	DEFINE(PACA_LOCK_TOKEN, offsetof(struct paca_struct, lock_token));
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
	DEFINE(SLBSHADOW_SAVEAREA, offsetof(struct slb_shadow, save_area));
	DEFINE(LPPACASRR0, offsetof(struct lppaca, saved_srr0));
	DEFINE(LPPACASRR1, offsetof(struct lppaca, saved_srr1));
	DEFINE(LPPACAANYINT, offsetof(struct lppaca, int_dword.any_int));
	DEFINE(LPPACADECRINT, offsetof(struct lppaca, int_dword.fields.decr_int));
	DEFINE(LPPACA_PMCINUSE, offsetof(struct lppaca, pmcregs_in_use));
	DEFINE(LPPACA_DTLIDX, offsetof(struct lppaca, dtl_idx));
	DEFINE(LPPACA_YIELDCOUNT, offsetof(struct lppaca, yield_count));
	DEFINE(PACA_DTL_RIDX, offsetof(struct paca_struct, dtl_ridx));
#endif /* CONFIG_PPC_STD_MMU_64 */
	DEFINE(PACAEMERGSP, offsetof(struct paca_struct, emergency_sp));
	DEFINE(PACAHWCPUID, offsetof(struct paca_struct, hw_cpu_id));
	DEFINE(PACAKEXECSTATE, offsetof(struct paca_struct, kexec_state));
	DEFINE(PACA_STARTTIME, offsetof(struct paca_struct, starttime));
	DEFINE(PACA_STARTTIME_USER, offsetof(struct paca_struct, starttime_user));
	DEFINE(PACA_USER_TIME, offsetof(struct paca_struct, user_time));
	DEFINE(PACA_SYSTEM_TIME, offsetof(struct paca_struct, system_time));
	DEFINE(PACA_TRAP_SAVE, offsetof(struct paca_struct, trap_save));
#endif /* CONFIG_PPC64 */

	/* RTAS */
	DEFINE(RTASBASE, offsetof(struct rtas_t, base));
	DEFINE(RTASENTRY, offsetof(struct rtas_t, entry));

	/* Interrupt register frame */
	DEFINE(INT_FRAME_SIZE, STACK_INT_FRAME_SIZE);
	DEFINE(SWITCH_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs));
#ifdef CONFIG_PPC64
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
	DEFINE(STAMP_SEC_FRAC, offsetof(struct vdso_data, stamp_sec_fraction));
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
	DEFINE(VCPU_VRSAVE, offsetof(struct kvm_vcpu, arch.vrsave));
	DEFINE(VCPU_FPRS, offsetof(struct kvm_vcpu, arch.fpr));
	DEFINE(VCPU_FPSCR, offsetof(struct kvm_vcpu, arch.fpscr));
#ifdef CONFIG_ALTIVEC
	DEFINE(VCPU_VRS, offsetof(struct kvm_vcpu, arch.vr));
	DEFINE(VCPU_VSCR, offsetof(struct kvm_vcpu, arch.vscr));
#endif
#ifdef CONFIG_VSX
	DEFINE(VCPU_VSRS, offsetof(struct kvm_vcpu, arch.vsr));
#endif
	DEFINE(VCPU_XER, offsetof(struct kvm_vcpu, arch.xer));
	DEFINE(VCPU_CTR, offsetof(struct kvm_vcpu, arch.ctr));
	DEFINE(VCPU_LR, offsetof(struct kvm_vcpu, arch.lr));
	DEFINE(VCPU_CR, offsetof(struct kvm_vcpu, arch.cr));
	DEFINE(VCPU_PC, offsetof(struct kvm_vcpu, arch.pc));
#ifdef CONFIG_KVM_BOOK3S_64_HV
	DEFINE(VCPU_MSR, offsetof(struct kvm_vcpu, arch.shregs.msr));
	DEFINE(VCPU_SRR0, offsetof(struct kvm_vcpu, arch.shregs.srr0));
	DEFINE(VCPU_SRR1, offsetof(struct kvm_vcpu, arch.shregs.srr1));
	DEFINE(VCPU_SPRG0, offsetof(struct kvm_vcpu, arch.shregs.sprg0));
	DEFINE(VCPU_SPRG1, offsetof(struct kvm_vcpu, arch.shregs.sprg1));
	DEFINE(VCPU_SPRG2, offsetof(struct kvm_vcpu, arch.shregs.sprg2));
	DEFINE(VCPU_SPRG3, offsetof(struct kvm_vcpu, arch.shregs.sprg3));
#endif
	DEFINE(VCPU_SPRG4, offsetof(struct kvm_vcpu, arch.sprg4));
	DEFINE(VCPU_SPRG5, offsetof(struct kvm_vcpu, arch.sprg5));
	DEFINE(VCPU_SPRG6, offsetof(struct kvm_vcpu, arch.sprg6));
	DEFINE(VCPU_SPRG7, offsetof(struct kvm_vcpu, arch.sprg7));
	DEFINE(VCPU_SHADOW_PID, offsetof(struct kvm_vcpu, arch.shadow_pid));
	DEFINE(VCPU_SHADOW_PID1, offsetof(struct kvm_vcpu, arch.shadow_pid1));
	DEFINE(VCPU_SHARED, offsetof(struct kvm_vcpu, arch.shared));
	DEFINE(VCPU_SHARED_MSR, offsetof(struct kvm_vcpu_arch_shared, msr));
	DEFINE(VCPU_SHADOW_MSR, offsetof(struct kvm_vcpu, arch.shadow_msr));

	/* book3s */
#ifdef CONFIG_KVM_BOOK3S_64_HV
	DEFINE(KVM_LPID, offsetof(struct kvm, arch.lpid));
	DEFINE(KVM_SDR1, offsetof(struct kvm, arch.sdr1));
	DEFINE(KVM_HOST_LPID, offsetof(struct kvm, arch.host_lpid));
	DEFINE(KVM_HOST_LPCR, offsetof(struct kvm, arch.host_lpcr));
	DEFINE(KVM_HOST_SDR1, offsetof(struct kvm, arch.host_sdr1));
	DEFINE(KVM_TLBIE_LOCK, offsetof(struct kvm, arch.tlbie_lock));
	DEFINE(KVM_ONLINE_CPUS, offsetof(struct kvm, online_vcpus.counter));
	DEFINE(KVM_LAST_VCPU, offsetof(struct kvm, arch.last_vcpu));
	DEFINE(KVM_LPCR, offsetof(struct kvm, arch.lpcr));
	DEFINE(KVM_RMOR, offsetof(struct kvm, arch.rmor));
	DEFINE(VCPU_DSISR, offsetof(struct kvm_vcpu, arch.shregs.dsisr));
	DEFINE(VCPU_DAR, offsetof(struct kvm_vcpu, arch.shregs.dar));
#endif
#ifdef CONFIG_PPC_BOOK3S
	DEFINE(VCPU_KVM, offsetof(struct kvm_vcpu, kvm));
	DEFINE(VCPU_VCPUID, offsetof(struct kvm_vcpu, vcpu_id));
	DEFINE(VCPU_HOST_RETIP, offsetof(struct kvm_vcpu, arch.host_retip));
	DEFINE(VCPU_HOST_MSR, offsetof(struct kvm_vcpu, arch.host_msr));
	DEFINE(VCPU_PURR, offsetof(struct kvm_vcpu, arch.purr));
	DEFINE(VCPU_SPURR, offsetof(struct kvm_vcpu, arch.spurr));
	DEFINE(VCPU_DSCR, offsetof(struct kvm_vcpu, arch.dscr));
	DEFINE(VCPU_AMR, offsetof(struct kvm_vcpu, arch.amr));
	DEFINE(VCPU_UAMOR, offsetof(struct kvm_vcpu, arch.uamor));
	DEFINE(VCPU_CTRL, offsetof(struct kvm_vcpu, arch.ctrl));
	DEFINE(VCPU_DABR, offsetof(struct kvm_vcpu, arch.dabr));
	DEFINE(VCPU_TRAMPOLINE_LOWMEM, offsetof(struct kvm_vcpu, arch.trampoline_lowmem));
	DEFINE(VCPU_TRAMPOLINE_ENTER, offsetof(struct kvm_vcpu, arch.trampoline_enter));
	DEFINE(VCPU_HIGHMEM_HANDLER, offsetof(struct kvm_vcpu, arch.highmem_handler));
	DEFINE(VCPU_RMCALL, offsetof(struct kvm_vcpu, arch.rmcall));
	DEFINE(VCPU_HFLAGS, offsetof(struct kvm_vcpu, arch.hflags));
	DEFINE(VCPU_DEC, offsetof(struct kvm_vcpu, arch.dec));
	DEFINE(VCPU_DEC_EXPIRES, offsetof(struct kvm_vcpu, arch.dec_expires));
	DEFINE(VCPU_PENDING_EXC, offsetof(struct kvm_vcpu, arch.pending_exceptions));
	DEFINE(VCPU_VPA, offsetof(struct kvm_vcpu, arch.vpa));
	DEFINE(VCPU_MMCR, offsetof(struct kvm_vcpu, arch.mmcr));
	DEFINE(VCPU_PMC, offsetof(struct kvm_vcpu, arch.pmc));
	DEFINE(VCPU_SLB, offsetof(struct kvm_vcpu, arch.slb));
	DEFINE(VCPU_SLB_MAX, offsetof(struct kvm_vcpu, arch.slb_max));
	DEFINE(VCPU_SLB_NR, offsetof(struct kvm_vcpu, arch.slb_nr));
	DEFINE(VCPU_LAST_CPU, offsetof(struct kvm_vcpu, arch.last_cpu));
	DEFINE(VCPU_FAULT_DSISR, offsetof(struct kvm_vcpu, arch.fault_dsisr));
	DEFINE(VCPU_FAULT_DAR, offsetof(struct kvm_vcpu, arch.fault_dar));
	DEFINE(VCPU_LAST_INST, offsetof(struct kvm_vcpu, arch.last_inst));
	DEFINE(VCPU_TRAP, offsetof(struct kvm_vcpu, arch.trap));
	DEFINE(VCPU_PTID, offsetof(struct kvm_vcpu, arch.ptid));
	DEFINE(VCORE_ENTRY_EXIT, offsetof(struct kvmppc_vcore, entry_exit_count));
	DEFINE(VCORE_NAP_COUNT, offsetof(struct kvmppc_vcore, nap_count));
	DEFINE(VCORE_IN_GUEST, offsetof(struct kvmppc_vcore, in_guest));
	DEFINE(VCPU_SVCPU, offsetof(struct kvmppc_vcpu_book3s, shadow_vcpu) -
			   offsetof(struct kvmppc_vcpu_book3s, vcpu));
	DEFINE(VCPU_SLB_E, offsetof(struct kvmppc_slb, orige));
	DEFINE(VCPU_SLB_V, offsetof(struct kvmppc_slb, origv));
	DEFINE(VCPU_SLB_SIZE, sizeof(struct kvmppc_slb));

#ifdef CONFIG_PPC_BOOK3S_64
#ifdef CONFIG_KVM_BOOK3S_PR
# define SVCPU_FIELD(x, f)	DEFINE(x, offsetof(struct paca_struct, shadow_vcpu.f))
#else
# define SVCPU_FIELD(x, f)
#endif
# define HSTATE_FIELD(x, f)	DEFINE(x, offsetof(struct paca_struct, kvm_hstate.f))
#else	/* 32-bit */
# define SVCPU_FIELD(x, f)	DEFINE(x, offsetof(struct kvmppc_book3s_shadow_vcpu, f))
# define HSTATE_FIELD(x, f)	DEFINE(x, offsetof(struct kvmppc_book3s_shadow_vcpu, hstate.f))
#endif

	SVCPU_FIELD(SVCPU_CR, cr);
	SVCPU_FIELD(SVCPU_XER, xer);
	SVCPU_FIELD(SVCPU_CTR, ctr);
	SVCPU_FIELD(SVCPU_LR, lr);
	SVCPU_FIELD(SVCPU_PC, pc);
	SVCPU_FIELD(SVCPU_R0, gpr[0]);
	SVCPU_FIELD(SVCPU_R1, gpr[1]);
	SVCPU_FIELD(SVCPU_R2, gpr[2]);
	SVCPU_FIELD(SVCPU_R3, gpr[3]);
	SVCPU_FIELD(SVCPU_R4, gpr[4]);
	SVCPU_FIELD(SVCPU_R5, gpr[5]);
	SVCPU_FIELD(SVCPU_R6, gpr[6]);
	SVCPU_FIELD(SVCPU_R7, gpr[7]);
	SVCPU_FIELD(SVCPU_R8, gpr[8]);
	SVCPU_FIELD(SVCPU_R9, gpr[9]);
	SVCPU_FIELD(SVCPU_R10, gpr[10]);
	SVCPU_FIELD(SVCPU_R11, gpr[11]);
	SVCPU_FIELD(SVCPU_R12, gpr[12]);
	SVCPU_FIELD(SVCPU_R13, gpr[13]);
	SVCPU_FIELD(SVCPU_FAULT_DSISR, fault_dsisr);
	SVCPU_FIELD(SVCPU_FAULT_DAR, fault_dar);
	SVCPU_FIELD(SVCPU_LAST_INST, last_inst);
	SVCPU_FIELD(SVCPU_SHADOW_SRR1, shadow_srr1);
#ifdef CONFIG_PPC_BOOK3S_32
	SVCPU_FIELD(SVCPU_SR, sr);
#endif
#ifdef CONFIG_PPC64
	SVCPU_FIELD(SVCPU_SLB, slb);
	SVCPU_FIELD(SVCPU_SLB_MAX, slb_max);
#endif

	HSTATE_FIELD(HSTATE_HOST_R1, host_r1);
	HSTATE_FIELD(HSTATE_HOST_R2, host_r2);
	HSTATE_FIELD(HSTATE_HOST_MSR, host_msr);
	HSTATE_FIELD(HSTATE_VMHANDLER, vmhandler);
	HSTATE_FIELD(HSTATE_SCRATCH0, scratch0);
	HSTATE_FIELD(HSTATE_SCRATCH1, scratch1);
	HSTATE_FIELD(HSTATE_IN_GUEST, in_guest);

#ifdef CONFIG_KVM_BOOK3S_64_HV
	HSTATE_FIELD(HSTATE_KVM_VCPU, kvm_vcpu);
	HSTATE_FIELD(HSTATE_KVM_VCORE, kvm_vcore);
	HSTATE_FIELD(HSTATE_XICS_PHYS, xics_phys);
	HSTATE_FIELD(HSTATE_MMCR, host_mmcr);
	HSTATE_FIELD(HSTATE_PMC, host_pmc);
	HSTATE_FIELD(HSTATE_PURR, host_purr);
	HSTATE_FIELD(HSTATE_SPURR, host_spurr);
	HSTATE_FIELD(HSTATE_DSCR, host_dscr);
	HSTATE_FIELD(HSTATE_DABR, dabr);
	HSTATE_FIELD(HSTATE_DECEXP, dec_expires);
#endif /* CONFIG_KVM_BOOK3S_64_HV */

#else /* CONFIG_PPC_BOOK3S */
	DEFINE(VCPU_CR, offsetof(struct kvm_vcpu, arch.cr));
	DEFINE(VCPU_XER, offsetof(struct kvm_vcpu, arch.xer));
	DEFINE(VCPU_LR, offsetof(struct kvm_vcpu, arch.lr));
	DEFINE(VCPU_CTR, offsetof(struct kvm_vcpu, arch.ctr));
	DEFINE(VCPU_PC, offsetof(struct kvm_vcpu, arch.pc));
	DEFINE(VCPU_LAST_INST, offsetof(struct kvm_vcpu, arch.last_inst));
	DEFINE(VCPU_FAULT_DEAR, offsetof(struct kvm_vcpu, arch.fault_dear));
	DEFINE(VCPU_FAULT_ESR, offsetof(struct kvm_vcpu, arch.fault_esr));
#endif /* CONFIG_PPC_BOOK3S */
#endif /* CONFIG_KVM */

#ifdef CONFIG_KVM_GUEST
	DEFINE(KVM_MAGIC_SCRATCH1, offsetof(struct kvm_vcpu_arch_shared,
					    scratch1));
	DEFINE(KVM_MAGIC_SCRATCH2, offsetof(struct kvm_vcpu_arch_shared,
					    scratch2));
	DEFINE(KVM_MAGIC_SCRATCH3, offsetof(struct kvm_vcpu_arch_shared,
					    scratch3));
	DEFINE(KVM_MAGIC_INT, offsetof(struct kvm_vcpu_arch_shared,
				       int_pending));
	DEFINE(KVM_MAGIC_MSR, offsetof(struct kvm_vcpu_arch_shared, msr));
	DEFINE(KVM_MAGIC_CRITICAL, offsetof(struct kvm_vcpu_arch_shared,
					    critical));
	DEFINE(KVM_MAGIC_SR, offsetof(struct kvm_vcpu_arch_shared, sr));
#endif

#ifdef CONFIG_44x
	DEFINE(PGD_T_LOG2, PGD_T_LOG2);
	DEFINE(PTE_T_LOG2, PTE_T_LOG2);
#endif
#ifdef CONFIG_PPC_FSL_BOOK3E
	DEFINE(TLBCAM_SIZE, sizeof(struct tlbcam));
	DEFINE(TLBCAM_MAS0, offsetof(struct tlbcam, MAS0));
	DEFINE(TLBCAM_MAS1, offsetof(struct tlbcam, MAS1));
	DEFINE(TLBCAM_MAS2, offsetof(struct tlbcam, MAS2));
	DEFINE(TLBCAM_MAS3, offsetof(struct tlbcam, MAS3));
	DEFINE(TLBCAM_MAS7, offsetof(struct tlbcam, MAS7));
#endif

#if defined(CONFIG_KVM) && defined(CONFIG_SPE)
	DEFINE(VCPU_EVR, offsetof(struct kvm_vcpu, arch.evr[0]));
	DEFINE(VCPU_ACC, offsetof(struct kvm_vcpu, arch.acc));
	DEFINE(VCPU_SPEFSCR, offsetof(struct kvm_vcpu, arch.spefscr));
	DEFINE(VCPU_HOST_SPEFSCR, offsetof(struct kvm_vcpu, arch.host_spefscr));
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
