/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/oom.h>
#include <kern_util.h>
#include <os.h>
#include <skas.h>

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

static void kill_off_processes(void)
{
	struct task_struct *p;
	int pid;

	read_lock(&tasklist_lock);
	for_each_process(p) {
		struct task_struct *t;

		t = find_lock_task_mm(p);
		if (!t)
			continue;
		pid = t->mm->context.id.u.pid;
		task_unlock(t);
		os_kill_ptraced_process(pid, 1);
	}
	read_unlock(&tasklist_lock);
}

void uml_cleanup(void)
{
	kmalloc_ok = 0;
	do_uml_exitcalls();
	kill_off_processes();
}

void machine_restart(char * __unused)
{
	uml_cleanup();
	reboot_skas();
}

void machine_power_off(void)
{
	uml_cleanup();
	halt_skas();
}

void machine_halt(void)
{
	machine_power_off();
}
