// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#include <linux/compat.h>
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
#ifdef CONFIG_BOOKE_OR_40x
#include "head_booke.h"
#endif
#endif

#if defined(CONFIG_PPC_E500)
#include "../mm/mmu_decl.h"
#endif

#ifdef CONFIG_PPC_8xx
#include <asm/fixmap.h>
#endif

#ifdef CONFIG_XMON
#include "../xmon/xmon_bpts.h"
#endif

#define STACK_PT_REGS_OFFSET(sym, val)	\
	DEFINE(sym, STACK_INT_FRAME_REGS + offsetof(struct pt_regs, val))

int main(void)
{
	OFFSET(THREAD, task_struct, thread);
	OFFSET(MM, task_struct, mm);
#ifdef CONFIG_STACKPROTECTOR
	OFFSET(TASK_CANARY, task_struct, stack_canary);
#ifdef CONFIG_PPC64
	OFFSET(PACA_CANARY, paca_struct, canary);
#endif
#endif
#ifdef CONFIG_PPC32
#ifdef CONFIG_PPC_RTAS
	OFFSET(RTAS_SP, thread_struct, rtas_sp);
#endif
#endif /* CONFIG_PPC64 */
	OFFSET(TASK_STACK, task_struct, stack);
#ifdef CONFIG_SMP
	OFFSET(TASK_CPU, task_struct, thread_info.cpu);
#endif

#ifdef CONFIG_LIVEPATCH_64
	OFFSET(TI_livepatch_sp, thread_info, livepatch_sp);
#endif

	OFFSET(KSP, thread_struct, ksp);
	OFFSET(PT_REGS, thread_struct, regs);
#ifdef CONFIG_BOOKE
	OFFSET(THREAD_NORMSAVES, thread_struct, normsave[0]);
#endif
#ifdef CONFIG_PPC_FPU
	OFFSET(THREAD_FPEXC_MODE, thread_struct, fpexc_mode);
	OFFSET(THREAD_FPSTATE, thread_struct, fp_state.fpr);
	OFFSET(THREAD_FPSAVEAREA, thread_struct, fp_save_area);
#endif
	OFFSET(FPSTATE_FPSCR, thread_fp_state, fpscr);
	OFFSET(THREAD_LOAD_FP, thread_struct, load_fp);
#ifdef CONFIG_ALTIVEC
	OFFSET(THREAD_VRSTATE, thread_struct, vr_state.vr);
	OFFSET(THREAD_VRSAVEAREA, thread_struct, vr_save_area);
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
	OFFSET(SRR0, thread_struct, srr0);
	OFFSET(SRR1, thread_struct, srr1);
	OFFSET(DAR, thread_struct, dar);
	OFFSET(DSISR, thread_struct, dsisr);
#ifdef CONFIG_PPC_BOOK3S_32
	OFFSET(THR0, thread_struct, r0);
	OFFSET(THR3, thread_struct, r3);
	OFFSET(THR4, thread_struct, r4);
	OFFSET(THR5, thread_struct, r5);
	OFFSET(THR6, thread_struct, r6);
	OFFSET(THR8, thread_struct, r8);
	OFFSET(THR9, thread_struct, r9);
	OFFSET(THR11, thread_struct, r11);
	OFFSET(THLR, thread_struct, lr);
	OFFSET(THCTR, thread_struct, ctr);
	OFFSET(THSR0, thread_struct, sr0);
#endif
#ifdef CONFIG_SPE
	OFFSET(THREAD_EVR0, thread_struct, evr[0]);
	OFFSET(THREAD_ACC, thread_struct, acc);
	OFFSET(THREAD_USED_SPE, thread_struct, used_spe);
#endif /* CONFIG_SPE */
#endif /* CONFIG_PPC64 */
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
	OFFSET(THREAD_TM_AMR, thread_struct, tm_amr);
	OFFSET(PT_CKPT_REGS, thread_struct, ckpt_regs);
	OFFSET(THREAD_CKVRSTATE, thread_struct, ckvr_state.vr);
	OFFSET(THREAD_CKVRSAVE, thread_struct, ckvrsave);
	OFFSET(THREAD_CKFPSTATE, thread_struct, ckfp_state.fpr);
	/* Local pt_regs on stack in int frame form, plus 16 bytes for TM */
	DEFINE(TM_FRAME_SIZE, STACK_INT_FRAME_SIZE + 16);
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */

	OFFSET(TI_LOCAL_FLAGS, thread_info, local_flags);

#ifdef CONFIG_PPC64
	OFFSET(DCACHEL1BLOCKSIZE, ppc64_caches, l1d.block_size);
	OFFSET(DCACHEL1LOGBLOCKSIZE, ppc64_caches, l1d.log_block_size);
	/* paca */
	OFFSET(PACAPACAINDEX, paca_struct, paca_index);
	OFFSET(PACAPROCSTART, paca_struct, cpu_start);
	OFFSET(PACAKSAVE, paca_struct, kstack);
	OFFSET(PACACURRENT, paca_struct, __current);
	DEFINE(PACA_THREAD_INFO, offsetof(struct paca_struct, __current) +
				 offsetof(struct task_struct, thread_info));
	OFFSET(PACASAVEDMSR, paca_struct, saved_msr);
	OFFSET(PACAR1, paca_struct, saved_r1);
#ifndef CONFIG_PPC_KERNEL_PCREL
	OFFSET(PACATOC, paca_struct, kernel_toc);
#endif
	OFFSET(PACAKBASE, paca_struct, kernelbase);
	OFFSET(PACAKMSR, paca_struct, kernel_msr);
#ifdef CONFIG_PPC_BOOK3S_64
	OFFSET(PACAHSRR_VALID, paca_struct, hsrr_valid);
	OFFSET(PACASRR_VALID, paca_struct, srr_valid);
#endif
	OFFSET(PACAIRQSOFTMASK, paca_struct, irq_soft_mask);
	OFFSET(PACAIRQHAPPENED, paca_struct, irq_happened);
	OFFSET(PACA_FTRACE_ENABLED, paca_struct, ftrace_enabled);

#ifdef CONFIG_PPC_BOOK3E_64
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
#endif /* CONFIG_PPC_BOOK3E_64 */

#ifdef CONFIG_PPC_BOOK3S_64
	OFFSET(PACA_EXGEN, paca_struct, exgen);
	OFFSET(PACA_EXMC, paca_struct, exmc);
	OFFSET(PACA_EXNMI, paca_struct, exnmi);
#ifdef CONFIG_PPC_64S_HASH_MMU
	OFFSET(PACA_SLBSHADOWPTR, paca_struct, slb_shadow_ptr);
	OFFSET(SLBSHADOW_STACKVSID, slb_shadow, save_area[SLB_NUM_BOLTED - 1].vsid);
	OFFSET(SLBSHADOW_STACKESID, slb_shadow, save_area[SLB_NUM_BOLTED - 1].esid);
	OFFSET(SLBSHADOW_SAVEAREA, slb_shadow, save_area);
#endif
	OFFSET(LPPACA_PMCINUSE, lppaca, pmcregs_in_use);
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	OFFSET(PACA_PMCINUSE, paca_struct, pmcregs_in_use);
#endif
	OFFSET(LPPACA_YIELDCOUNT, lppaca, yield_count);
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
#ifdef CONFIG_PPC64
	OFFSET(PACA_EXIT_SAVE_R1, paca_struct, exit_save_r1);
#endif
#ifdef CONFIG_PPC_BOOK3E_64
	OFFSET(PACA_TRAP_SAVE, paca_struct, trap_save);
#endif
	OFFSET(PACA_SPRG_VDSO, paca_struct, sprg_vdso);
#else /* CONFIG_PPC64 */
#endif /* CONFIG_PPC64 */

	/* RTAS */
	OFFSET(RTASBASE, rtas_t, base);
	OFFSET(RTASENTRY, rtas_t, entry);

	/* Interrupt register frame */
	DEFINE(INT_FRAME_SIZE, STACK_INT_FRAME_SIZE);
	DEFINE(SWITCH_FRAME_SIZE, STACK_SWITCH_FRAME_SIZE);
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
	STACK_PT_REGS_OFFSET(_DEAR, dear);
	STACK_PT_REGS_OFFSET(_DSISR, dsisr);
	STACK_PT_REGS_OFFSET(_ESR, esr);
	STACK_PT_REGS_OFFSET(ORIG_GPR3, orig_gpr3);
	STACK_PT_REGS_OFFSET(RESULT, result);
	STACK_PT_REGS_OFFSET(_TRAP, trap);
#ifdef CONFIG_PPC64
	STACK_PT_REGS_OFFSET(SOFTE, softe);
	STACK_PT_REGS_OFFSET(_PPR, ppr);
#endif

#ifdef CONFIG_PPC_PKEY
	STACK_PT_REGS_OFFSET(STACK_REGS_AMR, amr);
	STACK_PT_REGS_OFFSET(STACK_REGS_IAMR, iamr);
#endif

#if defined(CONFIG_PPC32) && defined(CONFIG_BOOKE)
	STACK_PT_REGS_OFFSET(MAS0, mas0);
	/* we overload MMUCR for 44x on MAS0 since they are mutually exclusive */
	STACK_PT_REGS_OFFSET(MMUCR, mas0);
	STACK_PT_REGS_OFFSET(MAS1, mas1);
	STACK_PT_REGS_OFFSET(MAS2, mas2);
	STACK_PT_REGS_OFFSET(MAS3, mas3);
	STACK_PT_REGS_OFFSET(MAS6, mas6);
	STACK_PT_REGS_OFFSET(MAS7, mas7);
	STACK_PT_REGS_OFFSET(_SRR0, srr0);
	STACK_PT_REGS_OFFSET(_SRR1, srr1);
	STACK_PT_REGS_OFFSET(_CSRR0, csrr0);
	STACK_PT_REGS_OFFSET(_CSRR1, csrr1);
	STACK_PT_REGS_OFFSET(_DSRR0, dsrr0);
	STACK_PT_REGS_OFFSET(_DSRR1, dsrr1);
#endif

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
	OFFSET(VDSO_DATA_OFFSET, vdso_arch_data, data);
	OFFSET(CFG_TB_TICKS_PER_SEC, vdso_arch_data, tb_ticks_per_sec);
#ifdef CONFIG_PPC64
	OFFSET(CFG_ICACHE_BLOCKSZ, vdso_arch_data, icache_block_size);
	OFFSET(CFG_DCACHE_BLOCKSZ, vdso_arch_data, dcache_block_size);
	OFFSET(CFG_ICACHE_LOGBLOCKSZ, vdso_arch_data, icache_log_block_size);
	OFFSET(CFG_DCACHE_LOGBLOCKSZ, vdso_arch_data, dcache_log_block_size);
	OFFSET(CFG_SYSCALL_MAP64, vdso_arch_data, syscall_map);
	OFFSET(CFG_SYSCALL_MAP32, vdso_arch_data, compat_syscall_map);
#else
	OFFSET(CFG_SYSCALL_MAP32, vdso_arch_data, syscall_map);
#endif

#ifdef CONFIG_BUG
	DEFINE(BUG_ENTRY_SIZE, sizeof(struct bug_entry));
#endif

#ifdef CONFIG_KVM
	OFFSET(VCPU_HOST_STACK, kvm_vcpu, arch.host_stack);
	OFFSET(VCPU_HOST_PID, kvm_vcpu, arch.host_pid);
	OFFSET(VCPU_GUEST_PID, kvm_vcpu, arch.pid);
	OFFSET(VCPU_GPRS, kvm_vcpu, arch.regs.gpr);
	OFFSET(VCPU_VRSAVE, kvm_vcpu, arch.vrsave);
	OFFSET(VCPU_FPRS, kvm_vcpu, arch.fp.fpr);
#ifdef CONFIG_ALTIVEC
	OFFSET(VCPU_VRS, kvm_vcpu, arch.vr.vr);
#endif
	OFFSET(VCPU_XER, kvm_vcpu, arch.regs.xer);
	OFFSET(VCPU_CTR, kvm_vcpu, arch.regs.ctr);
	OFFSET(VCPU_LR, kvm_vcpu, arch.regs.link);
#ifdef CONFIG_PPC_BOOK3S
	OFFSET(VCPU_TAR, kvm_vcpu, arch.tar);
#endif
	OFFSET(VCPU_CR, kvm_vcpu, arch.regs.ccr);
	OFFSET(VCPU_PC, kvm_vcpu, arch.regs.nip);
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	OFFSET(VCPU_MSR, kvm_vcpu, arch.shregs.msr);
	OFFSET(VCPU_SRR0, kvm_vcpu, arch.shregs.srr0);
	OFFSET(VCPU_SRR1, kvm_vcpu, arch.shregs.srr1);
	OFFSET(VCPU_SPRG0, kvm_vcpu, arch.shregs.sprg0);
	OFFSET(VCPU_SPRG1, kvm_vcpu, arch.shregs.sprg1);
	OFFSET(VCPU_SPRG2, kvm_vcpu, arch.shregs.sprg2);
	OFFSET(VCPU_SPRG3, kvm_vcpu, arch.shregs.sprg3);
#endif
#ifdef CONFIG_KVM_BOOK3S_HV_P8_TIMING
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
	OFFSET(KVM_SDR1, kvm, arch.sdr1);
	OFFSET(KVM_HOST_LPID, kvm, arch.host_lpid);
	OFFSET(KVM_HOST_LPCR, kvm, arch.host_lpcr);
	OFFSET(KVM_HOST_SDR1, kvm, arch.host_sdr1);
	OFFSET(KVM_ENABLED_HCALLS, kvm, arch.enabled_hcalls);
	OFFSET(KVM_VRMA_SLB_V, kvm, arch.vrma_slb_v);
	OFFSET(KVM_SECURE_GUEST, kvm, arch.secure_guest);
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
	OFFSET(VCPU_DAWR0, kvm_vcpu, arch.dawr0);
	OFFSET(VCPU_DAWRX0, kvm_vcpu, arch.dawrx0);
	OFFSET(VCPU_CIABR, kvm_vcpu, arch.ciabr);
	OFFSET(VCPU_HFLAGS, kvm_vcpu, arch.hflags);
	OFFSET(VCPU_DEC_EXPIRES, kvm_vcpu, arch.dec_expires);
	OFFSET(VCPU_PENDING_EXC, kvm_vcpu, arch.pending_exceptions);
	OFFSET(VCPU_CEDED, kvm_vcpu, arch.ceded);
	OFFSET(VCPU_PRODDED, kvm_vcpu, arch.prodded);
	OFFSET(VCPU_MMCR, kvm_vcpu, arch.mmcr);
	OFFSET(VCPU_MMCRA, kvm_vcpu, arch.mmcra);
	OFFSET(VCPU_MMCRS, kvm_vcpu, arch.mmcrs);
	OFFSET(VCPU_PMC, kvm_vcpu, arch.pmc);
	OFFSET(VCPU_SIAR, kvm_vcpu, arch.siar);
	OFFSET(VCPU_SDAR, kvm_vcpu, arch.sdar);
	OFFSET(VCPU_SIER, kvm_vcpu, arch.sier);
	OFFSET(VCPU_SLB, kvm_vcpu, arch.slb);
	OFFSET(VCPU_SLB_MAX, kvm_vcpu, arch.slb_max);
	OFFSET(VCPU_SLB_NR, kvm_vcpu, arch.slb_nr);
	OFFSET(VCPU_FAULT_DSISR, kvm_vcpu, arch.fault_dsisr);
	OFFSET(VCPU_FAULT_DAR, kvm_vcpu, arch.fault_dar);
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
	HSTATE_FIELD(HSTATE_HOST_IPI, host_ipi);
	HSTATE_FIELD(HSTATE_PTID, ptid);
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
#endif /* CONFIG_KVM_BOOK3S_HV_POSSIBLE */

#ifdef CONFIG_PPC_BOOK3S_64
	HSTATE_FIELD(HSTATE_CFAR, cfar);
	HSTATE_FIELD(HSTATE_PPR, ppr);
	HSTATE_FIELD(HSTATE_HOST_FSCR, host_fscr);
#endif /* CONFIG_PPC_BOOK3S_64 */

#else /* CONFIG_PPC_BOOK3S */
	OFFSET(VCPU_CR, kvm_vcpu, arch.regs.ccr);
	OFFSET(VCPU_XER, kvm_vcpu, arch.regs.xer);
	OFFSET(VCPU_LR, kvm_vcpu, arch.regs.link);
	OFFSET(VCPU_CTR, kvm_vcpu, arch.regs.ctr);
	OFFSET(VCPU_PC, kvm_vcpu, arch.regs.nip);
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
#ifdef CONFIG_PPC_E500
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

#ifdef CONFIG_KVM_EXIT_TIMING
	OFFSET(VCPU_TIMING_EXIT_TBU, kvm_vcpu, arch.timing_exit.tv32.tbu);
	OFFSET(VCPU_TIMING_EXIT_TBL, kvm_vcpu, arch.timing_exit.tv32.tbl);
	OFFSET(VCPU_TIMING_LAST_ENTER_TBU, kvm_vcpu, arch.timing_last_enter.tv32.tbu);
	OFFSET(VCPU_TIMING_LAST_ENTER_TBL, kvm_vcpu, arch.timing_last_enter.tv32.tbl);
#endif

	DEFINE(PPC_DBELL_SERVER, PPC_DBELL_SERVER);

#ifdef CONFIG_PPC_8xx
	DEFINE(VIRT_IMMR_BASE, (u64)__fix_to_virt(FIX_IMMR_BASE));
#endif

#ifdef CONFIG_XMON
	DEFINE(BPT_SIZE, BPT_SIZE);
#endif

	return 0;
}
