/*
 * SMP stuff which is common to all sub-architectures.
 */
#include <linux/module.h>
#include <asm/smp.h>

DEFINE_PER_CPU(unsigned long, this_cpu_off);
EXPORT_PER_CPU_SYMBOL(this_cpu_off);

/* Initialize the CPU's GDT.  This is either the boot CPU doing itself
   (still using the master per-cpu area), or a CPU doing it for a
   secondary which will soon come up. */
__cpuinit void init_gdt(int cpu)
{
	struct desc_struct *gdt = get_cpu_gdt_table(cpu);

	pack_descriptor((u32 *)&gdt[GDT_ENTRY_PERCPU].a,
			(u32 *)&gdt[GDT_ENTRY_PERCPU].b,
			__per_cpu_offset[cpu], 0xFFFFF,
			0x80 | DESCTYPE_S | 0x2, 0x8);

	per_cpu(this_cpu_off, cpu) = __per_cpu_offset[cpu];
	per_cpu(cpu_number, cpu) = cpu;
}


/**
 * smp_call_function(): Run a function on all other CPUs.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @nonatomic: Unused.
 * @wait: If true, wait (atomically) until function has completed on other CPUs.
 *
 * Returns 0 on success, else a negative status code.
 *
 * If @wait is true, then returns once @func has returned; otherwise
 * it returns just before the target cpu calls @func.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int smp_call_function(void (*func) (void *info), void *info, int nonatomic,
		      int wait)
{
	return smp_call_function_mask(cpu_online_map, func, info, wait);
}
EXPORT_SYMBOL(smp_call_function);

/**
 * smp_call_function_single - Run a function on a specific CPU
 * @cpu: The target CPU.  Cannot be the calling CPU.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @nonatomic: Unused.
 * @wait: If true, wait until function has completed on other CPUs.
 *
 * Returns 0 on success, else a negative status code.
 *
 * If @wait is true, then returns once @func has returned; otherwise
 * it returns just before the target cpu calls @func.
 */
int smp_call_function_single(int cpu, void (*func) (void *info), void *info,
			     int nonatomic, int wait)
{
	/* prevent preemption and reschedule on another processor */
	int ret;
	int me = get_cpu();
	if (cpu == me) {
		local_irq_disable();
		func(info);
		local_irq_enable();
		put_cpu();
		return 0;
	}

	ret = smp_call_function_mask(cpumask_of_cpu(cpu), func, info, wait);

	put_cpu();
	return ret;
}
EXPORT_SYMBOL(smp_call_function_single);
