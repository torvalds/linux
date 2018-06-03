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
#include <asm/dbell.h>
#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/lppaca.h>
#include <asm/cache.h>
#include <asm/compat.h>
#include <asm/mmu.h>
#include <asm/hvcall.h>
#include <asm/xics.h>
#endif
#ifdef CONFIG_PPC_POWERNV
#include <asm/opal.h>
#endif
#if defined(CONFIG_KVM) || defined(CONFIG_KVM_GUEST)
#include <linux/kvm_host.h>
#endif
#if defined(CONFIG_KVM) && defined(CONFIG_PPC_BOOK3S)
#include <asm/kvm_book3s.h>
#include <asm/kvm_ppc.h>
#endif

#ifdef CONFIG_PPC32
#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
#include "head_booke.h"
#endif
#endif

#if defined(CONFIG_PPC_FSL_BOOK3E)
#include "../mm/mmu_decl.h"
#endif

#ifdef CONFIG_PPC_8xx
#include <asm/fixmap.h>
#endif

#define STACK_PT_REGS_OFFSET(sym, val)	\
	DEFINE(sym, STACK_FRAME_OVERHEAD + offsetof(struct pt_regs, val))

int main(void)
{
	OFFSET(THREAD, task_struct, thread);
	OFFSET(MM, task_struct, mm);
	OFFSET(MMCONTEXTID, mm_struct, context.id);
#ifdef CONFIG_PPC64
	DEFINE(SIGSEGV, SIGSEGV);
	DEFINE(NMI_MASK, NMI_MASK);
	OFFSET(TASKTHREADPPR, task_struct, thread.ppr);
#else
	OFFSET(THREAD_INFO, task_struct, stack);
	DEFINE(THREAD_INFO_GAP, _ALIGN_UP(sizeof(struct thread_info), 16));
	OFFSET(KSP_LIMIT, thread_struct, ksp_limit);
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_LIVEPATCH
	OFFSET(TI_livepatch_sp, thread_info, livepatch_sp);
#endif

	OFFSET(KSP, thread_struct, ksp);
	OFFSET(PT_REGS, thread_struct, regs);
#ifdef CONFIG_BOOKE
	OFFSET(THREAD_NORMSAVES, thread_struct, normsave[0]);
#endif
	OFFSET(THREAD_FPEXC_MODE, thread_struct, fpexc_mode);
	OFFSET(THREAD_FPSTATE, thread_struct, fp_state.fpr);
	OFFSET(THREAD_FPSAVEAREA, thread_struct, fp_save_area);
	OFFSET(FPSTATE_FPSCR, thread_fp_state, fpscr);
	OFFSET(THREAD_LOAD_FP, thread_struct, load_fp);
#ifdef CONFIG_ALTIVEC
	OFFSET(THREAD_VRSTATE, thread_struct, vr_state.vr);
	OFFSET(THREAD_VRSAVEAREA, thread_struct, vr_save_area);
	OFFSET(THREAD_VRSAVE, thread_struct, vrsave);
	OFFSET(THREAD_USED_VR, thread_struct, used_vr);
	OFFSET(VRSTATE_VSCR, thread_vr_state, vscr);
	OFFSET(THREAD_LOAD_VEC, thread_struct, load_vec);
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_VSX
	OFFSET(THREAD_USED_VSR, thread_struct, used_vsr);
#endif /* CONFIG_VSX */
#ifdef CONFIG_PPC64
	OFFSET(KSP_VSID, thread_struct, ksp_vsid);
#else /* CONFIG_PPC64 */
	OFFSET(PGDIR, thread_struct, pgdir);
#ifdef CONFIG_SPE
	OFFSET(THREAD_EVR0, thread_struct, evr[0]);
	OFFSET(THREAD_ACC, thread_struct, acc);
	OFFSET(THREAD_SPEFSCR, thread_struct, spefscr);
	OFFSET(THREAD_USED_SPE, thread_struct, used_spe);
#endif /* CONFIG_SPE */
#endif /* CONFIG_PPC64 */
#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
	OFFSET(THREAD_DBCR0, thread_struct, debug.dbcr0);
#endif
#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
	OFFSET(THREAD_KVM_SVCPU, thread_struct, kvm_shadow_vcpu);
#endif
#if defined(CONFIG_KVM) && defined(CONFIG_BOOKE)
	OFFSET(THREAD_KVM_VCPU, thread_struct, kvm_vcpu);
#endif

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	OFFSET(PACATMSCRATCH, paca_struct, tm_scratch);
	OFFSET(THREAD_TM_TFHAR, thread_struct, tm_tfhar);
	OFFSET(THREAD_TM_TEXASR, thread_struct, tm_texasr);
	OFFSET(THREAD_TM_TFIAR, thread_struct, tm_tfiar);
	OFFSET(THREAD_TM_TAR, thread_struct, tm_tar);
	OFFSET(THREAD_TM_PPR, thread_struct, tm_ppr);
	OFFSET(THREAD_TM_DSCR, thread_struct, tm_dscr);
	OFFSET(PT_CKPT_REGS, thread_struct, ckpt_regs);
	OFFSET(THREAD_CKVRSTATE, thread_struct, ckvr_state.vr);
	OFFSET(THREAD_CKVRSAVE, thread_struct, ckvrsave);
	OFFSET(THREAD_CKFPSTATE, thread_struct, ckfp_state.fpr);
	/* Local pt_regs on stack for Transactional Memory funcs. */
	DEFINE(TM_FRAME_SIZE, STACK_FRAME_OVERHEAD +
	       sizeof(struct pt_regs) + 16);
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */

	OFFSET(TI_FLAGS, thread_info, flags);
	OFFSET(TI_LOCAL_FLAGS, thread_info, local_flags);
	OFFSET(TI_PREEMPT, thread_info, preempt_count);
	OFFSET(TI_TASK, thread_info, task);
	OFFSET(TI_CPU, thread_info, cpu);

#ifdef CONFIG_PPC64
	OFFSET(DCACHEL1BLOCKSIZE, ppc64_caches, l1d.block_size);
	OFFSET(DCACHEL1LOGBLOCKSIZE, ppc64_caches, l1d.log_block_size);
	OFFSET(DCACHEL1BLOCKSPERPAGE, ppc64_caches, l1d.blocks_per_page);
	OFFSET(ICACHEL1BLOCKSIZE, ppc64_caches, l1i.block_size);
	OFFSET(ICACHEL1LOGBLOCKSIZE, ppc64_caches, l1i.log_block_size);
	OFFSET(ICACHEL1BLOCKSPERPAGE, ppc64_caches, l1i.blocks_per_page);
	/* paca */
	DEFINE(PACA_SIZE, sizeof(struct paca_struct));
	OFFSET(PACAPACAINDEX, paca_struct, paca_index);
	OFFSET(PACAPROCSTART, paca_struct, cpu_start);
	OFFSET(PACAKSAVE, paca_struct, kstack);
	OFFSET(PACACURRENT, paca_struct, __current);
	OFFSET(PACASAVEDMSR, paca_struct, saved_msr);
	OFFSET(PACASTABRR, paca_struct, stab_rr);
	OFFSET(PACAR1, paca_struct, saved_r1);
	OFFSET(PACATOC, paca_struct, kernel_toc);
	OFFSET(PACAKBASE, paca_struct, kernelbase);
	OFFSET(PACAKMSR, paca_struct, kernel_msr);
	OFFSET(PACAIRQSOFTMASK, paca_struct, irq_soft_mask);
	OFFSET(PACAIRQHAPPENED, paca_struct, irq_happened);
#ifdef CONFIG_PPC_BOOK3S
	OFFSET(PACACONTEXTID, paca_struct, mm_ctx_id);
#ifdef CONFIG_PPC_MM_SLICES
	OFFSET(PACALOWSLICESPSIZE, paca_struct, mm_ctx_low_slices_psize);
	OFFSET(PACAHIGHSLICEPSIZE, paca_struct, mm_ctx_high_slices_psize);
	OFFSET(PACA_SLB_ADDR_LIMIT, paca_struct, mm_ctx_slb_addr_limit);
	DEFINE(MMUPSIZEDEFSIZE, sizeof(struct mmu_psize_def));
#endif /* CONFIG_PPC_MM_SLICES */
#endif

#ifdef CONFIG_PPC_BOOK3E
	OFFSET(PACAPGD, paca_struct, pgd);
	OFFSET(PACA_KERNELPGD, paca_struct, kernel_pgd);
	OFFSET(PACA_EXGEN, paca_struct, exgen);
	OFFSET(PACA_EXTLB, paca_struct, extlb);
	OFFSET(PACA_EXMC, paca_struct, exmc);
	OFFSET(PACA_EXCRIT, paca_struct, excrit);
	OFFSET(PACA_EXDBG, paca_struct, exdbg);
	OFFSET(PACA_MC_STACK, paca_struct, mc_kstack);
	OFFSET(PACA_CRIT_STACK, paca_struct, crit_kstack);
	OFFSET(PACA_DBG_STACK, paca_struct, dbg_kstack);
	OFFSET(PACA_TCD_PTR, paca_struct, tcd_ptr);

	OFFSET(TCD_ESEL_NEXT, tlb_core_data, esel_next);
	OFFSET(TCD_ESEL_MAX, tlb_core_data, esel_max);
	OFFSET(TCD_ESEL_FIRST, tlb_core_data, esel_first);
#endif /* CONFIG_PPC_BOOK3E */

#ifdef CONFIG_PPC_BOOK3S_64
	OFFSET(PACASLBCACHE, paca_struct, slb_cache);
	OFFSET(PACASLBCACHEPTR, paca_struct, slb_cache_ptr);
	OFFSET(PACAVMALLOCSLLP, paca_struct, vmalloc_sllp);
#ifdef CONFIG_PPC_MM_SLICES
	OFFSET(MMUPSIZESLLP, mmu_psize_def, sllp);
#else
	OFFSET(PACACONTEXTSLLP, paca_struct, mm_ctx_sllp);
#endif /* CONFIG_PPC_MM_SLICES */
	OFFSET(PACA_EXGEN, paca_struct, exgen);
	OFFSET(PACA_EXMC, paca_struct, exmc);
	OFFSET(PACA_EXSLB, paca_struct, exslb);
	OFFSET(PACA_EXNMI, paca_struct, exnmi);
#ifdef CONFIG_PPC_PSERIES
	OFFSET(PACALPPACAPTR, paca_struct, lppaca_ptr);
#endif
	OFFSET(PACA_SLBSHADOWPTR, paca_struct, slb_shadow_ptr);
	OFFSET(SLBSHADOW_STACKVSID, slb_shadow, save_area[SLB_NUM_BOLTED - 1].vsid);
	OFFSET(SLBSHADOW_STACKESID, slb_shadow, save_area[SLB_NUM_BOLTED - 1].esid);
	OFFSET(SLBSHADOW_SAVEAREA, slb_shadow, save_area);
	OFFSET(LPPACA_PMCINUSE, lppaca, pmcregs_in_use);
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	OFFSET(PACA_PMCINUSE, paca_struct, pmcregs_in_use);
#endif
	OFFSET(LPPACA_DTLIDX, lppaca, dtl_idx);
	OFFSET(LPPACA_YIELDCOUNT, lppaca, yield_count);
	OFFSET(PACA_DTL_RIDX, paca_struct, dtl_ridx);
#endif /* CONFIG_PPC_BOOK3S_64 */
	OFFSET(PACAEMERGSP, paca_struct, emergency_sp);
#ifdef CONFIG_PPC_BOOK3S_64
	OFFSET(PACAMCEMERGSP, paca_struct, mc_emergency_sp);
	OFFSET(PACA_NMI_EMERG_SP, paca_struct, nmi_emergency_sp);
	OFFSET(PACA_IN_MCE, paca_struct, in_mce);
	OFFSET(PACA_IN_NMI, paca_struct, in_nmi);
	OFFSET(PACA_RFI_FLUSH_FALLBACK_AREA, paca_struct, rfi_flush_fallback_area);
	OFFSET(PACA_EXRFI, paca_struct, exrfi);
	OFFSET(PACA_L1D_FLUSH_SIZE, paca_struct, l1d_flush_size);

#endif
	OFFSET(PACAHWCPUID, paca_struct, hw_cpu_id);
	OFFSET(PACAKEXECSTATE, paca_struct, kexec_state);
	OFFSET(PACA_DSCR_DEFAULT, paca_struct, dscr_default);
	OFFSET(ACCOUNT_STARTTIME, paca_struct, accounting.starttime);
	OFFSET(ACCOUNT_STARTTIME_USER, paca_struct, accounting.starttime_user);
	OFFSET(ACCOUNT_USER_TIME, paca_struct, accounting.utime);
	OFFSET(ACCOUNT_SYSTEM_TIME, paca_struct, accounting.stime);
	OFFSET(PACA_TRAP_SAVE, paca_struct, trap_save);
	OFFSET(PACA_NAPSTATELOST, paca_struct, nap_state_lost);
	OFFSET(PACA_SPRG_VDSO, paca_struct, sprg_vdso);
#else /* CONFIG_PPC64 */
#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
	OFFSET(ACCOUNT_STARTTIME, thread_info, accounting.starttime);
	OFFSET(ACCOUNT_STARTTIME_USER, thread_info, accounting.starttime_user);
	OFFSET(ACCOUNT_USER_TIME, thread_info, accounting.utime);
	OFFSET(ACCOUNT_SYSTEM_TIME, thread_info, accounting.stime);
#endif
#endif /* CONFIG_PPC64 */

	/* RTAS */
	OFFSET(RTASBASE, rtas_t, base);
	OFFSET(RTASENTRY, rtas_t, entry);

	/* Interrupt register frame */
	DEFINE(INT_FRAME_SIZE, STACK_INT_FRAME_SIZE);
	DEFINE(SWITCH_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs));
