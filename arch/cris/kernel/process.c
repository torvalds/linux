/* $Id: process.c,v 1.21 2005/03/04 08:16:17 starvik Exp $
 * 
 *  linux/arch/cris/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000-2002  Axis Communications AB
 *
 *  Authors:   Bjorn Wesen (bjornw@axis.com)
 *
 *  $Log: process.c,v $
 *  Revision 1.21  2005/03/04 08:16:17  starvik
 *  Merge of Linux 2.6.11.
 *
 *  Revision 1.20  2005/01/18 05:57:22  starvik
 *  Renamed hlt_counter to cris_hlt_counter and made it global.
 *
 *  Revision 1.19  2004/10/19 13:07:43  starvik
 *  Merge of Linux 2.6.9
 *
 *  Revision 1.18  2004/08/16 12:37:23  starvik
 *  Merge of Linux 2.6.8
 *
 *  Revision 1.17  2004/04/05 13:53:48  starvik
 *  Merge of Linux 2.6.5
 *
 *  Revision 1.16  2003/10/27 08:04:33  starvik
 *  Merge of Linux 2.6.0-test9
 *
 *  Revision 1.15  2003/09/11 07:29:52  starvik
 *  Merge of Linux 2.6.0-test5
 *
 *  Revision 1.14  2003/06/10 10:21:12  johana
 *  Moved thread_saved_pc() from arch/cris/kernel/process.c to
 *  subarch specific process.c. arch-v32 has an erp, no irp.
 *
 *  Revision 1.13  2003/04/09 05:20:47  starvik
 *  Merge of Linux 2.5.67
 *
 *  Revision 1.12  2002/12/11 15:41:11  starvik
 *  Extracted v10 (ETRAX 100LX) specific stuff to arch/cris/arch-v10/kernel
 *
 *  Revision 1.11  2002/12/10 09:00:10  starvik
 *  Merge of Linux 2.5.51
 *
 *  Revision 1.10  2002/11/27 08:42:34  starvik
 *  Argument to user_regs() is thread_info*
 *
 *  Revision 1.9  2002/11/26 09:44:21  starvik
 *  New threads exits through ret_from_fork (necessary for preemptive scheduling)
 *
 *  Revision 1.8  2002/11/19 14:35:24  starvik
 *  Changes from linux 2.4
 *  Changed struct initializer syntax to the currently prefered notation
 *
 *  Revision 1.7  2002/11/18 07:39:42  starvik
 *  thread_saved_pc moved here from processor.h
 *
 *  Revision 1.6  2002/11/14 06:51:27  starvik
 *  Made cpu_idle more similar with other archs
 *  init_task_union -> init_thread_union
 *  Updated for new interrupt macros
 *  sys_clone and do_fork have a new argument, user_tid
 *
 *  Revision 1.5  2002/11/05 06:45:11  starvik
 *  Merge of Linux 2.5.45
 *
 *  Revision 1.4  2002/02/05 15:37:44  bjornw
 *  Need init_task.h
 *
 *  Revision 1.3  2002/01/21 15:22:49  bjornw
 *  current->counter is gone
 *
 *  Revision 1.22  2001/11/13 09:40:43  orjanf
 *  Added dump_fpu (needed for core dumps).
 *
 *  Revision 1.21  2001/11/12 18:26:21  pkj
 *  Fixed compiler warnings.
 *
 *  Revision 1.20  2001/10/03 08:21:39  jonashg
 *  cause_of_death does not exist if CONFIG_SVINTO_SIM is defined.
 *
 *  Revision 1.19  2001/09/26 11:52:54  bjornw
 *  INIT_MMAP is gone in 2.4.10
 *
 *  Revision 1.18  2001/08/21 21:43:51  hp
 *  Move last watchdog fix inside #ifdef CONFIG_ETRAX_WATCHDOG
 *
 *  Revision 1.17  2001/08/21 13:48:01  jonashg
 *  Added fix by HP to avoid oops when doing a hard_reset_now.
 *
 *  Revision 1.16  2001/06/21 02:00:40  hp
 *  	* entry.S: Include asm/unistd.h.
 *  	(_sys_call_table): Use section .rodata, not .data.
 *  	(_kernel_thread): Move from...
 *  	* process.c: ... here.
 *  	* entryoffsets.c (VAL): Break out from...
 *  	(OF): Use VAL.
 *  	(LCLONE_VM): New asmified value from CLONE_VM.
 *
 *  Revision 1.15  2001/06/20 16:31:57  hp
 *  Add comments to describe empty functions according to review.
 *
 *  Revision 1.14  2001/05/29 11:27:59  markusl
 *  Fixed so that hard_reset_now will do reset even if watchdog wasn't enabled
 *
 *  Revision 1.13  2001/03/20 19:44:06  bjornw
 *  Use the 7th syscall argument for regs instead of current_regs
 *
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <asm/atomic.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/fs_struct.h>
#include <linux/init_task.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/mqueue.h>
#include <linux/reboot.h>

//#define DEBUG

/*
 * Initial task structure. Make this a per-architecture thing,
 * because different architectures tend to have different
 * alignment requirements and potentially different initial
 * setup.
 */

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

