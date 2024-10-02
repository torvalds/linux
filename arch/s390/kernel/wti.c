// SPDX-License-Identifier: GPL-2.0
/*
 * Support for warning track interruption
 *
 * Copyright IBM Corp. 2023
 */

#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/kallsyms.h>
#include <linux/smpboot.h>
#include <linux/irq.h>
#include <uapi/linux/sched/types.h>
#include <asm/debug.h>
#include <asm/diag.h>
#include <asm/sclp.h>

#define WTI_DBF_LEN 64

struct wti_debug {
	unsigned long	missed;
	unsigned long	addr;
	pid_t		pid;
};

struct wti_state {
	/* debug data for s390dbf */
	struct wti_debug	dbg;
	/*
	 * Represents the real-time thread responsible to
	 * acknowledge the warning-track interrupt and trigger
	 * preliminary and postliminary precautions.
	 */
	struct task_struct	*thread;
	/*
	 * If pending is true, the real-time thread must be scheduled.
	 * If not, a wake up of that thread will remain a noop.
	 */
	bool			pending;
};

static DEFINE_PER_CPU(struct wti_state, wti_state);

static debug_info_t *wti_dbg;

/*
 * During a warning-track grace period, interrupts are disabled
 * to prevent delays of the warning-track acknowledgment.
 *
 * Once the CPU is physically dispatched again, interrupts are
 * re-enabled.
 */

static void wti_irq_disable(void)
{
	unsigned long flags;
	struct ctlreg cr6;

	local_irq_save(flags);
	local_ctl_store(6, &cr6);
	/* disable all I/O interrupts */
	cr6.val &= ~0xff000000UL;
	local_ctl_load(6, &cr6);
	local_irq_restore(flags);
}

static void wti_irq_enable(void)
{
	unsigned long flags;
	struct ctlreg cr6;

	local_irq_save(flags);
	local_ctl_store(6, &cr6);
	/* enable all I/O interrupts */
	cr6.val |= 0xff000000UL;
	local_ctl_load(6, &cr6);
	local_irq_restore(flags);
}

static void store_debug_data(struct wti_state *st)
{
	struct pt_regs *regs = get_irq_regs();

	st->dbg.pid = current->pid;
	st->dbg.addr = 0;
	if (!user_mode(regs))
		st->dbg.addr = regs->psw.addr;
}

static void wti_interrupt(struct ext_code ext_code,
			  unsigned int param32, unsigned long param64)
{
	struct wti_state *st = this_cpu_ptr(&wti_state);

	inc_irq_stat(IRQEXT_WTI);
	wti_irq_disable();
	store_debug_data(st);
	st->pending = true;
	wake_up_process(st->thread);
}

static int wti_pending(unsigned int cpu)
{
	struct wti_state *st = per_cpu_ptr(&wti_state, cpu);

	return st->pending;
}

static void wti_dbf_grace_period(struct wti_state *st)
{
	struct wti_debug *wdi = &st->dbg;
	char buf[WTI_DBF_LEN];

	if (wdi->addr)
		snprintf(buf, sizeof(buf), "%d %pS", wdi->pid, (void *)wdi->addr);
	else
		snprintf(buf, sizeof(buf), "%d <user>", wdi->pid);
	debug_text_event(wti_dbg, 2, buf);
	wdi->missed++;
}

static int wti_show(struct seq_file *seq, void *v)
{
	struct wti_state *st;
	int cpu;

	cpus_read_lock();
	seq_puts(seq, "       ");
	for_each_online_cpu(cpu)
		seq_printf(seq, "CPU%-8d", cpu);
	seq_putc(seq, '\n');
	for_each_online_cpu(cpu) {
		st = per_cpu_ptr(&wti_state, cpu);
		seq_printf(seq, " %10lu", st->dbg.missed);
	}
	seq_putc(seq, '\n');
	cpus_read_unlock();
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wti);

static void wti_thread_fn(unsigned int cpu)
{
	struct wti_state *st = per_cpu_ptr(&wti_state, cpu);

	st->pending = false;
	/*
	 * Yield CPU voluntarily to the hypervisor. Control
	 * resumes when hypervisor decides to dispatch CPU
	 * to this LPAR again.
	 */
	if (diag49c(DIAG49C_SUBC_ACK))
		wti_dbf_grace_period(st);
	wti_irq_enable();
}

static struct smp_hotplug_thread wti_threads = {
	.store			= &wti_state.thread,
	.thread_should_run	= wti_pending,
	.thread_fn		= wti_thread_fn,
	.thread_comm		= "cpuwti/%u",
	.selfparking		= false,
};

static int __init wti_init(void)
{
	struct sched_param wti_sched_param = { .sched_priority = MAX_RT_PRIO - 1 };
	struct dentry *wti_dir;
	struct wti_state *st;
	int cpu, rc;

	rc = -EOPNOTSUPP;
	if (!sclp.has_wti)
		goto out;
	rc = smpboot_register_percpu_thread(&wti_threads);
	if (WARN_ON(rc))
		goto out;
	for_each_online_cpu(cpu) {
		st = per_cpu_ptr(&wti_state, cpu);
		sched_setscheduler(st->thread, SCHED_FIFO, &wti_sched_param);
	}
	rc = register_external_irq(EXT_IRQ_WARNING_TRACK, wti_interrupt);
	if (rc) {
		pr_warn("Couldn't request external interrupt 0x1007\n");
		goto out_thread;
	}
	irq_subclass_register(IRQ_SUBCLASS_WARNING_TRACK);
	rc = diag49c(DIAG49C_SUBC_REG);
	if (rc) {
		pr_warn("Failed to register warning track interrupt through DIAG 49C\n");
		rc = -EOPNOTSUPP;
		goto out_subclass;
	}
	wti_dir = debugfs_create_dir("wti", arch_debugfs_dir);
	debugfs_create_file("stat", 0400, wti_dir, NULL, &wti_fops);
	wti_dbg = debug_register("wti", 1, 1, WTI_DBF_LEN);
	if (!wti_dbg) {
		rc = -ENOMEM;
		goto out_debug_register;
	}
	rc = debug_register_view(wti_dbg, &debug_hex_ascii_view);
	if (rc)
		goto out_debug_register;
	goto out;
out_debug_register:
	debug_unregister(wti_dbg);
out_subclass:
	irq_subclass_unregister(IRQ_SUBCLASS_WARNING_TRACK);
	unregister_external_irq(EXT_IRQ_WARNING_TRACK, wti_interrupt);
out_thread:
	smpboot_unregister_percpu_thread(&wti_threads);
out:
	return rc;
}
late_initcall(wti_init);
