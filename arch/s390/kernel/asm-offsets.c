/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#define ASM_OFFSETS_C

#include <linux/kbuild.h>
#include <linux/sched.h>
#include <asm/vdso.h>
#include <asm/sigp.h>
#include <asm/pgtable.h>

/*
 * Make sure that the compiler is new enough. We want a compiler that
 * is known to work with the "Q" assembler constraint.
 */
#if __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 3)
#error Your compiler is too old; please use version 3.3.3 or newer
#endif

int main(void)
{
	DEFINE(__THREAD_info, offsetof(struct task_struct, stack));
	DEFINE(__THREAD_ksp, offsetof(struct task_struct, thread.ksp));
	DEFINE(__THREAD_mm_segment, offsetof(struct task_struct, thread.mm_segment));
	BLANK();
	DEFINE(__TASK_pid, offsetof(struct task_struct, pid));
	BLANK();
	DEFINE(__THREAD_per_cause, offsetof(struct task_struct, thread.per_event.cause));
	DEFINE(__THREAD_per_address, offsetof(struct task_struct, thread.per_event.address));
	DEFINE(__THREAD_per_paid, offsetof(struct task_struct, thread.per_event.paid));
	BLANK();
	DEFINE(__TI_task, offsetof(struct thread_info, task));
	DEFINE(__TI_domain, offsetof(struct thread_info, exec_domain));
	DEFINE(__TI_flags, offsetof(struct thread_info, flags));
	DEFINE(__TI_cpu, offsetof(struct thread_info, cpu));
	DEFINE(__TI_precount, offsetof(struct thread_info, preempt_count));
	DEFINE(__TI_user_timer, offsetof(struct thread_info, user_timer));
	DEFINE(__TI_system_timer, offsetof(struct thread_info, system_timer));
	DEFINE(__TI_last_break, offsetof(struct thread_info, last_break));
	BLANK();
	DEFINE(__PT_ARGS, offsetof(struct pt_regs, args));
	DEFINE(__PT_PSW, offsetof(struct pt_regs, psw));
	DEFINE(__PT_GPRS, offsetof(struct pt_regs, gprs));
	DEFINE(__PT_ORIG_GPR2, offsetof(struct pt_regs, orig_gpr2));
	DEFINE(__PT_SVC_CODE, offsetof(struct pt_regs, svc_code));
	DEFINE(__PT_SIZE, sizeof(struct pt_regs));
	BLANK();
	DEFINE(__SF_BACKCHAIN, offsetof(struct stack_frame, back_chain));
	DEFINE(__SF_GPRS, offsetof(struct stack_frame, gprs));
	DEFINE(__SF_EMPTY, offsetof(struct stack_frame, empty1));
	BLANK();
	/* timeval/timezone offsets for use by vdso */
	DEFINE(__VDSO_UPD_COUNT, offsetof(struct vdso_data, tb_update_count));
	DEFINE(__VDSO_XTIME_STAMP, offsetof(struct vdso_data, xtime_tod_stamp));
	DEFINE(__VDSO_XTIME_SEC, offsetof(struct vdso_data, xtime_clock_sec));
	DEFINE(__VDSO_XTIME_NSEC, offsetof(struct vdso_data, xtime_clock_nsec));
	DEFINE(__VDSO_WTOM_SEC, offsetof(struct vdso_data, wtom_clock_sec));
	DEFINE(__VDSO_WTOM_NSEC, offsetof(struct vdso_data, wtom_clock_nsec));
	DEFINE(__VDSO_TIMEZONE, offsetof(struct vdso_data, tz_minuteswest));
	DEFINE(__VDSO_ECTG_OK, offsetof(struct vdso_data, ectg_available));
	DEFINE(__VDSO_NTP_MULT, offsetof(struct vdso_data, ntp_mult));
	DEFINE(__VDSO_ECTG_BASE, offsetof(struct vdso_per_cpu_data, ectg_timer_base));
	DEFINE(__VDSO_ECTG_USER, offsetof(struct vdso_per_cpu_data, ectg_user_time));
	/* constants used by the vdso */
	DEFINE(__CLOCK_REALTIME, CLOCK_REALTIME);
	DEFINE(__CLOCK_MONOTONIC, CLOCK_MONOTONIC);
	DEFINE(__CLOCK_REALTIME_RES, MONOTONIC_RES_NSEC);
	BLANK();
	/* constants for SIGP */
	DEFINE(__SIGP_STOP, sigp_stop);
	DEFINE(__SIGP_RESTART, sigp_restart);
	DEFINE(__SIGP_SENSE, sigp_sense);
	DEFINE(__SIGP_INITIAL_CPU_RESET, sigp_initial_cpu_reset);
	BLANK();
	/* lowcore offsets */
	DEFINE(__LC_EXT_PARAMS, offsetof(struct _lowcore, ext_params));
	DEFINE(__LC_CPU_ADDRESS, offsetof(struct _lowcore, cpu_addr));
	DEFINE(__LC_EXT_INT_CODE, offsetof(struct _lowcore, ext_int_code));
	DEFINE(__LC_SVC_ILC, offsetof(struct _lowcore, svc_ilc));
	DEFINE(__LC_SVC_INT_CODE, offsetof(struct _lowcore, svc_code));
	DEFINE(__LC_PGM_ILC, offsetof(struct _lowcore, pgm_ilc));
	DEFINE(__LC_PGM_INT_CODE, offsetof(struct _lowcore, pgm_code));
	DEFINE(__LC_TRANS_EXC_CODE, offsetof(struct _lowcore, trans_exc_code));
	DEFINE(__LC_PER_CAUSE, offsetof(struct _lowcore, per_perc_atmid));
	DEFINE(__LC_PER_ADDRESS, offsetof(struct _lowcore, per_address));
	DEFINE(__LC_PER_PAID, offsetof(struct _lowcore, per_access_id));
	DEFINE(__LC_AR_MODE_ID, offsetof(struct _lowcore, ar_access_id));
	DEFINE(__LC_SUBCHANNEL_ID, offsetof(struct _lowcore, subchannel_id));
	DEFINE(__LC_SUBCHANNEL_NR, offsetof(struct _lowcore, subchannel_nr));
	DEFINE(__LC_IO_INT_PARM, offsetof(struct _lowcore, io_int_parm));
	DEFINE(__LC_IO_INT_WORD, offsetof(struct _lowcore, io_int_word));
	DEFINE(__LC_STFL_FAC_LIST, offsetof(struct _lowcore, stfl_fac_list));
	DEFINE(__LC_MCCK_CODE, offsetof(struct _lowcore, mcck_interruption_code));
	DEFINE(__LC_DUMP_REIPL, offsetof(struct _lowcore, ipib));
	BLANK();
	DEFINE(__LC_RST_NEW_PSW, offsetof(struct _lowcore, restart_psw));
	DEFINE(__LC_RST_OLD_PSW, offsetof(struct _lowcore, restart_old_psw));
	DEFINE(__LC_EXT_OLD_PSW, offsetof(struct _lowcore, external_old_psw));
	DEFINE(__LC_SVC_OLD_PSW, offsetof(struct _lowcore, svc_old_psw));
	DEFINE(__LC_PGM_OLD_PSW, offsetof(struct _lowcore, program_old_psw));
	DEFINE(__LC_MCK_OLD_PSW, offsetof(struct _lowcore, mcck_old_psw));
	DEFINE(__LC_IO_OLD_PSW, offsetof(struct _lowcore, io_old_psw));
	DEFINE(__LC_EXT_NEW_PSW, offsetof(struct _lowcore, external_new_psw));
	DEFINE(__LC_SVC_NEW_PSW, offsetof(struct _lowcore, svc_new_psw));
	DEFINE(__LC_PGM_NEW_PSW, offsetof(struct _lowcore, program_new_psw));
	DEFINE(__LC_MCK_NEW_PSW, offsetof(struct _lowcore, mcck_new_psw));
	DEFINE(__LC_IO_NEW_PSW, offsetof(struct _lowcore, io_new_psw));
	DEFINE(__LC_SAVE_AREA, offsetof(struct _lowcore, save_area));
	DEFINE(__LC_RETURN_PSW, offsetof(struct _lowcore, return_psw));
	DEFINE(__LC_RETURN_MCCK_PSW, offsetof(struct _lowcore, return_mcck_psw));
	DEFINE(__LC_SYNC_ENTER_TIMER, offsetof(struct _lowcore, sync_enter_timer));
	DEFINE(__LC_ASYNC_ENTER_TIMER, offsetof(struct _lowcore, async_enter_timer));
	DEFINE(__LC_MCCK_ENTER_TIMER, offsetof(struct _lowcore, mcck_enter_timer));
	DEFINE(__LC_EXIT_TIMER, offsetof(struct _lowcore, exit_timer));
	DEFINE(__LC_USER_TIMER, offsetof(struct _lowcore, user_timer));
	DEFINE(__LC_SYSTEM_TIMER, offsetof(struct _lowcore, system_timer));
	DEFINE(__LC_STEAL_TIMER, offsetof(struct _lowcore, steal_timer));
	DEFINE(__LC_LAST_UPDATE_TIMER, offsetof(struct _lowcore, last_update_timer));
	DEFINE(__LC_LAST_UPDATE_CLOCK, offsetof(struct _lowcore, last_update_clock));
	DEFINE(__LC_CURRENT, offsetof(struct _lowcore, current_task));
	DEFINE(__LC_CURRENT_PID, offsetof(struct _lowcore, current_pid));
	DEFINE(__LC_THREAD_INFO, offsetof(struct _lowcore, thread_info));
	DEFINE(__LC_KERNEL_STACK, offsetof(struct _lowcore, kernel_stack));
	DEFINE(__LC_ASYNC_STACK, offsetof(struct _lowcore, async_stack));
	DEFINE(__LC_PANIC_STACK, offsetof(struct _lowcore, panic_stack));
	DEFINE(__LC_USER_ASCE, offsetof(struct _lowcore, user_asce));
	DEFINE(__LC_INT_CLOCK, offsetof(struct _lowcore, int_clock));
	DEFINE(__LC_MCCK_CLOCK, offsetof(struct _lowcore, mcck_clock));
	DEFINE(__LC_MACHINE_FLAGS, offsetof(struct _lowcore, machine_flags));
	DEFINE(__LC_FTRACE_FUNC, offsetof(struct _lowcore, ftrace_func));
	DEFINE(__LC_IRB, offsetof(struct _lowcore, irb));
	DEFINE(__LC_CPU_TIMER_SAVE_AREA, offsetof(struct _lowcore, cpu_timer_save_area));
	DEFINE(__LC_CLOCK_COMP_SAVE_AREA, offsetof(struct _lowcore, clock_comp_save_area));
	DEFINE(__LC_PSW_SAVE_AREA, offsetof(struct _lowcore, psw_save_area));
	DEFINE(__LC_PREFIX_SAVE_AREA, offsetof(struct _lowcore, prefixreg_save_area));
	DEFINE(__LC_AREGS_SAVE_AREA, offsetof(struct _lowcore, access_regs_save_area));
	DEFINE(__LC_FPREGS_SAVE_AREA, offsetof(struct _lowcore, floating_pt_save_area));
	DEFINE(__LC_GPREGS_SAVE_AREA, offsetof(struct _lowcore, gpregs_save_area));
	DEFINE(__LC_CREGS_SAVE_AREA, offsetof(struct _lowcore, cregs_save_area));
#ifdef CONFIG_32BIT
	DEFINE(SAVE_AREA_BASE, offsetof(struct _lowcore, extended_save_area_addr));
#else /* CONFIG_32BIT */
	DEFINE(__LC_EXT_PARAMS2, offsetof(struct _lowcore, ext_params2));
	DEFINE(SAVE_AREA_BASE, offsetof(struct _lowcore, floating_pt_save_area));
	DEFINE(__LC_PASTE, offsetof(struct _lowcore, paste));
	DEFINE(__LC_FP_CREG_SAVE_AREA, offsetof(struct _lowcore, fpt_creg_save_area));
	DEFINE(__LC_LAST_BREAK, offsetof(struct _lowcore, breaking_event_addr));
	DEFINE(__LC_VDSO_PER_CPU, offsetof(struct _lowcore, vdso_per_cpu_data));
	DEFINE(__LC_GMAP, offsetof(struct _lowcore, gmap));
	DEFINE(__LC_CMF_HPP, offsetof(struct _lowcore, cmf_hpp));
	DEFINE(__GMAP_ASCE, offsetof(struct gmap, asce));
#endif /* CONFIG_32BIT */
	return 0;
}