#ifdef CONFIG_PPC64
	/* Create extra stack space for SRR0 and SRR1 when calling prom/rtas. */
	DEFINE(PROM_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 16);
	DEFINE(RTAS_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 16);
#endif /* CONFIG_PPC64 */
	STACK_PT_REGS_OFFSET(GPR0, gpr[0]);
	STACK_PT_REGS_OFFSET(GPR1, gpr[1]);
	STACK_PT_REGS_OFFSET(GPR2, gpr[2]);
	STACK_PT_REGS_OFFSET(GPR3, gpr[3]);
	STACK_PT_REGS_OFFSET(GPR4, gpr[4]);
	STACK_PT_REGS_OFFSET(GPR5, gpr[5]);
	STACK_PT_REGS_OFFSET(GPR6, gpr[6]);
	STACK_PT_REGS_OFFSET(GPR7, gpr[7]);
	STACK_PT_REGS_OFFSET(GPR8, gpr[8]);
	STACK_PT_REGS_OFFSET(GPR9, gpr[9]);
	STACK_PT_REGS_OFFSET(GPR10, gpr[10]);
	STACK_PT_REGS_OFFSET(GPR11, gpr[11]);
	STACK_PT_REGS_OFFSET(GPR12, gpr[12]);
	STACK_PT_REGS_OFFSET(GPR13, gpr[13]);
