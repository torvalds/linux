/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/kbuild.h>
#include <asm/registers.h>
#include <asm/ucontext.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/gdb-stub.h>

#define DEF_PTREG(sym, reg) \
        asm volatile("\n->" #sym " %0 offsetof(struct pt_regs, " #reg ")" \
		     : : "i" (offsetof(struct pt_regs, reg)))

#define DEF_IREG(sym, reg) \
        asm volatile("\n->" #sym " %0 offsetof(struct user_context, " #reg ")" \
		     : : "i" (offsetof(struct user_context, reg)))

#define DEF_FREG(sym, reg) \
        asm volatile("\n->" #sym " %0 offsetof(struct user_context, " #reg ")" \
		     : : "i" (offsetof(struct user_context, reg)))

#define DEF_0REG(sym, reg) \
        asm volatile("\n->" #sym " %0 offsetof(struct frv_frame0, " #reg ")" \
		     : : "i" (offsetof(struct frv_frame0, reg)))

void foo(void)
{
	/* offsets into the thread_info structure */
	OFFSET(TI_TASK,			thread_info, task);
	OFFSET(TI_EXEC_DOMAIN,		thread_info, exec_domain);
	OFFSET(TI_FLAGS,		thread_info, flags);
	OFFSET(TI_STATUS,		thread_info, status);
	OFFSET(TI_CPU,			thread_info, cpu);
	OFFSET(TI_PREEMPT_COUNT,	thread_info, preempt_count);
	OFFSET(TI_ADDR_LIMIT,		thread_info, addr_limit);
	OFFSET(TI_RESTART_BLOCK,	thread_info, restart_block);
	BLANK();

	/* offsets into register file storage */
	DEF_PTREG(REG_PSR,		psr);
	DEF_PTREG(REG_ISR,		isr);
	DEF_PTREG(REG_CCR,		ccr);
	DEF_PTREG(REG_CCCR,		cccr);
	DEF_PTREG(REG_LR,		lr);
	DEF_PTREG(REG_LCR,		lcr);
	DEF_PTREG(REG_PC,		pc);
	DEF_PTREG(REG__STATUS,		__status);
	DEF_PTREG(REG_SYSCALLNO,	syscallno);
	DEF_PTREG(REG_ORIG_GR8,		orig_gr8);
	DEF_PTREG(REG_GNER0,		gner0);
	DEF_PTREG(REG_GNER1,		gner1);
	DEF_PTREG(REG_IACC0,		iacc0);
	DEF_PTREG(REG_TBR,		tbr);
	DEF_PTREG(REG_GR0,		tbr);
	DEFINE(REG__END,		sizeof(struct pt_regs));
	BLANK();

	DEF_0REG(REG_DCR,		debug.dcr);
	DEF_0REG(REG_IBAR0,		debug.ibar[0]);
	DEF_0REG(REG_DBAR0,		debug.dbar[0]);
	DEF_0REG(REG_DBDR00,		debug.dbdr[0][0]);
	DEF_0REG(REG_DBMR00,		debug.dbmr[0][0]);
	BLANK();

	DEF_IREG(__INT_GR0,		i.gr[0]);
	DEF_FREG(__USER_FPMEDIA,	f);
	DEF_FREG(__FPMEDIA_FR0,		f.fr[0]);
	DEF_FREG(__FPMEDIA_FNER0,	f.fner[0]);
	DEF_FREG(__FPMEDIA_MSR0,	f.msr[0]);
	DEF_FREG(__FPMEDIA_ACC0,	f.acc[0]);
	DEF_FREG(__FPMEDIA_ACCG0,	f.accg[0]);
	DEF_FREG(__FPMEDIA_FSR0,	f.fsr[0]);
	BLANK();

	DEFINE(NR_PT_REGS,		sizeof(struct pt_regs) / 4);
	DEFINE(NR_USER_INT_REGS,	sizeof(struct user_int_regs) / 4);
	DEFINE(NR_USER_FPMEDIA_REGS,	sizeof(struct user_fpmedia_regs) / 4);
	DEFINE(NR_USER_CONTEXT,		sizeof(struct user_context) / 4);
	DEFINE(FRV_FRAME0_SIZE,		sizeof(struct frv_frame0));
	BLANK();

	/* offsets into thread_struct */
	OFFSET(__THREAD_FRAME,		thread_struct, frame);
	OFFSET(__THREAD_CURR,		thread_struct, curr);
	OFFSET(__THREAD_SP,		thread_struct, sp);
	OFFSET(__THREAD_FP,		thread_struct, fp);
	OFFSET(__THREAD_LR,		thread_struct, lr);
	OFFSET(__THREAD_PC,		thread_struct, pc);
	OFFSET(__THREAD_GR16,		thread_struct, gr[0]);
	OFFSET(__THREAD_SCHED_LR,	thread_struct, sched_lr);
	OFFSET(__THREAD_FRAME0,		thread_struct, frame0);
	OFFSET(__THREAD_USER,		thread_struct, user);
	BLANK();

	/* offsets into frv_debug_status */
	OFFSET(DEBUG_BPSR,		frv_debug_status, bpsr);
	OFFSET(DEBUG_DCR,		frv_debug_status, dcr);
	OFFSET(DEBUG_BRR,		frv_debug_status, brr);
	OFFSET(DEBUG_NMAR,		frv_debug_status, nmar);
	BLANK();
}
