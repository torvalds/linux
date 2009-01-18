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

#include <asm/atomic.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/system.h>
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
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
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