#ifndef CONFIG_PPC64
	STACK_PT_REGS_OFFSET(GPR14, gpr[14]);
#endif /* CONFIG_PPC64 */
	/*
	 * Note: these symbols include _ because they overlap with special
	 * register names
	 */
	STACK_PT_REGS_OFFSET(_NIP, nip);
	STACK_PT_REGS_OFFSET(_MSR, msr);
	STACK_PT_REGS_OFFSET(_CTR, ctr);
	STACK_PT_REGS_OFFSET(_LINK, link);
	STACK_PT_REGS_OFFSET(_CCR, ccr);
	STACK_PT_REGS_OFFSET(_XER, xer);
	STACK_PT_REGS_OFFSET(_DAR, dar);
	STACK_PT_REGS_OFFSET(_DSISR, dsisr);
	STACK_PT_REGS_OFFSET(ORIG_GPR3, orig_gpr3);
	STACK_PT_REGS_OFFSET(RESULT, result);
	STACK_PT_REGS_OFFSET(_TRAP, trap);
#ifndef CONFIG_PPC64
	/*
	 * The PowerPC 400-class & Book-E processors have neither the DAR
	 * nor the DSISR SPRs. Hence, we overload them to hold the similar
	 * DEAR and ESR SPRs for such processors.  For critical interrupts
	 * we use them to hold SRR0 and SRR1.
	 */
	STACK_PT_REGS_OFFSET(_DEAR, dar);
	STACK_PT_REGS_OFFSET(_ESR, dsisr);
#else /* CONFIG_PPC64 */
	STACK_PT_REGS_OFFSET(SOFTE, softe);

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

#ifndef CONFIG_PPC64
	OFFSET(MM_PGD, mm_struct, pgd);
#endif /* ! CONFIG_PPC64 */

	/* About the CPU features table */
	OFFSET(CPU_SPEC_FEATURES, cpu_spec, cpu_features);
	OFFSET(CPU_SPEC_SETUP, cpu_spec, cpu_setup);
	OFFSET(CPU_SPEC_RESTORE, cpu_spec, cpu_restore);

	OFFSET(pbe_address, pbe, address);
	OFFSET(pbe_orig_address, pbe, orig_address);
	OFFSET(pbe_next, pbe, next);

