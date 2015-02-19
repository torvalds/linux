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

/* This keeps a track of which one is crashing cpu. */
static int crashing_cpu = -1;
static cpumask_t cpus_in_crash = CPU_MASK_NONE;

#ifdef CONFIG_SMP
static void crash_shutdown_secondary(void *ignore)
{
	struct pt_regs *regs;
	int cpu = smp_processor_id();

	regs = task_pt_regs(current);

	if (!cpu_online(cpu))
		return;

	local_irq_disable();
	if (!cpu_isset(cpu, cpus_in_crash))
		crash_save_cpu(regs, cpu);
	cpu_set(cpu, cpus_in_crash);

	while (!atomic_read(&kexec_ready_to_reboot))
		cpu_relax();
	relocated_kexec_smp_wait(NULL);
	/* NOTREACHED */
}

static void crash_kexec_prepare_cpus(void)
{
	unsigned int msecs;

	unsigned int ncpus = num_online_cpus() - 1;/* Excluding the panic cpu */

	dump_send_ipi(crash_shutdown_secondary);
	smp_wmb();

	/*
	 * The crash CPU sends an IPI and wait for other CPUs to
	 * respond. Delay of at least 10 seconds.
	 */
	pr_emerg("Sending IPI to other cpus...\n");
	msecs = 10000;
	while ((cpus_weight(cpus_in_crash) < ncpus) && (--msecs > 0)) {
		cpu_relax();
		mdelay(1);
	}
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
	cpu_set(crashing_cpu, cpus_in_crash);
}
