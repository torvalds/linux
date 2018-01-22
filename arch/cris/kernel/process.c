// SPDX-License-Identifier: GPL-2.0
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
#include <linux/uaccess.h>
#include <asm/irq.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/init_task.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/fs.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/mqueue.h>
#include <linux/reboot.h>
#include <linux/rcupdate.h>

//#define DEBUG

extern void default_idle(void);

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

void arch_cpu_idle(void)
{
	default_idle();
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
