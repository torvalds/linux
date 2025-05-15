// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2016
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/guarded_storage.h>
#include "entry.h"

void guarded_storage_release(struct task_struct *tsk)
{
	kfree(tsk->thread.gs_cb);
	kfree(tsk->thread.gs_bc_cb);
}

static int gs_enable(void)
{
	struct gs_cb *gs_cb;

	if (!current->thread.gs_cb) {
		gs_cb = kzalloc(sizeof(*gs_cb), GFP_KERNEL);
		if (!gs_cb)
			return -ENOMEM;
		gs_cb->gsd = 25;
		preempt_disable();
		local_ctl_set_bit(2, CR2_GUARDED_STORAGE_BIT);
		load_gs_cb(gs_cb);
		current->thread.gs_cb = gs_cb;
		preempt_enable();
	}
	return 0;
}

static int gs_disable(void)
{
	if (current->thread.gs_cb) {
		preempt_disable();
		kfree(current->thread.gs_cb);
		current->thread.gs_cb = NULL;
		local_ctl_clear_bit(2, CR2_GUARDED_STORAGE_BIT);
		preempt_enable();
	}
	return 0;
}

static int gs_set_bc_cb(struct gs_cb __user *u_gs_cb)
{
	struct gs_cb *gs_cb;

	gs_cb = current->thread.gs_bc_cb;
	if (!gs_cb) {
		gs_cb = kzalloc(sizeof(*gs_cb), GFP_KERNEL);
		if (!gs_cb)
			return -ENOMEM;
		current->thread.gs_bc_cb = gs_cb;
	}
	if (copy_from_user(gs_cb, u_gs_cb, sizeof(*gs_cb)))
		return -EFAULT;
	return 0;
}

static int gs_clear_bc_cb(void)
{
	struct gs_cb *gs_cb;

	gs_cb = current->thread.gs_bc_cb;
	current->thread.gs_bc_cb = NULL;
	kfree(gs_cb);
	return 0;
}

void gs_load_bc_cb(struct pt_regs *regs)
{
	struct gs_cb *gs_cb;

	preempt_disable();
	clear_thread_flag(TIF_GUARDED_STORAGE);
	gs_cb = current->thread.gs_bc_cb;
	if (gs_cb) {
		kfree(current->thread.gs_cb);
		current->thread.gs_bc_cb = NULL;
		local_ctl_set_bit(2, CR2_GUARDED_STORAGE_BIT);
		load_gs_cb(gs_cb);
		current->thread.gs_cb = gs_cb;
	}
	preempt_enable();
}

static int gs_broadcast(void)
{
	struct task_struct *sibling;

	read_lock(&tasklist_lock);
	for_each_thread(current, sibling) {
		if (!sibling->thread.gs_bc_cb)
			continue;
		if (test_and_set_tsk_thread_flag(sibling, TIF_GUARDED_STORAGE))
			kick_process(sibling);
	}
	read_unlock(&tasklist_lock);
	return 0;
}

SYSCALL_DEFINE2(s390_guarded_storage, int, command,
		struct gs_cb __user *, gs_cb)
{
	if (!cpu_has_gs())
		return -EOPNOTSUPP;
	switch (command) {
	case GS_ENABLE:
		return gs_enable();
	case GS_DISABLE:
		return gs_disable();
	case GS_SET_BC_CB:
		return gs_set_bc_cb(gs_cb);
	case GS_CLEAR_BC_CB:
		return gs_clear_bc_cb();
	case GS_BROADCAST:
		return gs_broadcast();
	default:
		return -EINVAL;
	}
}
