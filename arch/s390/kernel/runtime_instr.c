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

static int runtime_instr_avail(void)
{
	return test_facility(64);
}

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
	if (s390_user_mode == HOME_SPACE_MODE)
		cb->home_space = 1;
	cb->int_requested = 1;
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

	if (!task->thread.ri_cb)
		return;
	disable_runtime_instr();
	kfree(task->thread.ri_cb);
	task->thread.ri_signum = 0;
	task->thread.ri_cb = NULL;
}

static void runtime_instr_int_handler(struct ext_code ext_code,
				unsigned int param32, unsigned long param64)
{
	struct siginfo info;

	if (!(param32 & CPU_MF_INT_RI_MASK))
		return;

	inc_irq_stat(IRQEXT_CMR);

	if (!current->thread.ri_cb)
		return;
	if (current->thread.ri_signum < SIGRTMIN ||
	    current->thread.ri_signum > SIGRTMAX) {
		WARN_ON_ONCE(1);
		return;
	}

	memset(&info, 0, sizeof(info));
	info.si_signo = current->thread.ri_signum;
	info.si_code = SI_QUEUE;
	if (param32 & CPU_MF_INT_RI_BUF_FULL)
		info.si_int = ENOBUFS;
	else if (param32 & CPU_MF_INT_RI_HALTED)
		info.si_int = ECANCELED;
	else
		return; /* unknown reason */

	send_sig_info(current->thread.ri_signum, &info, current);
}

SYSCALL_DEFINE2(s390_runtime_instr, int, command, int, signum)
{
	struct runtime_instr_cb *cb;

	if (!runtime_instr_avail())
		return -EOPNOTSUPP;

	if (command == S390_RUNTIME_INSTR_STOP) {
		preempt_disable();
		exit_thread_runtime_instr();
		preempt_enable();
		return 0;
	}

	if (command != S390_RUNTIME_INSTR_START ||
	    (signum < SIGRTMIN || signum > SIGRTMAX))
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
	current->thread.ri_signum = signum;

	/* now load the control block to make it available */
	preempt_disable();
	current->thread.ri_cb = cb;
	load_runtime_instr_cb(cb);
	preempt_enable();
	return 0;
}

static int __init runtime_instr_init(void)
{
	int rc;

	if (!runtime_instr_avail())
		return 0;

	irq_subclass_register(IRQ_SUBCLASS_MEASUREMENT_ALERT);
	rc = register_external_interrupt(0x1407, runtime_instr_int_handler);
	if (rc)
		irq_subclass_unregister(IRQ_SUBCLASS_MEASUREMENT_ALERT);
	else
		pr_info("Runtime instrumentation facility initialized\n");
	return rc;
}
device_initcall(runtime_instr_init);