#ifndef CONFIG_PPC64
	DEFINE(TASK_SIZE, TASK_SIZE);
	DEFINE(NUM_USER_SEGMENTS, TASK_SIZE>>28);
#endif /* ! CONFIG_PPC64 */

	/* datapage offsets for use by vdso */
	OFFSET(CFG_TB_ORIG_STAMP, vdso_data, tb_orig_stamp);
	OFFSET(CFG_TB_TICKS_PER_SEC, vdso_data, tb_ticks_per_sec);
	OFFSET(CFG_TB_TO_XS, vdso_data, tb_to_xs);
	OFFSET(CFG_TB_UPDATE_COUNT, vdso_data, tb_update_count);
	OFFSET(CFG_TZ_MINUTEWEST, vdso_data, tz_minuteswest);
	OFFSET(CFG_TZ_DSTTIME, vdso_data, tz_dsttime);
	OFFSET(CFG_SYSCALL_MAP32, vdso_data, syscall_map_32);
	OFFSET(WTOM_CLOCK_SEC, vdso_data, wtom_clock_sec);
	OFFSET(WTOM_CLOCK_NSEC, vdso_data, wtom_clock_nsec);
	OFFSET(STAMP_XTIME, vdso_data, stamp_xtime);
	OFFSET(STAMP_SEC_FRAC, vdso_data, stamp_sec_fraction);
	OFFSET(CFG_ICACHE_BLOCKSZ, vdso_data, icache_block_size);
	OFFSET(CFG_DCACHE_BLOCKSZ, vdso_data, dcache_block_size);
	OFFSET(CFG_ICACHE_LOGBLOCKSZ, vdso_data, icache_log_block_size);
	OFFSET(CFG_DCACHE_LOGBLOCKSZ, vdso_data, dcache_log_block_size);
#ifdef CONFIG_PPC64
	OFFSET(CFG_SYSCALL_MAP64, vdso_data, syscall_map_64);
	OFFSET(TVAL64_TV_SEC, timeval, tv_sec);
	OFFSET(TVAL64_TV_USEC, timeval, tv_usec);
	OFFSET(TVAL32_TV_SEC, compat_timeval, tv_sec);
	OFFSET(TVAL32_TV_USEC, compat_timeval, tv_usec);
	OFFSET(TSPC64_TV_SEC, timespec, tv_sec);
	OFFSET(TSPC64_TV_NSEC, timespec, tv_nsec);
	OFFSET(TSPC32_TV_SEC, compat_timespec, tv_sec);
	OFFSET(TSPC32_TV_NSEC, compat_timespec, tv_nsec);
#else
	OFFSET(TVAL32_TV_SEC, timeval, tv_sec);
	OFFSET(TVAL32_TV_USEC, timeval, tv_usec);
	OFFSET(TSPC32_TV_SEC, timespec, tv_sec);
	OFFSET(TSPC32_TV_NSEC, timespec, tv_nsec);
#endif
	/* timeval/timezone offsets for use by vdso */
	OFFSET(TZONE_TZ_MINWEST, timezone, tz_minuteswest);
	OFFSET(TZONE_TZ_DSTTIME, timezone, tz_dsttime);

	/* Other bits used by the vdso */
	DEFINE(CLOCK_REALTIME, CLOCK_REALTIME);
	DEFINE(CLOCK_MONOTONIC, CLOCK_MONOTONIC);
	DEFINE(CLOCK_REALTIME_COARSE, CLOCK_REALTIME_COARSE);
	DEFINE(CLOCK_MONOTONIC_COARSE, CLOCK_MONOTONIC_COARSE);
	DEFINE(NSEC_PER_SEC, NSEC_PER_SEC);
	DEFINE(CLOCK_REALTIME_RES, MONOTONIC_RES_NSEC);

#ifdef CONFIG_BUG
	DEFINE(BUG_ENTRY_SIZE, sizeof(struct bug_entry));
#endif

#ifdef CONFIG_PPC_BOOK3S_64
	DEFINE(PGD_TABLE_SIZE, (sizeof(pgd_t) << max(RADIX_PGD_INDEX_SIZE, H_PGD_INDEX_SIZE)));
#else
	DEFINE(PGD_TABLE_SIZE, PGD_TABLE_SIZE);
#endif
	DEFINE(PTE_SIZE, sizeof(pte_t));

#ifdef CONFIG_KVM
	OFFSET(VCPU_HOST_STACK, kvm_vcpu, arch.host_stack);
	OFFSET(VCPU_HOST_PID, kvm_vcpu, arch.host_pid);
	OFFSET(VCPU_GUEST_PID, kvm_vcpu, arch.pid);
	OFFSET(VCPU_GPRS, kvm_vcpu, arch.gpr);
	OFFSET(VCPU_VRSAVE, kvm_vcpu, arch.vrsave);
	OFFSET(VCPU_FPRS, kvm_vcpu, arch.fp.fpr);
#ifdef CONFIG_ALTIVEC
	OFFSET(VCPU_VRS, kvm_vcpu, arch.vr.vr);
#endif
	OFFSET(VCPU_XER, kvm_vcpu, arch.xer);
	OFFSET(VCPU_CTR, kvm_vcpu, arch.ctr);
	OFFSET(VCPU_LR, kvm_vcpu, arch.lr);
#ifdef CONFIG_PPC_BOOK3S
	OFFSET(VCPU_TAR, kvm_vcpu, arch.tar);
#endif
	OFFSET(VCPU_CR, kvm_vcpu, arch.cr);
	OFFSET(VCPU_PC, kvm_vcpu, arch.pc);
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	OFFSET(VCPU_MSR, kvm_vcpu, arch.shregs.msr);
	OFFSET(VCPU_SRR0, kvm_vcpu, arch.shregs.srr0);
	OFFSET(VCPU_SRR1, kvm_vcpu, arch.shregs.srr1);
	OFFSET(VCPU_SPRG0, kvm_vcpu, arch.shregs.sprg0);
	OFFSET(VCPU_SPRG1, kvm_vcpu, arch.shregs.sprg1);
	OFFSET(VCPU_SPRG2, kvm_vcpu, arch.shregs.sprg2);
	OFFSET(VCPU_SPRG3, kvm_vcpu, arch.shregs.sprg3);
