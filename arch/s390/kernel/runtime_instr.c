/*
 * Copyright IBM Corp. 2012
 * Author(s): Jan Glauber <jang@linux.vnet.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <asm/runtime_instr.h>
#include <asm/cpu_mf.h>
#include <asm/irq.h>

/* empty control block to disable RI by loading it */
struct runtime_instr_cb runtime_instr_empty_cb;

static void disable_runtime_instr(void)
{
	struct pt_regs *regs = task_pt_regs(current);

	load_runtime_instr_cb(&runtime_instr_empty_cb);

	/*
	 * Make sure the RI bit is deleted from the PSW. If the user did not
	 * switch off RI before the system call the process will get a
	 * specification exception otherwise.
	 */
	regs->psw.mask &= ~PSW_MASK_RI;
}

static void init_runtime_instr_cb(struct runtime_instr_cb *cb)
{
	cb->buf_limit = 0xfff;
	cb->pstate = 1;
	cb->pstate_set_buf = 1;
	cb->pstate_sample = 1;
	cb->pstate_collect = 1;
	cb->key = PAGE_DEFAULT_KEY;
	cb->valid = 1;
}

void exit_thread_runtime_instr(void)
{
	struct task_struct *task = current;

	preempt_disable();
	if (!task->thread.ri_cb)
		return;
	disable_runtime_instr();
	kfree(task->thread.ri_cb);
	task->thread.ri_cb = NULL;
	preempt_enable();
}

SYSCALL_DEFINE1(s390_runtime_instr, int, command)
{
	struct runtime_instr_cb *cb;

	if (!test_facility(64))
		return -EOPNOTSUPP;

	if (command == S390_RUNTIME_INSTR_STOP) {
		exit_thread_runtime_instr();
		return 0;
	}

	if (command != S390_RUNTIME_INSTR_START)
		return -EINVAL;

	if (!current->thread.ri_cb) {
		cb = kzalloc(sizeof(*cb), GFP_KERNEL);
		if (!cb)
			return -ENOMEM;
	} else {
		cb = current->thread.ri_cb;
		memset(cb, 0, sizeof(*cb));
	}

	init_runtime_instr_cb(cb);

	/* now load the control block to make it available */
	preempt_disable();
	current->thread.ri_cb = cb;
	load_runtime_instr_cb(cb);
	preempt_enable();
	return 0;
}
