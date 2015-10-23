/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#define ASM_OFFSETS_C

#include <linux/kbuild.h>
#include <linux/kvm_host.h>
#include <linux/sched.h>
#include <asm/idle.h>
#include <asm/vdso.h>
#include <asm/pgtable.h>

/*
 * Make sure that the compiler is new enough. We want a compiler that
 * is known to work with the "Q" assembler constraint.
 */
#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 3)
#error Your compiler is too old; please use version 4.3 or newer
#endif

int main(void)
{
	DEFINE(__TASK_thread_info, offsetof(struct task_struct, stack));
	DEFINE(__TASK_thread, offsetof(struct task_struct, thread));
	DEFINE(__TASK_pid, offsetof(struct task_struct, pid));
	BLANK();
	DEFINE(__THREAD_ksp, offsetof(struct thread_struct, ksp));
	DEFINE(__THREAD_FPU_fpc, offsetof(struct thread_struct, fpu.fpc));
	DEFINE(__THREAD_FPU_flags, offsetof(struct thread_struct, fpu.flags));
	DEFINE(__THREAD_FPU_regs, offsetof(struct thread_struct, fpu.regs));
	DEFINE(__THREAD_per_cause, offsetof(struct thread_struct, per_event.cause));
	DEFINE(__THREAD_per_address, offsetof(struct thread_struct, per_event.address));
	DEFINE(__THREAD_per_paid, offsetof(struct thread_struct, per_event.paid));
	DEFINE(__THREAD_trap_tdb, offsetof(struct thread_struct, trap_tdb));
	BLANK();
	DEFINE(__TI_task, offsetof(struct thread_info, task));
	DEFINE(__TI_flags, offsetof(struct thread_info, flags));
	DEFINE(__TI_sysc_table, offsetof(struct thread_info, sys_call_table));
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
	DEFINE(__PT_INT_CODE, offsetof(struct pt_regs, int_code));
	DEFINE(__PT_INT_PARM, offsetof(struct pt_regs, int_parm));
	DEFINE(__PT_INT_PARM_LONG, offsetof(struct pt_regs, int_parm_long));
	DEFINE(__PT_FLAGS, offsetof(struct pt_regs, flags));
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
	DEFINE(__VDSO_XTIME_CRS_SEC, offsetof(struct vdso_data, xtime_coarse_sec));
	DEFINE(__VDSO_XTIME_CRS_NSEC, offsetof(struct vdso_data, xtime_coarse_nsec));
	DEFINE(__VDSO_WTOM_SEC, offsetof(struct vdso_data, wtom_clock_sec));
	DEFINE(__VDSO_WTOM_NSEC, offsetof(struct vdso_data, wtom_clock_nsec));
	DEFINE(__VDSO_WTOM_CRS_SEC, offsetof(struct vdso_data, wtom_coarse_sec));
	DEFINE(__VDSO_WTOM_CRS_NSEC, offsetof(struct vdso_data, wtom_coarse_nsec));
	DEFINE(__VDSO_TIMEZONE, offsetof(struct vdso_data, tz_minuteswest));
	DEFINE(__VDSO_ECTG_OK, offsetof(struct vdso_data, ectg_available));
	DEFINE(__VDSO_TK_MULT, offsetof(struct vdso_data, tk_mult));
	DEFINE(__VDSO_TK_SHIFT, offsetof(struct vdso_data, tk_shift));
	DEFINE(__VDSO_ECTG_BASE, offsetof(struct vdso_per_cpu_data, ectg_timer_base));
	DEFINE(__VDSO_ECTG_USER, offsetof(struct vdso_per_cpu_data, ectg_user_time));
	/* constants used by the vdso */
	DEFINE(__CLOCK_REALTIME, CLOCK_REALTIME);
	DEFINE(__CLOCK_MONOTONIC, CLOCK_MONOTONIC);
	DEFINE(__CLOCK_REALTIME_COARSE, CLOCK_REALTIME_COARSE);
	DEFINE(__CLOCK_MONOTONIC_COARSE, CLOCK_MONOTONIC_COARSE);
	DEFINE(__CLOCK_THREAD_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID);
	DEFINE(__CLOCK_REALTIME_RES, MONOTONIC_RES_NSEC);
	DEFINE(__CLOCK_COARSE_RES, LOW_RES_NSEC);
	BLANK();
	/* idle data offsets */
	DEFINE(__CLOCK_IDLE_ENTER, offsetof(struct s390_idle_data, clock_idle_enter));
	DEFINE(__CLOCK_IDLE_EXIT, offsetof(struct s390_idle_data, clock_idle_exit));
	DEFINE(__TIMER_IDLE_ENTER, offsetof(struct s390_idle_data, timer_idle_enter));
	DEFINE(__TIMER_IDLE_EXIT, offsetof(struct s390_idle_data, timer_idle_exit));
	/* lowcore offsets */
	DEFINE(__LC_EXT_PARAMS, offsetof(struct _lowcore, ext_params));
	DEFINE(__LC_EXT_CPU_ADDR, offsetof(struct _lowcore, ext_cpu_addr));
	DEFINE(__LC_EXT_INT_CODE, offsetof(struct _lowcore, ext_int_code));
	DEFINE(__LC_SVC_ILC, offsetof(struct _lowcore, svc_ilc));
	DEFINE(__LC_SVC_INT_CODE, offsetof(struct _lowcore, svc_code));
	DEFINE(__LC_PGM_ILC, offsetof(struct _lowcore, pgm_ilc));
	DEFINE(__LC_PGM_INT_CODE, offsetof(struct _lowcore, pgm_code));
	DEFINE(__LC_TRANS_EXC_CODE, offsetof(struct _lowcore, trans_exc_code));
	DEFINE(__LC_MON_CLASS_NR, offsetof(struct _lowcore, mon_class_num));
	DEFINE(__LC_PER_CODE, offsetof(struct _lowcore, per_code));
	DEFINE(__LC_PER_ATMID, offsetof(struct _lowcore, per_atmid));
	DEFINE(__LC_PER_ADDRESS, offsetof(struct _lowcore, per_address));
	DEFINE(__LC_EXC_ACCESS_ID, offsetof(struct _lowcore, exc_access_id));
	DEFINE(__LC_PER_ACCESS_ID, offsetof(struct _lowcore, per_access_id));
	DEFINE(__LC_OP_ACCESS_ID, offsetof(struct _lowcore, op_access_id));
	DEFINE(__LC_AR_MODE_ID, offsetof(struct _lowcore, ar_mode_id));
	DEFINE(__LC_MON_CODE, offsetof(struct _lowcore, monitor_code));
	DEFINE(__LC_SUBCHANNEL_ID, offsetof(struct _lowcore, subchannel_id));
	DEFINE(__LC_SUBCHANNEL_NR, offsetof(struct _lowcore, subchannel_nr));
	DEFINE(__LC_IO_INT_PARM, offsetof(struct _lowcore, io_int_parm));
	DEFINE(__LC_IO_INT_WORD, offsetof(struct _lowcore, io_int_word));
	DEFINE(__LC_STFL_FAC_LIST, offsetof(struct _lowcore, stfl_fac_list));
	DEFINE(__LC_MCCK_CODE, offsetof(struct _lowcore, mcck_interruption_code));
	DEFINE(__LC_MCCK_EXT_DAM_CODE, offsetof(struct _lowcore, external_damage_code));
	DEFINE(__LC_RST_OLD_PSW, offsetof(struct _lowcore, restart_old_psw));
	DEFINE(__LC_EXT_OLD_PSW, offsetof(struct _lowcore, external_old_psw));
	DEFINE(__LC_SVC_OLD_PSW, offsetof(struct _lowcore, svc_old_psw));
	DEFINE(__LC_PGM_OLD_PSW, offsetof(struct _lowcore, program_old_psw));
	DEFINE(__LC_MCK_OLD_PSW, offsetof(struct _lowcore, mcck_old_psw));
	DEFINE(__LC_IO_OLD_PSW, offsetof(struct _lowcore, io_old_psw));
	DEFINE(__LC_RST_NEW_PSW, offsetof(struct _lowcore, restart_psw));
	DEFINE(__LC_EXT_NEW_PSW, offsetof(struct _lowcore, external_new_psw));
	DEFINE(__LC_SVC_NEW_PSW, offsetof(struct _lowcore, svc_new_psw));
	DEFINE(__LC_PGM_NEW_PSW, offsetof(struct _lowcore, program_new_psw));
	DEFINE(__LC_MCK_NEW_PSW, offsetof(struct _lowcore, mcck_new_psw));
	DEFINE(__LC_IO_NEW_PSW, offsetof(struct _lowcore, io_new_psw));
	BLANK();
	DEFINE(__LC_SAVE_AREA_SYNC, offsetof(struct _lowcore, save_area_sync));
	DEFINE(__LC_SAVE_AREA_ASYNC, offsetof(struct _lowcore, save_area_async));
	DEFINE(__LC_SAVE_AREA_RESTART, offsetof(struct _lowcore, save_area_restart));
	DEFINE(__LC_CPU_FLAGS, offsetof(struct _lowcore, cpu_flags));
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
	DEFINE(__LC_RESTART_STACK, offsetof(struct _lowcore, restart_stack));
	DEFINE(__LC_RESTART_FN, offsetof(struct _lowcore, restart_fn));
	DEFINE(__LC_RESTART_DATA, offsetof(struct _lowcore, restart_data));
	DEFINE(__LC_RESTART_SOURCE, offsetof(struct _lowcore, restart_source));
	DEFINE(__LC_KERNEL_ASCE, offsetof(struct _lowcore, kernel_asce));
	DEFINE(__LC_USER_ASCE, offsetof(struct _lowcore, user_asce));
	DEFINE(__LC_INT_CLOCK, offsetof(struct _lowcore, int_clock));
	DEFINE(__LC_MCCK_CLOCK, offsetof(struct _lowcore, mcck_clock));
	DEFINE(__LC_MACHINE_FLAGS, offsetof(struct _lowcore, machine_flags));
	DEFINE(__LC_DUMP_REIPL, offsetof(struct _lowcore, ipib));
	BLANK();
	DEFINE(__LC_CPU_TIMER_SAVE_AREA, offsetof(struct _lowcore, cpu_timer_save_area));
	DEFINE(__LC_CLOCK_COMP_SAVE_AREA, offsetof(struct _lowcore, clock_comp_save_area));
	DEFINE(__LC_PSW_SAVE_AREA, offsetof(struct _lowcore, psw_save_area));
	DEFINE(__LC_PREFIX_SAVE_AREA, offsetof(struct _lowcore, prefixreg_save_area));
	DEFINE(__LC_AREGS_SAVE_AREA, offsetof(struct _lowcore, access_regs_save_area));
	DEFINE(__LC_FPREGS_SAVE_AREA, offsetof(struct _lowcore, floating_pt_save_area));
	DEFINE(__LC_GPREGS_SAVE_AREA, offsetof(struct _lowcore, gpregs_save_area));
	DEFINE(__LC_CREGS_SAVE_AREA, offsetof(struct _lowcore, cregs_save_area));
	DEFINE(__LC_DATA_EXC_CODE, offsetof(struct _lowcore, data_exc_code));
	DEFINE(__LC_MCCK_FAIL_STOR_ADDR, offsetof(struct _lowcore, failing_storage_address));
	DEFINE(__LC_VX_SAVE_AREA_ADDR, offsetof(struct _lowcore, vector_save_area_addr));
	DEFINE(__LC_EXT_PARAMS2, offsetof(struct _lowcore, ext_params2));
	DEFINE(SAVE_AREA_BASE, offsetof(struct _lowcore, floating_pt_save_area));
	DEFINE(__LC_PASTE, offsetof(struct _lowcore, paste));
	DEFINE(__LC_FP_CREG_SAVE_AREA, offsetof(struct _lowcore, fpt_creg_save_area));
	DEFINE(__LC_LAST_BREAK, offsetof(struct _lowcore, breaking_event_addr));
	DEFINE(__LC_PERCPU_OFFSET, offsetof(struct _lowcore, percpu_offset));
	DEFINE(__LC_VDSO_PER_CPU, offsetof(struct _lowcore, vdso_per_cpu_data));
	DEFINE(__LC_GMAP, offsetof(struct _lowcore, gmap));
	DEFINE(__LC_PGM_TDB, offsetof(struct _lowcore, pgm_tdb));
	DEFINE(__GMAP_ASCE, offsetof(struct gmap, asce));
	DEFINE(__SIE_PROG0C, offsetof(struct kvm_s390_sie_block, prog0c));
	DEFINE(__SIE_PROG20, offsetof(struct kvm_s390_sie_block, prog20));
	return 0;
}