#endif
#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	OFFSET(VCPU_TB_RMENTRY, kvm_vcpu, arch.rm_entry);
	OFFSET(VCPU_TB_RMINTR, kvm_vcpu, arch.rm_intr);
	OFFSET(VCPU_TB_RMEXIT, kvm_vcpu, arch.rm_exit);
	OFFSET(VCPU_TB_GUEST, kvm_vcpu, arch.guest_time);
	OFFSET(VCPU_TB_CEDE, kvm_vcpu, arch.cede_time);
	OFFSET(VCPU_CUR_ACTIVITY, kvm_vcpu, arch.cur_activity);
	OFFSET(VCPU_ACTIVITY_START, kvm_vcpu, arch.cur_tb_start);
	OFFSET(TAS_SEQCOUNT, kvmhv_tb_accumulator, seqcount);
	OFFSET(TAS_TOTAL, kvmhv_tb_accumulator, tb_total);
	OFFSET(TAS_MIN, kvmhv_tb_accumulator, tb_min);
	OFFSET(TAS_MAX, kvmhv_tb_accumulator, tb_max);
#endif
	OFFSET(VCPU_SHARED_SPRG3, kvm_vcpu_arch_shared, sprg3);
	OFFSET(VCPU_SHARED_SPRG4, kvm_vcpu_arch_shared, sprg4);
	OFFSET(VCPU_SHARED_SPRG5, kvm_vcpu_arch_shared, sprg5);
	OFFSET(VCPU_SHARED_SPRG6, kvm_vcpu_arch_shared, sprg6);
	OFFSET(VCPU_SHARED_SPRG7, kvm_vcpu_arch_shared, sprg7);
	OFFSET(VCPU_SHADOW_PID, kvm_vcpu, arch.shadow_pid);
	OFFSET(VCPU_SHADOW_PID1, kvm_vcpu, arch.shadow_pid1);
	OFFSET(VCPU_SHARED, kvm_vcpu, arch.shared);
	OFFSET(VCPU_SHARED_MSR, kvm_vcpu_arch_shared, msr);
	OFFSET(VCPU_SHADOW_MSR, kvm_vcpu, arch.shadow_msr);
#if defined(CONFIG_PPC_BOOK3S_64) && defined(CONFIG_KVM_BOOK3S_PR_POSSIBLE)
	OFFSET(VCPU_SHAREDBE, kvm_vcpu, arch.shared_big_endian);
#endif

	OFFSET(VCPU_SHARED_MAS0, kvm_vcpu_arch_shared, mas0);
	OFFSET(VCPU_SHARED_MAS1, kvm_vcpu_arch_shared, mas1);
	OFFSET(VCPU_SHARED_MAS2, kvm_vcpu_arch_shared, mas2);
	OFFSET(VCPU_SHARED_MAS7_3, kvm_vcpu_arch_shared, mas7_3);
	OFFSET(VCPU_SHARED_MAS4, kvm_vcpu_arch_shared, mas4);
	OFFSET(VCPU_SHARED_MAS6, kvm_vcpu_arch_shared, mas6);

	OFFSET(VCPU_KVM, kvm_vcpu, kvm);
	OFFSET(KVM_LPID, kvm, arch.lpid);

	/* book3s */
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	OFFSET(KVM_TLB_SETS, kvm, arch.tlb_sets);
	OFFSET(KVM_SDR1, kvm, arch.sdr1);
	OFFSET(KVM_HOST_LPID, kvm, arch.host_lpid);
	OFFSET(KVM_HOST_LPCR, kvm, arch.host_lpcr);
	OFFSET(KVM_HOST_SDR1, kvm, arch.host_sdr1);
	OFFSET(KVM_NEED_FLUSH, kvm, arch.need_tlb_flush.bits);
	OFFSET(KVM_ENABLED_HCALLS, kvm, arch.enabled_hcalls);
	OFFSET(KVM_VRMA_SLB_V, kvm, arch.vrma_slb_v);
	OFFSET(KVM_RADIX, kvm, arch.radix);
	OFFSET(KVM_FWNMI, kvm, arch.fwnmi_enabled);
	OFFSET(VCPU_DSISR, kvm_vcpu, arch.shregs.dsisr);
	OFFSET(VCPU_DAR, kvm_vcpu, arch.shregs.dar);
	OFFSET(VCPU_VPA, kvm_vcpu, arch.vpa.pinned_addr);
	OFFSET(VCPU_VPA_DIRTY, kvm_vcpu, arch.vpa.dirty);
	OFFSET(VCPU_HEIR, kvm_vcpu, arch.emul_inst);
	OFFSET(VCPU_CPU, kvm_vcpu, cpu);
	OFFSET(VCPU_THREAD_CPU, kvm_vcpu, arch.thread_cpu);
