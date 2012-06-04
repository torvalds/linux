/*
 *  linux/arch/cris/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000-2002  Axis Communications AB
 *
 *  Authors:   Bjorn Wesen (bjornw@axis.com)
 *
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/atomic.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/init_task.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/mqueue.h>
#include <linux/reboot.h>

//#define DEBUG

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

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

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
		schedule_preempt_disabled();
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

/* Fill in the fpu structure for a core dump. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
        return 0;
}
