/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#define ASM_OFFSETS_C 1

#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/clocksource.h>
#include <linux/kbuild.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/siginfo.h>
#include <asm/sigcontext.h>
#include <asm/mca.h>

#include "../kernel/sigframe.h"
#include "../kernel/fsyscall_gtod_data.h"

void foo(void)
{
	DEFINE(IA64_TASK_SIZE, sizeof (struct task_struct));
	DEFINE(IA64_THREAD_INFO_SIZE, sizeof (struct thread_info));
	DEFINE(IA64_PT_REGS_SIZE, sizeof (struct pt_regs));
	DEFINE(IA64_SWITCH_STACK_SIZE, sizeof (struct switch_stack));
	DEFINE(IA64_SIGINFO_SIZE, sizeof (struct siginfo));
	DEFINE(IA64_CPU_SIZE, sizeof (struct cpuinfo_ia64));
	DEFINE(SIGFRAME_SIZE, sizeof (struct sigframe));
	DEFINE(UNW_FRAME_INFO_SIZE, sizeof (struct unw_frame_info));

	BUILD_BUG_ON(sizeof(struct upid) != 32);
	DEFINE(IA64_UPID_SHIFT, 5);

	BLANK();

	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_CPU, offsetof(struct thread_info, cpu));
	DEFINE(TI_PRE_COUNT, offsetof(struct thread_info, preempt_count));
#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
	DEFINE(TI_AC_STAMP, offsetof(struct thread_info, ac_stamp));
	DEFINE(TI_AC_LEAVE, offsetof(struct thread_info, ac_leave));
	DEFINE(TI_AC_STIME, offsetof(struct thread_info, ac_stime));
	DEFINE(TI_AC_UTIME, offsetof(struct thread_info, ac_utime));
