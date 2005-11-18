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

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#ifdef CONFIG_PPC64
#include <linux/time.h>
#include <linux/hardirq.h>
#else
#include <linux/ptrace.h>
#include <linux/suspend.h>
#endif

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
#include <asm/iseries/hv_lp_event.h>
#include <asm/cache.h>
#include <asm/compat.h>
#endif

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
	DEFINE(THREAD, offsetof(struct task_struct, thread));
	DEFINE(MM, offsetof(struct task_struct, mm));
#ifdef CONFIG_PPC64
	DEFINE(AUDITCONTEXT, offsetof(struct task_struct, audit_context));
#else
	DEFINE(THREAD_INFO, offsetof(struct task_struct, thread_info));
	DEFINE(PTRACE, offsetof(struct task_struct, ptrace));
#endif /* CONFIG_PPC64 */

	DEFINE(KSP, offsetof(struct thread_struct, ksp));
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
#ifdef CONFIG_PPC64
	DEFINE(KSP_VSID, offsetof(struct thread_struct, ksp_vsid));
#else /* CONFIG_PPC64 */
	DEFINE(PGDIR, offsetof(struct thread_struct, pgdir));
	DEFINE(LAST_SYSCALL, offsetof(struct thread_struct, last_syscall));
#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
	DEFINE(THREAD_DBCR0, offsetof(struct thread_struct, dbcr0));
	DEFINE(PT_PTRACED, PT_PTRACED);
#endif
#ifdef CONFIG_SPE
	DEFINE(THREAD_EVR0, offsetof(struct thread_struct, evr[0]));
	DEFINE(THREAD_ACC, offsetof(struct thread_struct, acc));
	DEFINE(THREAD_SPEFSCR, offsetof(struct thread_struct, spefscr));
	DEFINE(THREAD_USED_SPE, offsetof(struct thread_struct, used_spe));
#endif /* CONFIG_SPE */
#endif /* CONFIG_PPC64 */

	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_PREEMPT, offsetof(struct thread_info, preempt_count));
	DEFINE(TI_SC_NOERR, offsetof(struct thread_info, syscall_noerror));
#ifdef CONFIG_PPC32
	DEFINE(TI_TASK, offsetof(struct thread_info, task));
	DEFINE(TI_EXECDOMAIN, offsetof(struct thread_info, exec_domain));
	DEFINE(TI_CPU, offsetof(struct thread_info, cpu));
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_PPC64
	DEFINE(DCACHEL1LINESIZE, offsetof(struct ppc64_caches, dline_size));
	DEFINE(DCACHEL1LOGLINESIZE, offsetof(struct ppc64_caches, log_dline_size));
	DEFINE(DCACHEL1LINESPERPAGE, offsetof(struct ppc64_caches, dlines_per_page));
	DEFINE(ICACHEL1LINESIZE, offsetof(struct ppc64_caches, iline_size));
	DEFINE(ICACHEL1LOGLINESIZE, offsetof(struct ppc64_caches, log_iline_size));
	DEFINE(ICACHEL1LINESPERPAGE, offsetof(struct ppc64_caches, ilines_per_page));
	DEFINE(PLATFORM_LPAR, PLATFORM_LPAR);

	/* paca */
	DEFINE(PACA_SIZE, sizeof(struct paca_struct));
	DEFINE(PACAPACAINDEX, offsetof(struct paca_struct, paca_index));
	DEFINE(PACAPROCSTART, offsetof(struct paca_struct, cpu_start));
	DEFINE(PACAKSAVE, offsetof(struct paca_struct, kstack));
	DEFINE(PACACURRENT, offsetof(struct paca_struct, __current));
	DEFINE(PACASAVEDMSR, offsetof(struct paca_struct, saved_msr));
	DEFINE(PACASTABREAL, offsetof(struct paca_struct, stab_real));
	DEFINE(PACASTABVIRT, offsetof(struct paca_struct, stab_addr));
	DEFINE(PACASTABRR, offsetof(struct paca_struct, stab_rr));
	DEFINE(PACAR1, offsetof(struct paca_struct, saved_r1));
	DEFINE(PACATOC, offsetof(struct paca_struct, kernel_toc));
	DEFINE(PACAPROCENABLED, offsetof(struct paca_struct, proc_enabled));
	DEFINE(PACASLBCACHE, offsetof(struct paca_struct, slb_cache));
	DEFINE(PACASLBCACHEPTR, offsetof(struct paca_struct, slb_cache_ptr));
	DEFINE(PACACONTEXTID, offsetof(struct paca_struct, context.id));
