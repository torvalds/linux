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
	/* task struct offsets */
	OFFSET(__TASK_thread_info, task_struct, stack);
	OFFSET(__TASK_thread, task_struct, thread);
	OFFSET(__TASK_pid, task_struct, pid);
	BLANK();
	/* thread struct offsets */
	OFFSET(__THREAD_ksp, thread_struct, ksp);
	OFFSET(__THREAD_FPU_fpc, thread_struct, fpu.fpc);
	OFFSET(__THREAD_FPU_regs, thread_struct, fpu.regs);
	OFFSET(__THREAD_per_cause, thread_struct, per_event.cause);
	OFFSET(__THREAD_per_address, thread_struct, per_event.address);
	OFFSET(__THREAD_per_paid, thread_struct, per_event.paid);
	OFFSET(__THREAD_trap_tdb, thread_struct, trap_tdb);
	BLANK();
	/* thread info offsets */
	OFFSET(__TI_task, thread_info, task);
	OFFSET(__TI_flags, thread_info, flags);
	OFFSET(__TI_sysc_table, thread_info, sys_call_table);
	OFFSET(__TI_cpu, thread_info, cpu);
	OFFSET(__TI_precount, thread_info, preempt_count);
	OFFSET(__TI_user_timer, thread_info, user_timer);
	OFFSET(__TI_system_timer, thread_info, system_timer);
	OFFSET(__TI_last_break, thread_info, last_break);
	BLANK();
	/* pt_regs offsets */
	OFFSET(__PT_ARGS, pt_regs, args);
	OFFSET(__PT_PSW, pt_regs, psw);
	OFFSET(__PT_GPRS, pt_regs, gprs);
	OFFSET(__PT_ORIG_GPR2, pt_regs, orig_gpr2);
	OFFSET(__PT_INT_CODE, pt_regs, int_code);
	OFFSET(__PT_INT_PARM, pt_regs, int_parm);
	OFFSET(__PT_INT_PARM_LONG, pt_regs, int_parm_long);
	OFFSET(__PT_FLAGS, pt_regs, flags);
	DEFINE(__PT_SIZE, sizeof(struct pt_regs));
	BLANK();
	/* stack_frame offsets */
	OFFSET(__SF_BACKCHAIN, stack_frame, back_chain);
	OFFSET(__SF_GPRS, stack_frame, gprs);
	OFFSET(__SF_EMPTY, stack_frame, empty1);
	BLANK();
	/* timeval/timezone offsets for use by vdso */
	OFFSET(__VDSO_UPD_COUNT, vdso_data, tb_update_count);
	OFFSET(__VDSO_XTIME_STAMP, vdso_data, xtime_tod_stamp);
	OFFSET(__VDSO_XTIME_SEC, vdso_data, xtime_clock_sec);
	OFFSET(__VDSO_XTIME_NSEC, vdso_data, xtime_clock_nsec);
	OFFSET(__VDSO_XTIME_CRS_SEC, vdso_data, xtime_coarse_sec);
	OFFSET(__VDSO_XTIME_CRS_NSEC, vdso_data, xtime_coarse_nsec);
	OFFSET(__VDSO_WTOM_SEC, vdso_data, wtom_clock_sec);
	OFFSET(__VDSO_WTOM_NSEC, vdso_data, wtom_clock_nsec);
	OFFSET(__VDSO_WTOM_CRS_SEC, vdso_data, wtom_coarse_sec);
	OFFSET(__VDSO_WTOM_CRS_NSEC, vdso_data, wtom_coarse_nsec);
	OFFSET(__VDSO_TIMEZONE, vdso_data, tz_minuteswest);
	OFFSET(__VDSO_ECTG_OK, vdso_data, ectg_available);
	OFFSET(__VDSO_TK_MULT, vdso_data, tk_mult);
	OFFSET(__VDSO_TK_SHIFT, vdso_data, tk_shift);
	OFFSET(__VDSO_ECTG_BASE, vdso_per_cpu_data, ectg_timer_base);
	OFFSET(__VDSO_ECTG_USER, vdso_per_cpu_data, ectg_user_time);
	BLANK();
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
	OFFSET(__CLOCK_IDLE_ENTER, s390_idle_data, clock_idle_enter);
	OFFSET(__CLOCK_IDLE_EXIT, s390_idle_data, clock_idle_exit);
	OFFSET(__TIMER_IDLE_ENTER, s390_idle_data, timer_idle_enter);
	OFFSET(__TIMER_IDLE_EXIT, s390_idle_data, timer_idle_exit);
	BLANK();
	/* hardware defined lowcore locations 0x000 - 0x1ff */
	OFFSET(__LC_EXT_PARAMS, _lowcore, ext_params);
	OFFSET(__LC_EXT_CPU_ADDR, _lowcore, ext_cpu_addr);
	OFFSET(__LC_EXT_INT_CODE, _lowcore, ext_int_code);
	OFFSET(__LC_SVC_ILC, _lowcore, svc_ilc);
	OFFSET(__LC_SVC_INT_CODE, _lowcore, svc_code);
	OFFSET(__LC_PGM_ILC, _lowcore, pgm_ilc);
	OFFSET(__LC_PGM_INT_CODE, _lowcore, pgm_code);
	OFFSET(__LC_DATA_EXC_CODE, _lowcore, data_exc_code);
	OFFSET(__LC_MON_CLASS_NR, _lowcore, mon_class_num);
	OFFSET(__LC_PER_CODE, _lowcore, per_code);
	OFFSET(__LC_PER_ATMID, _lowcore, per_atmid);
	OFFSET(__LC_PER_ADDRESS, _lowcore, per_address);
	OFFSET(__LC_EXC_ACCESS_ID, _lowcore, exc_access_id);
	OFFSET(__LC_PER_ACCESS_ID, _lowcore, per_access_id);
	OFFSET(__LC_OP_ACCESS_ID, _lowcore, op_access_id);
	OFFSET(__LC_AR_MODE_ID, _lowcore, ar_mode_id);
	OFFSET(__LC_TRANS_EXC_CODE, _lowcore, trans_exc_code);
	OFFSET(__LC_MON_CODE, _lowcore, monitor_code);
	OFFSET(__LC_SUBCHANNEL_ID, _lowcore, subchannel_id);
	OFFSET(__LC_SUBCHANNEL_NR, _lowcore, subchannel_nr);
	OFFSET(__LC_IO_INT_PARM, _lowcore, io_int_parm);
	OFFSET(__LC_IO_INT_WORD, _lowcore, io_int_word);
	OFFSET(__LC_STFL_FAC_LIST, _lowcore, stfl_fac_list);
	OFFSET(__LC_MCCK_CODE, _lowcore, mcck_interruption_code);
	OFFSET(__LC_MCCK_FAIL_STOR_ADDR, _lowcore, failing_storage_address);
	OFFSET(__LC_LAST_BREAK, _lowcore, breaking_event_addr);
	OFFSET(__LC_RST_OLD_PSW, _lowcore, restart_old_psw);
	OFFSET(__LC_EXT_OLD_PSW, _lowcore, external_old_psw);
	OFFSET(__LC_SVC_OLD_PSW, _lowcore, svc_old_psw);
	OFFSET(__LC_PGM_OLD_PSW, _lowcore, program_old_psw);
	OFFSET(__LC_MCK_OLD_PSW, _lowcore, mcck_old_psw);
	OFFSET(__LC_IO_OLD_PSW, _lowcore, io_old_psw);
	OFFSET(__LC_RST_NEW_PSW, _lowcore, restart_psw);
	OFFSET(__LC_EXT_NEW_PSW, _lowcore, external_new_psw);
	OFFSET(__LC_SVC_NEW_PSW, _lowcore, svc_new_psw);
	OFFSET(__LC_PGM_NEW_PSW, _lowcore, program_new_psw);
	OFFSET(__LC_MCK_NEW_PSW, _lowcore, mcck_new_psw);
	OFFSET(__LC_IO_NEW_PSW, _lowcore, io_new_psw);
	/* software defined lowcore locations 0x200 - 0xdff*/
	OFFSET(__LC_SAVE_AREA_SYNC, _lowcore, save_area_sync);
	OFFSET(__LC_SAVE_AREA_ASYNC, _lowcore, save_area_async);
	OFFSET(__LC_SAVE_AREA_RESTART, _lowcore, save_area_restart);
	OFFSET(__LC_CPU_FLAGS, _lowcore, cpu_flags);
	OFFSET(__LC_RETURN_PSW, _lowcore, return_psw);
	OFFSET(__LC_RETURN_MCCK_PSW, _lowcore, return_mcck_psw);
	OFFSET(__LC_SYNC_ENTER_TIMER, _lowcore, sync_enter_timer);
	OFFSET(__LC_ASYNC_ENTER_TIMER, _lowcore, async_enter_timer);
	OFFSET(__LC_MCCK_ENTER_TIMER, _lowcore, mcck_enter_timer);
	OFFSET(__LC_EXIT_TIMER, _lowcore, exit_timer);
	OFFSET(__LC_USER_TIMER, _lowcore, user_timer);
	OFFSET(__LC_SYSTEM_TIMER, _lowcore, system_timer);
	OFFSET(__LC_STEAL_TIMER, _lowcore, steal_timer);
	OFFSET(__LC_LAST_UPDATE_TIMER, _lowcore, last_update_timer);
	OFFSET(__LC_LAST_UPDATE_CLOCK, _lowcore, last_update_clock);
	OFFSET(__LC_INT_CLOCK, _lowcore, int_clock);
	OFFSET(__LC_MCCK_CLOCK, _lowcore, mcck_clock);
	OFFSET(__LC_CURRENT, _lowcore, current_task);
	OFFSET(__LC_THREAD_INFO, _lowcore, thread_info);
	OFFSET(__LC_KERNEL_STACK, _lowcore, kernel_stack);
	OFFSET(__LC_ASYNC_STACK, _lowcore, async_stack);
	OFFSET(__LC_PANIC_STACK, _lowcore, panic_stack);
	OFFSET(__LC_RESTART_STACK, _lowcore, restart_stack);
	OFFSET(__LC_RESTART_FN, _lowcore, restart_fn);
	OFFSET(__LC_RESTART_DATA, _lowcore, restart_data);
	OFFSET(__LC_RESTART_SOURCE, _lowcore, restart_source);
	OFFSET(__LC_USER_ASCE, _lowcore, user_asce);
	OFFSET(__LC_LPP, _lowcore, lpp);
	OFFSET(__LC_CURRENT_PID, _lowcore, current_pid);
	OFFSET(__LC_PERCPU_OFFSET, _lowcore, percpu_offset);
	OFFSET(__LC_VDSO_PER_CPU, _lowcore, vdso_per_cpu_data);
	OFFSET(__LC_MACHINE_FLAGS, _lowcore, machine_flags);
	OFFSET(__LC_GMAP, _lowcore, gmap);
	OFFSET(__LC_PASTE, _lowcore, paste);
	/* software defined ABI-relevant lowcore locations 0xe00 - 0xe20 */
	OFFSET(__LC_DUMP_REIPL, _lowcore, ipib);
	/* hardware defined lowcore locations 0x1000 - 0x18ff */
	OFFSET(__LC_VX_SAVE_AREA_ADDR, _lowcore, vector_save_area_addr);
	OFFSET(__LC_EXT_PARAMS2, _lowcore, ext_params2);
	OFFSET(SAVE_AREA_BASE, _lowcore, floating_pt_save_area);
	OFFSET(__LC_FPREGS_SAVE_AREA, _lowcore, floating_pt_save_area);
	OFFSET(__LC_GPREGS_SAVE_AREA, _lowcore, gpregs_save_area);
	OFFSET(__LC_PSW_SAVE_AREA, _lowcore, psw_save_area);
	OFFSET(__LC_PREFIX_SAVE_AREA, _lowcore, prefixreg_save_area);
	OFFSET(__LC_FP_CREG_SAVE_AREA, _lowcore, fpt_creg_save_area);
	OFFSET(__LC_TOD_PROGREG_SAVE_AREA, _lowcore, tod_progreg_save_area);
	OFFSET(__LC_CPU_TIMER_SAVE_AREA, _lowcore, cpu_timer_save_area);
	OFFSET(__LC_CLOCK_COMP_SAVE_AREA, _lowcore, clock_comp_save_area);
	OFFSET(__LC_AREGS_SAVE_AREA, _lowcore, access_regs_save_area);
	OFFSET(__LC_CREGS_SAVE_AREA, _lowcore, cregs_save_area);
	OFFSET(__LC_PGM_TDB, _lowcore, pgm_tdb);
	BLANK();
	/* gmap/sie offsets */
	OFFSET(__GMAP_ASCE, gmap, asce);
	OFFSET(__SIE_PROG0C, kvm_s390_sie_block, prog0c);
	OFFSET(__SIE_PROG20, kvm_s390_sie_block, prog20);
	return 0;
}
