#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/bootmem.h>
#include <linux/crash_dump.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>

/* This keeps a track of which one is crashing cpu. */
static int crashing_cpu = -1;
static cpumask_t cpus_in_crash = CPU_MASK_NONE;

#ifdef CONFIG_SMP
static void crash_shutdown_secondary(void *passed_regs)
{
	struct pt_regs *regs = passed_regs;
	int cpu = smp_processor_id();

	/*
	 * If we are passed registers, use those.  Otherwise get the
	 * regs from the last interrupt, which should be correct, as
	 * we are in an interrupt.  But if the regs are not there,
	 * pull them from the top of the stack.  They are probably
	 * wrong, but we need something to keep from crashing again.
	 */
	if (!regs)
		regs = get_irq_regs();
	if (!regs)
		regs = task_pt_regs(current);

	if (!cpu_online(cpu))
		return;

	local_irq_disable();
	if (!cpumask_test_cpu(cpu, &cpus_in_crash))
		crash_save_cpu(regs, cpu);
	cpumask_set_cpu(cpu, &cpus_in_crash);

	while (!atomic_read(&kexec_ready_to_reboot))
		cpu_relax();
	relocated_kexec_smp_wait(NULL);
	/* NOTREACHED */
}

static void crash_kexec_prepare_cpus(void)
{
	static int cpus_stopped;
	unsigned int msecs;
	unsigned int ncpus;

	if (cpus_stopped)
		return;

	ncpus = num_online_cpus() - 1;/* Excluding the panic cpu */

	smp_call_function(crash_shutdown_secondary, NULL, 0);
	smp_wmb();

	/*
	 * The crash CPU sends an IPI and wait for other CPUs to
	 * respond. Delay of at least 10 seconds.
	 */
	pr_emerg("Sending IPI to other cpus...\n");
	msecs = 10000;
	while ((cpumask_weight(&cpus_in_crash) < ncpus) && (--msecs > 0)) {
		cpu_relax();
		mdelay(1);
	}

	cpus_stopped = 1;
}

/* Override the weak function in kernel/panic.c */
void crash_smp_send_stop(void)
{
	if (_crash_smp_send_stop)
		_crash_smp_send_stop();

	crash_kexec_prepare_cpus();
}

#else /* !defined(CONFIG_SMP)  */
static void crash_kexec_prepare_cpus(void) {}
#endif /* !defined(CONFIG_SMP)	*/

void default_machine_crash_shutdown(struct pt_regs *regs)
{
	local_irq_disable();
	crashing_cpu = smp_processor_id();
	crash_save_cpu(regs, crashing_cpu);
	crash_kexec_prepare_cpus();
	cpumask_set_cpu(crashing_cpu, &cpus_in_crash);
}
