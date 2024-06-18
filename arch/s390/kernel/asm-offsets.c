// SPDX-License-Identifier: GPL-2.0
/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#define ASM_OFFSETS_C

#include <linux/kbuild.h>
#include <linux/kvm_host.h>
#include <linux/sched.h>
#include <linux/purgatory.h>
#include <linux/pgtable.h>
#include <linux/ftrace.h>
#include <asm/gmap.h>
#include <asm/stacktrace.h>

int main(void)
{
	/* task struct offsets */
	OFFSET(__TASK_stack, task_struct, stack);
	OFFSET(__TASK_thread, task_struct, thread);
	OFFSET(__TASK_pid, task_struct, pid);
	BLANK();
	/* thread struct offsets */
	OFFSET(__THREAD_ksp, thread_struct, ksp);
	BLANK();
	/* thread info offsets */
	OFFSET(__TI_flags, task_struct, thread_info.flags);
	BLANK();
	/* pt_regs offsets */
	OFFSET(__PT_PSW, pt_regs, psw);
	OFFSET(__PT_GPRS, pt_regs, gprs);
	OFFSET(__PT_R0, pt_regs, gprs[0]);
	OFFSET(__PT_R1, pt_regs, gprs[1]);
	OFFSET(__PT_R2, pt_regs, gprs[2]);
	OFFSET(__PT_R3, pt_regs, gprs[3]);
	OFFSET(__PT_R4, pt_regs, gprs[4]);
	OFFSET(__PT_R5, pt_regs, gprs[5]);
	OFFSET(__PT_R6, pt_regs, gprs[6]);
	OFFSET(__PT_R7, pt_regs, gprs[7]);
	OFFSET(__PT_R8, pt_regs, gprs[8]);
	OFFSET(__PT_R9, pt_regs, gprs[9]);
	OFFSET(__PT_R10, pt_regs, gprs[10]);
	OFFSET(__PT_R11, pt_regs, gprs[11]);
	OFFSET(__PT_R12, pt_regs, gprs[12]);
	OFFSET(__PT_R13, pt_regs, gprs[13]);
	OFFSET(__PT_R14, pt_regs, gprs[14]);
	OFFSET(__PT_R15, pt_regs, gprs[15]);
	OFFSET(__PT_ORIG_GPR2, pt_regs, orig_gpr2);
	OFFSET(__PT_FLAGS, pt_regs, flags);
	OFFSET(__PT_CR1, pt_regs, cr1);
	OFFSET(__PT_LAST_BREAK, pt_regs, last_break);
	DEFINE(__PT_SIZE, sizeof(struct pt_regs));
	BLANK();
	/* stack_frame offsets */
	OFFSET(__SF_BACKCHAIN, stack_frame, back_chain);
	OFFSET(__SF_GPRS, stack_frame, gprs);
	OFFSET(__SF_EMPTY, stack_frame, empty[0]);
	OFFSET(__SF_SIE_CONTROL, stack_frame, sie_control_block);
	OFFSET(__SF_SIE_SAVEAREA, stack_frame, sie_savearea);
	OFFSET(__SF_SIE_REASON, stack_frame, sie_reason);
	OFFSET(__SF_SIE_FLAGS, stack_frame, sie_flags);
	OFFSET(__SF_SIE_CONTROL_PHYS, stack_frame, sie_control_block_phys);
	DEFINE(STACK_FRAME_OVERHEAD, sizeof(struct stack_frame));
	BLANK();
	OFFSET(__SFUSER_BACKCHAIN, stack_frame_user, back_chain);
	DEFINE(STACK_FRAME_USER_OVERHEAD, sizeof(struct stack_frame_user));
	OFFSET(__SFVDSO_RETURN_ADDRESS, stack_frame_vdso_wrapper, return_address);
	DEFINE(STACK_FRAME_VDSO_OVERHEAD, sizeof(struct stack_frame_vdso_wrapper));
	BLANK();
	/* hardware defined lowcore locations 0x000 - 0x1ff */
	OFFSET(__LC_EXT_PARAMS, lowcore, ext_params);
	OFFSET(__LC_EXT_CPU_ADDR, lowcore, ext_cpu_addr);
	OFFSET(__LC_EXT_INT_CODE, lowcore, ext_int_code);
	OFFSET(__LC_PGM_ILC, lowcore, pgm_ilc);
	OFFSET(__LC_PGM_INT_CODE, lowcore, pgm_code);
	OFFSET(__LC_DATA_EXC_CODE, lowcore, data_exc_code);
	OFFSET(__LC_MON_CLASS_NR, lowcore, mon_class_num);
	OFFSET(__LC_PER_CODE, lowcore, per_code);
	OFFSET(__LC_PER_ATMID, lowcore, per_atmid);
	OFFSET(__LC_PER_ADDRESS, lowcore, per_address);
	OFFSET(__LC_EXC_ACCESS_ID, lowcore, exc_access_id);
	OFFSET(__LC_PER_ACCESS_ID, lowcore, per_access_id);
	OFFSET(__LC_OP_ACCESS_ID, lowcore, op_access_id);
	OFFSET(__LC_AR_MODE_ID, lowcore, ar_mode_id);
	OFFSET(__LC_TRANS_EXC_CODE, lowcore, trans_exc_code);
	OFFSET(__LC_MON_CODE, lowcore, monitor_code);
	OFFSET(__LC_SUBCHANNEL_ID, lowcore, subchannel_id);
	OFFSET(__LC_SUBCHANNEL_NR, lowcore, subchannel_nr);
	OFFSET(__LC_IO_INT_PARM, lowcore, io_int_parm);
	OFFSET(__LC_IO_INT_WORD, lowcore, io_int_word);
	OFFSET(__LC_MCCK_CODE, lowcore, mcck_interruption_code);
	OFFSET(__LC_EXT_DAMAGE_CODE, lowcore, external_damage_code);
	OFFSET(__LC_MCCK_FAIL_STOR_ADDR, lowcore, failing_storage_address);
	OFFSET(__LC_PGM_LAST_BREAK, lowcore, pgm_last_break);
	OFFSET(__LC_RETURN_LPSWE, lowcore, return_lpswe);
	OFFSET(__LC_RETURN_MCCK_LPSWE, lowcore, return_mcck_lpswe);
	OFFSET(__LC_RST_OLD_PSW, lowcore, restart_old_psw);
	OFFSET(__LC_EXT_OLD_PSW, lowcore, external_old_psw);
	OFFSET(__LC_SVC_OLD_PSW, lowcore, svc_old_psw);
	OFFSET(__LC_PGM_OLD_PSW, lowcore, program_old_psw);
	OFFSET(__LC_MCK_OLD_PSW, lowcore, mcck_old_psw);
	OFFSET(__LC_IO_OLD_PSW, lowcore, io_old_psw);
	OFFSET(__LC_RST_NEW_PSW, lowcore, restart_psw);
	OFFSET(__LC_EXT_NEW_PSW, lowcore, external_new_psw);
	OFFSET(__LC_SVC_NEW_PSW, lowcore, svc_new_psw);
	OFFSET(__LC_PGM_NEW_PSW, lowcore, program_new_psw);
	OFFSET(__LC_MCK_NEW_PSW, lowcore, mcck_new_psw);
	OFFSET(__LC_IO_NEW_PSW, lowcore, io_new_psw);
	/* software defined lowcore locations 0x200 - 0xdff*/
	OFFSET(__LC_SAVE_AREA_SYNC, lowcore, save_area_sync);
	OFFSET(__LC_SAVE_AREA_ASYNC, lowcore, save_area_async);
	OFFSET(__LC_SAVE_AREA_RESTART, lowcore, save_area_restart);
	OFFSET(__LC_CPU_FLAGS, lowcore, cpu_flags);
	OFFSET(__LC_RETURN_PSW, lowcore, return_psw);
	OFFSET(__LC_RETURN_MCCK_PSW, lowcore, return_mcck_psw);
	OFFSET(__LC_SYS_ENTER_TIMER, lowcore, sys_enter_timer);
	OFFSET(__LC_MCCK_ENTER_TIMER, lowcore, mcck_enter_timer);
	OFFSET(__LC_EXIT_TIMER, lowcore, exit_timer);
	OFFSET(__LC_LAST_UPDATE_TIMER, lowcore, last_update_timer);
	OFFSET(__LC_LAST_UPDATE_CLOCK, lowcore, last_update_clock);
	OFFSET(__LC_INT_CLOCK, lowcore, int_clock);
	OFFSET(__LC_BOOT_CLOCK, lowcore, boot_clock);
	OFFSET(__LC_CURRENT, lowcore, current_task);
	OFFSET(__LC_KERNEL_STACK, lowcore, kernel_stack);
	OFFSET(__LC_ASYNC_STACK, lowcore, async_stack);
	OFFSET(__LC_NODAT_STACK, lowcore, nodat_stack);
	OFFSET(__LC_RESTART_STACK, lowcore, restart_stack);
	OFFSET(__LC_MCCK_STACK, lowcore, mcck_stack);
	OFFSET(__LC_RESTART_FN, lowcore, restart_fn);
	OFFSET(__LC_RESTART_DATA, lowcore, restart_data);
	OFFSET(__LC_RESTART_SOURCE, lowcore, restart_source);
	OFFSET(__LC_RESTART_FLAGS, lowcore, restart_flags);
	OFFSET(__LC_KERNEL_ASCE, lowcore, kernel_asce);
	OFFSET(__LC_USER_ASCE, lowcore, user_asce);
	OFFSET(__LC_LPP, lowcore, lpp);
	OFFSET(__LC_CURRENT_PID, lowcore, current_pid);
	OFFSET(__LC_GMAP, lowcore, gmap);
	OFFSET(__LC_LAST_BREAK, lowcore, last_break);
	/* software defined ABI-relevant lowcore locations 0xe00 - 0xe20 */
	OFFSET(__LC_DUMP_REIPL, lowcore, ipib);
	OFFSET(__LC_VMCORE_INFO, lowcore, vmcore_info);
	OFFSET(__LC_OS_INFO, lowcore, os_info);
	/* hardware defined lowcore locations 0x1000 - 0x18ff */
	OFFSET(__LC_MCESAD, lowcore, mcesad);
	OFFSET(__LC_EXT_PARAMS2, lowcore, ext_params2);
	OFFSET(__LC_FPREGS_SAVE_AREA, lowcore, floating_pt_save_area);
	OFFSET(__LC_GPREGS_SAVE_AREA, lowcore, gpregs_save_area);
	OFFSET(__LC_PSW_SAVE_AREA, lowcore, psw_save_area);
	OFFSET(__LC_PREFIX_SAVE_AREA, lowcore, prefixreg_save_area);
	OFFSET(__LC_FP_CREG_SAVE_AREA, lowcore, fpt_creg_save_area);
	OFFSET(__LC_TOD_PROGREG_SAVE_AREA, lowcore, tod_progreg_save_area);
	OFFSET(__LC_CPU_TIMER_SAVE_AREA, lowcore, cpu_timer_save_area);
	OFFSET(__LC_CLOCK_COMP_SAVE_AREA, lowcore, clock_comp_save_area);
	OFFSET(__LC_LAST_BREAK_SAVE_AREA, lowcore, last_break_save_area);
	OFFSET(__LC_AREGS_SAVE_AREA, lowcore, access_regs_save_area);
	OFFSET(__LC_CREGS_SAVE_AREA, lowcore, cregs_save_area);
	OFFSET(__LC_PGM_TDB, lowcore, pgm_tdb);
	BLANK();
	/* gmap/sie offsets */
	OFFSET(__GMAP_ASCE, gmap, asce);
	OFFSET(__SIE_PROG0C, kvm_s390_sie_block, prog0c);
	OFFSET(__SIE_PROG20, kvm_s390_sie_block, prog20);
	/* kexec_sha_region */
	OFFSET(__KEXEC_SHA_REGION_START, kexec_sha_region, start);
	OFFSET(__KEXEC_SHA_REGION_LEN, kexec_sha_region, len);
	DEFINE(__KEXEC_SHA_REGION_SIZE, sizeof(struct kexec_sha_region));
	/* sizeof kernel parameter area */
	DEFINE(__PARMAREA_SIZE, sizeof(struct parmarea));
	/* kernel parameter area offsets */
	DEFINE(IPL_DEVICE, PARMAREA + offsetof(struct parmarea, ipl_device));
	DEFINE(INITRD_START, PARMAREA + offsetof(struct parmarea, initrd_start));
	DEFINE(INITRD_SIZE, PARMAREA + offsetof(struct parmarea, initrd_size));
	DEFINE(OLDMEM_BASE, PARMAREA + offsetof(struct parmarea, oldmem_base));
	DEFINE(OLDMEM_SIZE, PARMAREA + offsetof(struct parmarea, oldmem_size));
	DEFINE(COMMAND_LINE, PARMAREA + offsetof(struct parmarea, command_line));
	DEFINE(MAX_COMMAND_LINE_SIZE, PARMAREA + offsetof(struct parmarea, max_command_line_size));
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* function graph return value tracing */
	OFFSET(__FGRAPH_RET_GPR2, fgraph_ret_regs, gpr2);
	OFFSET(__FGRAPH_RET_FP, fgraph_ret_regs, fp);
	DEFINE(__FGRAPH_RET_SIZE, sizeof(struct fgraph_ret_regs));
#endif
	OFFSET(__FTRACE_REGS_PT_REGS, ftrace_regs, regs);
	DEFINE(__FTRACE_REGS_SIZE, sizeof(struct ftrace_regs));
	return 0;
}