#endif
#ifdef CONFIG_PPC_BOOK3S
	OFFSET(VCPU_PURR, kvm_vcpu, arch.purr);
	OFFSET(VCPU_SPURR, kvm_vcpu, arch.spurr);
	OFFSET(VCPU_IC, kvm_vcpu, arch.ic);
	OFFSET(VCPU_DSCR, kvm_vcpu, arch.dscr);
	OFFSET(VCPU_AMR, kvm_vcpu, arch.amr);
	OFFSET(VCPU_UAMOR, kvm_vcpu, arch.uamor);
	OFFSET(VCPU_IAMR, kvm_vcpu, arch.iamr);
	OFFSET(VCPU_CTRL, kvm_vcpu, arch.ctrl);
	OFFSET(VCPU_DABR, kvm_vcpu, arch.dabr);
	OFFSET(VCPU_DABRX, kvm_vcpu, arch.dabrx);
	OFFSET(VCPU_DAWR, kvm_vcpu, arch.dawr);
	OFFSET(VCPU_DAWRX, kvm_vcpu, arch.dawrx);
	OFFSET(VCPU_CIABR, kvm_vcpu, arch.ciabr);
	OFFSET(VCPU_HFLAGS, kvm_vcpu, arch.hflags);
	OFFSET(VCPU_DEC, kvm_vcpu, arch.dec);
	OFFSET(VCPU_DEC_EXPIRES, kvm_vcpu, arch.dec_expires);
	OFFSET(VCPU_PENDING_EXC, kvm_vcpu, arch.pending_exceptions);
	OFFSET(VCPU_CEDED, kvm_vcpu, arch.ceded);
	OFFSET(VCPU_PRODDED, kvm_vcpu, arch.prodded);
	OFFSET(VCPU_IRQ_PENDING, kvm_vcpu, arch.irq_pending);
	OFFSET(VCPU_DBELL_REQ, kvm_vcpu, arch.doorbell_request);
	OFFSET(VCPU_MMCR, kvm_vcpu, arch.mmcr);
	OFFSET(VCPU_PMC, kvm_vcpu, arch.pmc);
	OFFSET(VCPU_SPMC, kvm_vcpu, arch.spmc);
	OFFSET(VCPU_SIAR, kvm_vcpu, arch.siar);
	OFFSET(VCPU_SDAR, kvm_vcpu, arch.sdar);
	OFFSET(VCPU_SIER, kvm_vcpu, arch.sier);
	OFFSET(VCPU_SLB, kvm_vcpu, arch.slb);
	OFFSET(VCPU_SLB_MAX, kvm_vcpu, arch.slb_max);
	OFFSET(VCPU_SLB_NR, kvm_vcpu, arch.slb_nr);
	OFFSET(VCPU_FAULT_DSISR, kvm_vcpu, arch.fault_dsisr);
	OFFSET(VCPU_FAULT_DAR, kvm_vcpu, arch.fault_dar);
	OFFSET(VCPU_FAULT_GPA, kvm_vcpu, arch.fault_gpa);
	OFFSET(VCPU_INTR_MSR, kvm_vcpu, arch.intr_msr);
	OFFSET(VCPU_LAST_INST, kvm_vcpu, arch.last_inst);
	OFFSET(VCPU_TRAP, kvm_vcpu, arch.trap);
	OFFSET(VCPU_CFAR, kvm_vcpu, arch.cfar);
	OFFSET(VCPU_PPR, kvm_vcpu, arch.ppr);
	OFFSET(VCPU_FSCR, kvm_vcpu, arch.fscr);
	OFFSET(VCPU_PSPB, kvm_vcpu, arch.pspb);
	OFFSET(VCPU_EBBHR, kvm_vcpu, arch.ebbhr);
	OFFSET(VCPU_EBBRR, kvm_vcpu, arch.ebbrr);
	OFFSET(VCPU_BESCR, kvm_vcpu, arch.bescr);
	OFFSET(VCPU_CSIGR, kvm_vcpu, arch.csigr);
	OFFSET(VCPU_TACR, kvm_vcpu, arch.tacr);
	OFFSET(VCPU_TCSCR, kvm_vcpu, arch.tcscr);
	OFFSET(VCPU_ACOP, kvm_vcpu, arch.acop);
	OFFSET(VCPU_WORT, kvm_vcpu, arch.wort);
	OFFSET(VCPU_TID, kvm_vcpu, arch.tid);
	OFFSET(VCPU_PSSCR, kvm_vcpu, arch.psscr);
	OFFSET(VCPU_HFSCR, kvm_vcpu, arch.hfscr);
	OFFSET(VCORE_ENTRY_EXIT, kvmppc_vcore, entry_exit_map);
	OFFSET(VCORE_IN_GUEST, kvmppc_vcore, in_guest);
	OFFSET(VCORE_NAPPING_THREADS, kvmppc_vcore, napping_threads);
	OFFSET(VCORE_KVM, kvmppc_vcore, kvm);
	OFFSET(VCORE_TB_OFFSET, kvmppc_vcore, tb_offset);
	OFFSET(VCORE_TB_OFFSET_APPL, kvmppc_vcore, tb_offset_applied);
	OFFSET(VCORE_LPCR, kvmppc_vcore, lpcr);
	OFFSET(VCORE_PCR, kvmppc_vcore, pcr);
	OFFSET(VCORE_DPDES, kvmppc_vcore, dpdes);
	OFFSET(VCORE_VTB, kvmppc_vcore, vtb);
	OFFSET(VCPU_SLB_E, kvmppc_slb, orige);
	OFFSET(VCPU_SLB_V, kvmppc_slb, origv);
	DEFINE(VCPU_SLB_SIZE, sizeof(struct kvmppc_slb));
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	OFFSET(VCPU_TFHAR, kvm_vcpu, arch.tfhar);
	OFFSET(VCPU_TFIAR, kvm_vcpu, arch.tfiar);
	OFFSET(VCPU_TEXASR, kvm_vcpu, arch.texasr);
	OFFSET(VCPU_ORIG_TEXASR, kvm_vcpu, arch.orig_texasr);
	OFFSET(VCPU_GPR_TM, kvm_vcpu, arch.gpr_tm);
	OFFSET(VCPU_FPRS_TM, kvm_vcpu, arch.fp_tm.fpr);
	OFFSET(VCPU_VRS_TM, kvm_vcpu, arch.vr_tm.vr);
	OFFSET(VCPU_VRSAVE_TM, kvm_vcpu, arch.vrsave_tm);
	OFFSET(VCPU_CR_TM, kvm_vcpu, arch.cr_tm);
	OFFSET(VCPU_XER_TM, kvm_vcpu, arch.xer_tm);
	OFFSET(VCPU_LR_TM, kvm_vcpu, arch.lr_tm);
	OFFSET(VCPU_CTR_TM, kvm_vcpu, arch.ctr_tm);
	OFFSET(VCPU_AMR_TM, kvm_vcpu, arch.amr_tm);
	OFFSET(VCPU_PPR_TM, kvm_vcpu, arch.ppr_tm);
	OFFSET(VCPU_DSCR_TM, kvm_vcpu, arch.dscr_tm);
	OFFSET(VCPU_TAR_TM, kvm_vcpu, arch.tar_tm);