#endif

	BLANK();

	DEFINE(IA64_TASK_BLOCKED_OFFSET,offsetof (struct task_struct, blocked));
	DEFINE(IA64_TASK_CLEAR_CHILD_TID_OFFSET,offsetof (struct task_struct, clear_child_tid));
	DEFINE(IA64_TASK_GROUP_LEADER_OFFSET, offsetof (struct task_struct, group_leader));
	DEFINE(IA64_TASK_TGIDLINK_OFFSET, offsetof (struct task_struct, pids[PIDTYPE_PID].pid));
	DEFINE(IA64_PID_LEVEL_OFFSET, offsetof (struct pid, level));
	DEFINE(IA64_PID_UPID_OFFSET, offsetof (struct pid, numbers[0]));
	DEFINE(IA64_TASK_PENDING_OFFSET,offsetof (struct task_struct, pending));
	DEFINE(IA64_TASK_PID_OFFSET, offsetof (struct task_struct, pid));
	DEFINE(IA64_TASK_REAL_PARENT_OFFSET, offsetof (struct task_struct, real_parent));
	DEFINE(IA64_TASK_SIGHAND_OFFSET,offsetof (struct task_struct, sighand));
	DEFINE(IA64_TASK_SIGNAL_OFFSET,offsetof (struct task_struct, signal));
	DEFINE(IA64_TASK_TGID_OFFSET, offsetof (struct task_struct, tgid));
	DEFINE(IA64_TASK_THREAD_KSP_OFFSET, offsetof (struct task_struct, thread.ksp));
	DEFINE(IA64_TASK_THREAD_ON_USTACK_OFFSET, offsetof (struct task_struct, thread.on_ustack));

	BLANK();

	DEFINE(IA64_SIGHAND_SIGLOCK_OFFSET,offsetof (struct sighand_struct, siglock));

	BLANK();

	DEFINE(IA64_SIGNAL_GROUP_STOP_COUNT_OFFSET,offsetof (struct signal_struct,
							     group_stop_count));
	DEFINE(IA64_SIGNAL_SHARED_PENDING_OFFSET,offsetof (struct signal_struct, shared_pending));

	BLANK();

	DEFINE(IA64_PT_REGS_B6_OFFSET, offsetof (struct pt_regs, b6));
	DEFINE(IA64_PT_REGS_B7_OFFSET, offsetof (struct pt_regs, b7));
	DEFINE(IA64_PT_REGS_AR_CSD_OFFSET, offsetof (struct pt_regs, ar_csd));
	DEFINE(IA64_PT_REGS_AR_SSD_OFFSET, offsetof (struct pt_regs, ar_ssd));
	DEFINE(IA64_PT_REGS_R8_OFFSET, offsetof (struct pt_regs, r8));
	DEFINE(IA64_PT_REGS_R9_OFFSET, offsetof (struct pt_regs, r9));
	DEFINE(IA64_PT_REGS_R10_OFFSET, offsetof (struct pt_regs, r10));
	DEFINE(IA64_PT_REGS_R11_OFFSET, offsetof (struct pt_regs, r11));
	DEFINE(IA64_PT_REGS_CR_IPSR_OFFSET, offsetof (struct pt_regs, cr_ipsr));
	DEFINE(IA64_PT_REGS_CR_IIP_OFFSET, offsetof (struct pt_regs, cr_iip));
	DEFINE(IA64_PT_REGS_CR_IFS_OFFSET, offsetof (struct pt_regs, cr_ifs));
	DEFINE(IA64_PT_REGS_AR_UNAT_OFFSET, offsetof (struct pt_regs, ar_unat));
	DEFINE(IA64_PT_REGS_AR_PFS_OFFSET, offsetof (struct pt_regs, ar_pfs));
	DEFINE(IA64_PT_REGS_AR_RSC_OFFSET, offsetof (struct pt_regs, ar_rsc));
	DEFINE(IA64_PT_REGS_AR_RNAT_OFFSET, offsetof (struct pt_regs, ar_rnat));

	DEFINE(IA64_PT_REGS_AR_BSPSTORE_OFFSET, offsetof (struct pt_regs, ar_bspstore));
	DEFINE(IA64_PT_REGS_PR_OFFSET, offsetof (struct pt_regs, pr));
	DEFINE(IA64_PT_REGS_B0_OFFSET, offsetof (struct pt_regs, b0));
	DEFINE(IA64_PT_REGS_LOADRS_OFFSET, offsetof (struct pt_regs, loadrs));
	DEFINE(IA64_PT_REGS_R1_OFFSET, offsetof (struct pt_regs, r1));
	DEFINE(IA64_PT_REGS_R12_OFFSET, offsetof (struct pt_regs, r12));
	DEFINE(IA64_PT_REGS_R13_OFFSET, offsetof (struct pt_regs, r13));
	DEFINE(IA64_PT_REGS_AR_FPSR_OFFSET, offsetof (struct pt_regs, ar_fpsr));
	DEFINE(IA64_PT_REGS_R15_OFFSET, offsetof (struct pt_regs, r15));
	DEFINE(IA64_PT_REGS_R14_OFFSET, offsetof (struct pt_regs, r14));
	DEFINE(IA64_PT_REGS_R2_OFFSET, offsetof (struct pt_regs, r2));
	DEFINE(IA64_PT_REGS_R3_OFFSET, offsetof (struct pt_regs, r3));
	DEFINE(IA64_PT_REGS_R16_OFFSET, offsetof (struct pt_regs, r16));
	DEFINE(IA64_PT_REGS_R17_OFFSET, offsetof (struct pt_regs, r17));
	DEFINE(IA64_PT_REGS_R18_OFFSET, offsetof (struct pt_regs, r18));
	DEFINE(IA64_PT_REGS_R19_OFFSET, offsetof (struct pt_regs, r19));
	DEFINE(IA64_PT_REGS_R20_OFFSET, offsetof (struct pt_regs, r20));
	DEFINE(IA64_PT_REGS_R21_OFFSET, offsetof (struct pt_regs, r21));
	DEFINE(IA64_PT_REGS_R22_OFFSET, offsetof (struct pt_regs, r22));
	DEFINE(IA64_PT_REGS_R23_OFFSET, offsetof (struct pt_regs, r23));
	DEFINE(IA64_PT_REGS_R24_OFFSET, offsetof (struct pt_regs, r24));
	DEFINE(IA64_PT_REGS_R25_OFFSET, offsetof (struct pt_regs, r25));
	DEFINE(IA64_PT_REGS_R26_OFFSET, offsetof (struct pt_regs, r26));
	DEFINE(IA64_PT_REGS_R27_OFFSET, offsetof (struct pt_regs, r27));
	DEFINE(IA64_PT_REGS_R28_OFFSET, offsetof (struct pt_regs, r28));
	DEFINE(IA64_PT_REGS_R29_OFFSET, offsetof (struct pt_regs, r29));
	DEFINE(IA64_PT_REGS_R30_OFFSET, offsetof (struct pt_regs, r30));
	DEFINE(IA64_PT_REGS_R31_OFFSET, offsetof (struct pt_regs, r31));
	DEFINE(IA64_PT_REGS_AR_CCV_OFFSET, offsetof (struct pt_regs, ar_ccv));
	DEFINE(IA64_PT_REGS_F6_OFFSET, offsetof (struct pt_regs, f6));
	DEFINE(IA64_PT_REGS_F7_OFFSET, offsetof (struct pt_regs, f7));
	DEFINE(IA64_PT_REGS_F8_OFFSET, offsetof (struct pt_regs, f8));
	DEFINE(IA64_PT_REGS_F9_OFFSET, offsetof (struct pt_regs, f9));
	DEFINE(IA64_PT_REGS_F10_OFFSET, offsetof (struct pt_regs, f10));
	DEFINE(IA64_PT_REGS_F11_OFFSET, offsetof (struct pt_regs, f11));

	BLANK();

	DEFINE(IA64_SWITCH_STACK_CALLER_UNAT_OFFSET, offsetof (struct switch_stack, caller_unat));
	DEFINE(IA64_SWITCH_STACK_AR_FPSR_OFFSET, offsetof (struct switch_stack, ar_fpsr));
	DEFINE(IA64_SWITCH_STACK_F2_OFFSET, offsetof (struct switch_stack, f2));
	DEFINE(IA64_SWITCH_STACK_F3_OFFSET, offsetof (struct switch_stack, f3));
	DEFINE(IA64_SWITCH_STACK_F4_OFFSET, offsetof (struct switch_stack, f4));
	DEFINE(IA64_SWITCH_STACK_F5_OFFSET, offsetof (struct switch_stack, f5));
	DEFINE(IA64_SWITCH_STACK_F12_OFFSET, offsetof (struct switch_stack, f12));
	DEFINE(IA64_SWITCH_STACK_F13_OFFSET, offsetof (struct switch_stack, f13));
	DEFINE(IA64_SWITCH_STACK_F14_OFFSET, offsetof (struct switch_stack, f14));
	DEFINE(IA64_SWITCH_STACK_F15_OFFSET, offsetof (struct switch_stack, f15));
	DEFINE(IA64_SWITCH_STACK_F16_OFFSET, offsetof (struct switch_stack, f16));
	DEFINE(IA64_SWITCH_STACK_F17_OFFSET, offsetof (struct switch_stack, f17));
	DEFINE(IA64_SWITCH_STACK_F18_OFFSET, offsetof (struct switch_stack, f18));
	DEFINE(IA64_SWITCH_STACK_F19_OFFSET, offsetof (struct switch_stack, f19));
	DEFINE(IA64_SWITCH_STACK_F20_OFFSET, offsetof (struct switch_stack, f20));
	DEFINE(IA64_SWITCH_STACK_F21_OFFSET, offsetof (struct switch_stack, f21));
	DEFINE(IA64_SWITCH_STACK_F22_OFFSET, offsetof (struct switch_stack, f22));
	DEFINE(IA64_SWITCH_STACK_F23_OFFSET, offsetof (struct switch_stack, f23));
	DEFINE(IA64_SWITCH_STACK_F24_OFFSET, offsetof (struct switch_stack, f24));
	DEFINE(IA64_SWITCH_STACK_F25_OFFSET, offsetof (struct switch_stack, f25));
	DEFINE(IA64_SWITCH_STACK_F26_OFFSET, offsetof (struct switch_stack, f26));
	DEFINE(IA64_SWITCH_STACK_F27_OFFSET, offsetof (struct switch_stack, f27));
	DEFINE(IA64_SWITCH_STACK_F28_OFFSET, offsetof (struct switch_stack, f28));
	DEFINE(IA64_SWITCH_STACK_F29_OFFSET, offsetof (struct switch_stack, f29));
	DEFINE(IA64_SWITCH_STACK_F30_OFFSET, offsetof (struct switch_stack, f30));
	DEFINE(IA64_SWITCH_STACK_F31_OFFSET, offsetof (struct switch_stack, f31));
	DEFINE(IA64_SWITCH_STACK_R4_OFFSET, offsetof (struct switch_stack, r4));
	DEFINE(IA64_SWITCH_STACK_R5_OFFSET, offsetof (struct switch_stack, r5));
	DEFINE(IA64_SWITCH_STACK_R6_OFFSET, offsetof (struct switch_stack, r6));
	DEFINE(IA64_SWITCH_STACK_R7_OFFSET, offsetof (struct switch_stack, r7));
	DEFINE(IA64_SWITCH_STACK_B0_OFFSET, offsetof (struct switch_stack, b0));
	DEFINE(IA64_SWITCH_STACK_B1_OFFSET, offsetof (struct switch_stack, b1));
	DEFINE(IA64_SWITCH_STACK_B2_OFFSET, offsetof (struct switch_stack, b2));
	DEFINE(IA64_SWITCH_STACK_B3_OFFSET, offsetof (struct switch_stack, b3));
	DEFINE(IA64_SWITCH_STACK_B4_OFFSET, offsetof (struct switch_stack, b4));
	DEFINE(IA64_SWITCH_STACK_B5_OFFSET, offsetof (struct switch_stack, b5));
	DEFINE(IA64_SWITCH_STACK_AR_PFS_OFFSET, offsetof (struct switch_stack, ar_pfs));
	DEFINE(IA64_SWITCH_STACK_AR_LC_OFFSET, offsetof (struct switch_stack, ar_lc));
	DEFINE(IA64_SWITCH_STACK_AR_UNAT_OFFSET, offsetof (struct switch_stack, ar_unat));
	DEFINE(IA64_SWITCH_STACK_AR_RNAT_OFFSET, offsetof (struct switch_stack, ar_rnat));
	DEFINE(IA64_SWITCH_STACK_AR_BSPSTORE_OFFSET, offsetof (struct switch_stack, ar_bspstore));
	DEFINE(IA64_SWITCH_STACK_PR_OFFSET, offsetof (struct switch_stack, pr));

	BLANK();

	DEFINE(IA64_SIGCONTEXT_IP_OFFSET, offsetof (struct sigcontext, sc_ip));
	DEFINE(IA64_SIGCONTEXT_AR_BSP_OFFSET, offsetof (struct sigcontext, sc_ar_bsp));
	DEFINE(IA64_SIGCONTEXT_AR_FPSR_OFFSET, offsetof (struct sigcontext, sc_ar_fpsr));
	DEFINE(IA64_SIGCONTEXT_AR_RNAT_OFFSET, offsetof (struct sigcontext, sc_ar_rnat));
	DEFINE(IA64_SIGCONTEXT_AR_UNAT_OFFSET, offsetof (struct sigcontext, sc_ar_unat));
	DEFINE(IA64_SIGCONTEXT_B0_OFFSET, offsetof (struct sigcontext, sc_br[0]));
	DEFINE(IA64_SIGCONTEXT_CFM_OFFSET, offsetof (struct sigcontext, sc_cfm));
	DEFINE(IA64_SIGCONTEXT_FLAGS_OFFSET, offsetof (struct sigcontext, sc_flags));
	DEFINE(IA64_SIGCONTEXT_FR6_OFFSET, offsetof (struct sigcontext, sc_fr[6]));
	DEFINE(IA64_SIGCONTEXT_PR_OFFSET, offsetof (struct sigcontext, sc_pr));
	DEFINE(IA64_SIGCONTEXT_R12_OFFSET, offsetof (struct sigcontext, sc_gr[12]));
	DEFINE(IA64_SIGCONTEXT_RBS_BASE_OFFSET,offsetof (struct sigcontext, sc_rbs_base));
	DEFINE(IA64_SIGCONTEXT_LOADRS_OFFSET, offsetof (struct sigcontext, sc_loadrs));

	BLANK();

	DEFINE(IA64_SIGPENDING_SIGNAL_OFFSET, offsetof (struct sigpending, signal));

	BLANK();

	DEFINE(IA64_SIGFRAME_ARG0_OFFSET, offsetof (struct sigframe, arg0));
	DEFINE(IA64_SIGFRAME_ARG1_OFFSET, offsetof (struct sigframe, arg1));
	DEFINE(IA64_SIGFRAME_ARG2_OFFSET, offsetof (struct sigframe, arg2));
	DEFINE(IA64_SIGFRAME_HANDLER_OFFSET, offsetof (struct sigframe, handler));
	DEFINE(IA64_SIGFRAME_SIGCONTEXT_OFFSET, offsetof (struct sigframe, sc));
	BLANK();
    /* for assembly files which can't include sched.h: */
	DEFINE(IA64_CLONE_VFORK, CLONE_VFORK);
	DEFINE(IA64_CLONE_VM, CLONE_VM);

	BLANK();
	DEFINE(IA64_CPUINFO_NSEC_PER_CYC_OFFSET,
	       offsetof (struct cpuinfo_ia64, nsec_per_cyc));
	DEFINE(IA64_CPUINFO_PTCE_BASE_OFFSET,
	       offsetof (struct cpuinfo_ia64, ptce_base));
	DEFINE(IA64_CPUINFO_PTCE_COUNT_OFFSET,
	       offsetof (struct cpuinfo_ia64, ptce_count));
	DEFINE(IA64_CPUINFO_PTCE_STRIDE_OFFSET,
	       offsetof (struct cpuinfo_ia64, ptce_stride));
	BLANK();
	DEFINE(IA64_TIMESPEC_TV_NSEC_OFFSET,
	       offsetof (struct timespec, tv_nsec));

	DEFINE(CLONE_SETTLS_BIT, 19);
