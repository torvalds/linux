/* drivers/android/kernel_debugger.c
 *
 * Guts of the kernel debugger.
 * Needs something to actually push commands to it.
 *
 * Copyright (C) 2007-2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/sysrq.h>
#include <linux/kernel_debugger.h>

#define dprintf(fmt...) (ctxt->printf(ctxt->cookie, fmt))

static void do_ps(struct kdbg_ctxt *ctxt)
{
	struct task_struct *g, *p;
	unsigned state;
	static const char stat_nam[] = "RSDTtZX";

	dprintf("pid   ppid  prio task            pc\n");
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		state = p->state ? __ffs(p->state) + 1 : 0;
		dprintf("%5d %5d %4d ", p->pid, p->parent->pid, p->prio);
		dprintf("%-13.13s %c", p->comm,
			state >= sizeof(stat_nam) ? '?' : stat_nam[state]);
		if (state == TASK_RUNNING)
			dprintf(" running\n");
		else
			dprintf(" %08lx\n", thread_saved_pc(p));
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
}

int log_buf_copy(char *dest, int idx, int len);
extern int do_syslog(int type, char __user *bug, int count);
static void do_sysrq(struct kdbg_ctxt *ctxt, char rq)
{
	char buf[128];
	int ret;
	int idx = 0;
	do_syslog(5 /* clear */, NULL, 0);
	handle_sysrq(rq);
	while (1) {
		ret = log_buf_copy(buf, idx, sizeof(buf) - 1);
		if (ret <= 0)
			break;
		buf[ret] = 0;
		dprintf("%s", buf);
		idx += ret;
	}
}

static void do_help(struct kdbg_ctxt *ctxt)
{
	dprintf("Kernel Debugger commands:\n");
	dprintf(" ps            Process list\n");
	dprintf(" sysrq         sysrq options\n");
	dprintf(" sysrq <param> Execute sysrq with <param>\n");
}

int kernel_debugger(struct kdbg_ctxt *ctxt, char *cmd)
{
	if (!strcmp(cmd, "ps"))
		do_ps(ctxt);
	if (!strcmp(cmd, "sysrq"))
		do_sysrq(ctxt, 'h');
	if (!strncmp(cmd, "sysrq ", 6))
		do_sysrq(ctxt, cmd[6]);
	if (!strcmp(cmd, "help"))
		do_help(ctxt);

	return 0;
}