#endif

#ifdef CONFIG_PPC_BOOK3S_64
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	OFFSET(PACA_SVCPU, paca_struct, shadow_vcpu);
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
	SVCPU_FIELD(SVCPU_SHADOW_FSCR, shadow_fscr);
#endif

	HSTATE_FIELD(HSTATE_HOST_R1, host_r1);
	HSTATE_FIELD(HSTATE_HOST_R2, host_r2);
	HSTATE_FIELD(HSTATE_HOST_MSR, host_msr);
	HSTATE_FIELD(HSTATE_VMHANDLER, vmhandler);
	HSTATE_FIELD(HSTATE_SCRATCH0, scratch0);
	HSTATE_FIELD(HSTATE_SCRATCH1, scratch1);
	HSTATE_FIELD(HSTATE_SCRATCH2, scratch2);
	HSTATE_FIELD(HSTATE_IN_GUEST, in_guest);
	HSTATE_FIELD(HSTATE_RESTORE_HID5, restore_hid5);
	HSTATE_FIELD(HSTATE_NAPPING, napping);

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	HSTATE_FIELD(HSTATE_HWTHREAD_REQ, hwthread_req);
	HSTATE_FIELD(HSTATE_HWTHREAD_STATE, hwthread_state);
	HSTATE_FIELD(HSTATE_KVM_VCPU, kvm_vcpu);
	HSTATE_FIELD(HSTATE_KVM_VCORE, kvm_vcore);
	HSTATE_FIELD(HSTATE_XICS_PHYS, xics_phys);
	HSTATE_FIELD(HSTATE_XIVE_TIMA_PHYS, xive_tima_phys);
	HSTATE_FIELD(HSTATE_XIVE_TIMA_VIRT, xive_tima_virt);
	HSTATE_FIELD(HSTATE_SAVED_XIRR, saved_xirr);
	HSTATE_FIELD(HSTATE_HOST_IPI, host_ipi);
	HSTATE_FIELD(HSTATE_PTID, ptid);
	HSTATE_FIELD(HSTATE_TID, tid);
	HSTATE_FIELD(HSTATE_FAKE_SUSPEND, fake_suspend);
	HSTATE_FIELD(HSTATE_MMCR0, host_mmcr[0]);
	HSTATE_FIELD(HSTATE_MMCR1, host_mmcr[1]);
	HSTATE_FIELD(HSTATE_MMCRA, host_mmcr[2]);
	HSTATE_FIELD(HSTATE_SIAR, host_mmcr[3]);
	HSTATE_FIELD(HSTATE_SDAR, host_mmcr[4]);
	HSTATE_FIELD(HSTATE_MMCR2, host_mmcr[5]);
	HSTATE_FIELD(HSTATE_SIER, host_mmcr[6]);
	HSTATE_FIELD(HSTATE_PMC1, host_pmc[0]);
	HSTATE_FIELD(HSTATE_PMC2, host_pmc[1]);
	HSTATE_FIELD(HSTATE_PMC3, host_pmc[2]);
	HSTATE_FIELD(HSTATE_PMC4, host_pmc[3]);
	HSTATE_FIELD(HSTATE_PMC5, host_pmc[4]);
	HSTATE_FIELD(HSTATE_PMC6, host_pmc[5]);
	HSTATE_FIELD(HSTATE_PURR, host_purr);
	HSTATE_FIELD(HSTATE_SPURR, host_spurr);
	HSTATE_FIELD(HSTATE_DSCR, host_dscr);
	HSTATE_FIELD(HSTATE_DABR, dabr);
	HSTATE_FIELD(HSTATE_DECEXP, dec_expires);
	HSTATE_FIELD(HSTATE_SPLIT_MODE, kvm_split_mode);
	DEFINE(IPI_PRIORITY, IPI_PRIORITY);
	OFFSET(KVM_SPLIT_RPR, kvm_split_mode, rpr);
	OFFSET(KVM_SPLIT_PMMAR, kvm_split_mode, pmmar);
	OFFSET(KVM_SPLIT_LDBAR, kvm_split_mode, ldbar);
	OFFSET(KVM_SPLIT_DO_NAP, kvm_split_mode, do_nap);
	OFFSET(KVM_SPLIT_NAPPED, kvm_split_mode, napped);
	OFFSET(KVM_SPLIT_DO_SET, kvm_split_mode, do_set);
	OFFSET(KVM_SPLIT_DO_RESTORE, kvm_split_mode, do_restore);
#endif /* CONFIG_KVM_BOOK3S_HV_POSSIBLE */

#ifdef CONFIG_PPC_BOOK3S_64
	HSTATE_FIELD(HSTATE_CFAR, cfar);
	HSTATE_FIELD(HSTATE_PPR, ppr);
	HSTATE_FIELD(HSTATE_HOST_FSCR, host_fscr);
#endif /* CONFIG_PPC_BOOK3S_64 */

#else /* CONFIG_PPC_BOOK3S */
	OFFSET(VCPU_CR, kvm_vcpu, arch.cr);
	OFFSET(VCPU_XER, kvm_vcpu, arch.xer);
	OFFSET(VCPU_LR, kvm_vcpu, arch.lr);
	OFFSET(VCPU_CTR, kvm_vcpu, arch.ctr);
	OFFSET(VCPU_PC, kvm_vcpu, arch.pc);
	OFFSET(VCPU_SPRG9, kvm_vcpu, arch.sprg9);
	OFFSET(VCPU_LAST_INST, kvm_vcpu, arch.last_inst);
	OFFSET(VCPU_FAULT_DEAR, kvm_vcpu, arch.fault_dear);
	OFFSET(VCPU_FAULT_ESR, kvm_vcpu, arch.fault_esr);
	OFFSET(VCPU_CRIT_SAVE, kvm_vcpu, arch.crit_save);