#if CLONE_SETTLS != (1<<19)
# error "CLONE_SETTLS_BIT incorrect, please fix"
#endif

	BLANK();
	DEFINE(IA64_MCA_CPU_MCA_STACK_OFFSET,
	       offsetof (struct ia64_mca_cpu, mca_stack));
	DEFINE(IA64_MCA_CPU_INIT_STACK_OFFSET,
	       offsetof (struct ia64_mca_cpu, init_stack));
	BLANK();
	DEFINE(IA64_SAL_OS_STATE_OS_GP_OFFSET,
	       offsetof (struct ia64_sal_os_state, os_gp));
	DEFINE(IA64_SAL_OS_STATE_PROC_STATE_PARAM_OFFSET,
	       offsetof (struct ia64_sal_os_state, proc_state_param));
	DEFINE(IA64_SAL_OS_STATE_SAL_RA_OFFSET,
	       offsetof (struct ia64_sal_os_state, sal_ra));
	DEFINE(IA64_SAL_OS_STATE_SAL_GP_OFFSET,
	       offsetof (struct ia64_sal_os_state, sal_gp));
	DEFINE(IA64_SAL_OS_STATE_PAL_MIN_STATE_OFFSET,
	       offsetof (struct ia64_sal_os_state, pal_min_state));
	DEFINE(IA64_SAL_OS_STATE_OS_STATUS_OFFSET,
	       offsetof (struct ia64_sal_os_state, os_status));
	DEFINE(IA64_SAL_OS_STATE_CONTEXT_OFFSET,
	       offsetof (struct ia64_sal_os_state, context));
	DEFINE(IA64_SAL_OS_STATE_SIZE,
	       sizeof (struct ia64_sal_os_state));
	BLANK();

	DEFINE(IA64_PMSA_GR_OFFSET,
	       offsetof (struct pal_min_state_area_s, pmsa_gr));
	DEFINE(IA64_PMSA_BANK1_GR_OFFSET,
	       offsetof (struct pal_min_state_area_s, pmsa_bank1_gr));
	DEFINE(IA64_PMSA_PR_OFFSET,
	       offsetof (struct pal_min_state_area_s, pmsa_pr));
	DEFINE(IA64_PMSA_BR0_OFFSET,
	       offsetof (struct pal_min_state_area_s, pmsa_br0));
	DEFINE(IA64_PMSA_RSC_OFFSET,
	       offsetof (struct pal_min_state_area_s, pmsa_rsc));
	DEFINE(IA64_PMSA_IIP_OFFSET,
	       offsetof (struct pal_min_state_area_s, pmsa_iip));
	DEFINE(IA64_PMSA_IPSR_OFFSET,
	       offsetof (struct pal_min_state_area_s, pmsa_ipsr));
	DEFINE(IA64_PMSA_IFS_OFFSET,
	       offsetof (struct pal_min_state_area_s, pmsa_ifs));
	DEFINE(IA64_PMSA_XIP_OFFSET,
	       offsetof (struct pal_min_state_area_s, pmsa_xip));
	BLANK();

	/* used by fsys_gettimeofday in arch/ia64/kernel/fsys.S */
	DEFINE(IA64_GTOD_SEQ_OFFSET,
	       offsetof (struct fsyscall_gtod_data_t, seq));
	DEFINE(IA64_GTOD_WALL_TIME_OFFSET,
		offsetof (struct fsyscall_gtod_data_t, wall_time));
	DEFINE(IA64_GTOD_MONO_TIME_OFFSET,
		offsetof (struct fsyscall_gtod_data_t, monotonic_time));
	DEFINE(IA64_CLKSRC_MASK_OFFSET,
		offsetof (struct fsyscall_gtod_data_t, clk_mask));
	DEFINE(IA64_CLKSRC_MULT_OFFSET,
		offsetof (struct fsyscall_gtod_data_t, clk_mult));
	DEFINE(IA64_CLKSRC_SHIFT_OFFSET,
		offsetof (struct fsyscall_gtod_data_t, clk_shift));
	DEFINE(IA64_CLKSRC_MMIO_OFFSET,
		offsetof (struct fsyscall_gtod_data_t, clk_fsys_mmio));
	DEFINE(IA64_CLKSRC_CYCLE_LAST_OFFSET,
		offsetof (struct fsyscall_gtod_data_t, clk_cycle_last));
	DEFINE(IA64_ITC_JITTER_OFFSET,
		offsetof (struct itc_jitter_data_t, itc_jitter));
	DEFINE(IA64_ITC_LASTCYCLE_OFFSET,
		offsetof (struct itc_jitter_data_t, itc_lastcycle));

}