EXPORT_SYMBOL(init_mm);

/*
 * Initial thread structure.
 *
 * We need to make sure that this is 8192-byte aligned due to the
 * way process stacks are handled. This is done by having a special
 * "init_task" linker map entry..
 */
union thread_union init_thread_union 
	__attribute__((__section__(".data.init_task"))) =
		{ INIT_THREAD_INFO(init_task) };

/*
 * Initial task structure.
 *
 * All other task structs will be allocated on slabs in fork.c
 */
struct task_struct init_task = INIT_TASK(init_task);

EXPORT_SYMBOL(init_task);

/*
 * The hlt_counter, disable_hlt and enable_hlt is just here as a hook if
 * there would ever be a halt sequence (for power save when idle) with
 * some largish delay when halting or resuming *and* a driver that can't
 * afford that delay.  The hlt_counter would then be checked before
 * executing the halt sequence, and the driver marks the unhaltable
 * region by enable_hlt/disable_hlt.
 */

int cris_hlt_counter=0;

void disable_hlt(void)
{
	cris_hlt_counter++;
}

EXPORT_SYMBOL(disable_hlt);

void enable_hlt(void)
{
	cris_hlt_counter--;
}

EXPORT_SYMBOL(enable_hlt);
 
/*
 * The following aren't currently used.
 */
void (*pm_idle)(void);

extern void default_idle(void);

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle (void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched()) {
			void (*idle)(void);
			/*
			 * Mark this as an RCU critical section so that
			 * synchronize_kernel() in the unload path waits
			 * for our completion.
			 */
			idle = pm_idle;
			if (!idle)
				idle = default_idle;
			idle();
		}
		schedule();
	}
}

void hard_reset_now (void);

void machine_restart(char *cmd)
{
	hard_reset_now();
}

/*
 * Similar to machine_power_off, but don't shut off power.  Add code
 * here to freeze the system for e.g. post-mortem debug purpose when
 * possible.  This halt has nothing to do with the idle halt.
 */

void machine_halt(void)
{
}

/* If or when software power-off is implemented, add code here.  */

void machine_power_off(void)
{
}

/*
 * When a process does an "exec", machine state like FPU and debug
 * registers need to be reset.  This is a hook function for that.
 * Currently we don't have any such state to reset, so this is empty.
 */

void flush_thread(void)
{
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
#if 0
	int i;

	/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->esp & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	for (i = 0; i < 8; i++)
		dump->u_debugreg[i] = current->debugreg[i];  

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

	dump->regs = *regs;

	dump->u_fpvalid = dump_fpu (regs, &dump->i387);
#endif 
}

/* Fill in the fpu structure for a core dump. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
        return 0;
}