#endif /* CONFIG_PPC_BOOK3S */
#endif /* CONFIG_KVM */

#ifdef CONFIG_KVM_GUEST
	OFFSET(KVM_MAGIC_SCRATCH1, kvm_vcpu_arch_shared, scratch1);
	OFFSET(KVM_MAGIC_SCRATCH2, kvm_vcpu_arch_shared, scratch2);
	OFFSET(KVM_MAGIC_SCRATCH3, kvm_vcpu_arch_shared, scratch3);
	OFFSET(KVM_MAGIC_INT, kvm_vcpu_arch_shared, int_pending);
	OFFSET(KVM_MAGIC_MSR, kvm_vcpu_arch_shared, msr);
	OFFSET(KVM_MAGIC_CRITICAL, kvm_vcpu_arch_shared, critical);
	OFFSET(KVM_MAGIC_SR, kvm_vcpu_arch_shared, sr);
#endif

#ifdef CONFIG_44x
	DEFINE(PGD_T_LOG2, PGD_T_LOG2);
	DEFINE(PTE_T_LOG2, PTE_T_LOG2);
#endif
#ifdef CONFIG_PPC_FSL_BOOK3E
	DEFINE(TLBCAM_SIZE, sizeof(struct tlbcam));
	OFFSET(TLBCAM_MAS0, tlbcam, MAS0);
	OFFSET(TLBCAM_MAS1, tlbcam, MAS1);
	OFFSET(TLBCAM_MAS2, tlbcam, MAS2);
	OFFSET(TLBCAM_MAS3, tlbcam, MAS3);
	OFFSET(TLBCAM_MAS7, tlbcam, MAS7);
#endif

#if defined(CONFIG_KVM) && defined(CONFIG_SPE)
	OFFSET(VCPU_EVR, kvm_vcpu, arch.evr[0]);
	OFFSET(VCPU_ACC, kvm_vcpu, arch.acc);
	OFFSET(VCPU_SPEFSCR, kvm_vcpu, arch.spefscr);
	OFFSET(VCPU_HOST_SPEFSCR, kvm_vcpu, arch.host_spefscr);
#endif

#ifdef CONFIG_KVM_BOOKE_HV
	OFFSET(VCPU_HOST_MAS4, kvm_vcpu, arch.host_mas4);
	OFFSET(VCPU_HOST_MAS6, kvm_vcpu, arch.host_mas6);
#endif

#ifdef CONFIG_KVM_XICS
	DEFINE(VCPU_XIVE_SAVED_STATE, offsetof(struct kvm_vcpu,
					       arch.xive_saved_state));
	DEFINE(VCPU_XIVE_CAM_WORD, offsetof(struct kvm_vcpu,
					    arch.xive_cam_word));
	DEFINE(VCPU_XIVE_PUSHED, offsetof(struct kvm_vcpu, arch.xive_pushed));
	DEFINE(VCPU_XIVE_ESC_ON, offsetof(struct kvm_vcpu, arch.xive_esc_on));
	DEFINE(VCPU_XIVE_ESC_RADDR, offsetof(struct kvm_vcpu, arch.xive_esc_raddr));
	DEFINE(VCPU_XIVE_ESC_VADDR, offsetof(struct kvm_vcpu, arch.xive_esc_vaddr));
#endif

#ifdef CONFIG_KVM_EXIT_TIMING
	OFFSET(VCPU_TIMING_EXIT_TBU, kvm_vcpu, arch.timing_exit.tv32.tbu);
	OFFSET(VCPU_TIMING_EXIT_TBL, kvm_vcpu, arch.timing_exit.tv32.tbl);
	OFFSET(VCPU_TIMING_LAST_ENTER_TBU, kvm_vcpu, arch.timing_last_enter.tv32.tbu);
	OFFSET(VCPU_TIMING_LAST_ENTER_TBL, kvm_vcpu, arch.timing_last_enter.tv32.tbl);
#endif

#ifdef CONFIG_PPC_POWERNV
	OFFSET(PACA_CORE_IDLE_STATE_PTR, paca_struct, core_idle_state_ptr);
	OFFSET(PACA_THREAD_IDLE_STATE, paca_struct, thread_idle_state);
	OFFSET(PACA_THREAD_MASK, paca_struct, thread_mask);
	OFFSET(PACA_SUBCORE_SIBLING_MASK, paca_struct, subcore_sibling_mask);
	OFFSET(PACA_SIBLING_PACA_PTRS, paca_struct, thread_sibling_pacas);
	OFFSET(PACA_REQ_PSSCR, paca_struct, requested_psscr);
	OFFSET(PACA_DONT_STOP, paca_struct, dont_stop);
#define STOP_SPR(x, f)	OFFSET(x, paca_struct, stop_sprs.f)
	STOP_SPR(STOP_PID, pid);
	STOP_SPR(STOP_LDBAR, ldbar);
	STOP_SPR(STOP_FSCR, fscr);
	STOP_SPR(STOP_HFSCR, hfscr);
	STOP_SPR(STOP_MMCR1, mmcr1);
	STOP_SPR(STOP_MMCR2, mmcr2);
	STOP_SPR(STOP_MMCRA, mmcra);
#endif

	DEFINE(PPC_DBELL_SERVER, PPC_DBELL_SERVER);
	DEFINE(PPC_DBELL_MSGTYPE, PPC_DBELL_MSGTYPE);

#ifdef CONFIG_PPC_8xx
	DEFINE(VIRT_IMMR_BASE, (u64)__fix_to_virt(FIX_IMMR_BASE));
#endif

	return 0;
}