#ifdef CONFIG_PPC_64K_PAGES
	DEFINE(PACAPGDIR, offsetof(struct paca_struct, pgdir));
#endif
#ifdef CONFIG_HUGETLB_PAGE
	DEFINE(PACALOWHTLBAREAS, offsetof(struct paca_struct, context.low_htlb_areas));
	DEFINE(PACAHIGHHTLBAREAS, offsetof(struct paca_struct, context.high_htlb_areas));
#endif /* CONFIG_HUGETLB_PAGE */
	DEFINE(PACADEFAULTDECR, offsetof(struct paca_struct, default_decr));
	DEFINE(PACA_EXGEN, offsetof(struct paca_struct, exgen));
	DEFINE(PACA_EXMC, offsetof(struct paca_struct, exmc));
	DEFINE(PACA_EXSLB, offsetof(struct paca_struct, exslb));
	DEFINE(PACA_EXDSI, offsetof(struct paca_struct, exdsi));
	DEFINE(PACAEMERGSP, offsetof(struct paca_struct, emergency_sp));
	DEFINE(PACALPPACA, offsetof(struct paca_struct, lppaca));
	DEFINE(PACAHWCPUID, offsetof(struct paca_struct, hw_cpu_id));

	DEFINE(LPPACASRR0, offsetof(struct lppaca, saved_srr0));
	DEFINE(LPPACASRR1, offsetof(struct lppaca, saved_srr1));
	DEFINE(LPPACAANYINT, offsetof(struct lppaca, int_dword.any_int));
	DEFINE(LPPACADECRINT, offsetof(struct lppaca, int_dword.fields.decr_int));
#endif /* CONFIG_PPC64 */

	/* RTAS */
	DEFINE(RTASBASE, offsetof(struct rtas_t, base));
	DEFINE(RTASENTRY, offsetof(struct rtas_t, entry));

	/* Interrupt register frame */
	DEFINE(STACK_FRAME_OVERHEAD, STACK_FRAME_OVERHEAD);
#ifndef CONFIG_PPC64
	DEFINE(INT_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs));
#else /* CONFIG_PPC64 */
	DEFINE(SWITCH_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs));
	/* 288 = # of volatile regs, int & fp, for leaf routines */
	/* which do not stack a frame.  See the PPC64 ABI.       */
	DEFINE(INT_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 288);
	/* Create extra stack space for SRR0 and SRR1 when calling prom/rtas. */
	DEFINE(PROM_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 16);
	DEFINE(RTAS_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 16);
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

	DEFINE(CLONE_VM, CLONE_VM);
	DEFINE(CLONE_UNTRACED, CLONE_UNTRACED);

#ifndef CONFIG_PPC64
	DEFINE(MM_PGD, offsetof(struct mm_struct, pgd));
#endif /* ! CONFIG_PPC64 */

	/* About the CPU features table */
	DEFINE(CPU_SPEC_ENTRY_SIZE, sizeof(struct cpu_spec));
	DEFINE(CPU_SPEC_PVR_MASK, offsetof(struct cpu_spec, pvr_mask));
	DEFINE(CPU_SPEC_PVR_VALUE, offsetof(struct cpu_spec, pvr_value));
	DEFINE(CPU_SPEC_FEATURES, offsetof(struct cpu_spec, cpu_features));
	DEFINE(CPU_SPEC_SETUP, offsetof(struct cpu_spec, cpu_setup));

#ifndef CONFIG_PPC64
	DEFINE(pbe_address, offsetof(struct pbe, address));
	DEFINE(pbe_orig_address, offsetof(struct pbe, orig_address));
	DEFINE(pbe_next, offsetof(struct pbe, next));

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
	DEFINE(CLOCK_REALTIME_RES, TICK_NSEC);

	return 0;
}
