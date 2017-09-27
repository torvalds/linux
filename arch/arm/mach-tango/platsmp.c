#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include "smc.h"

static int tango_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	tango_set_aux_boot_addr(__pa_symbol(secondary_startup));
	tango_start_aux_core(cpu);
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * cpu_kill() and cpu_die() run concurrently on different cores.
 * Firmware will only "kill" a core once it has properly "died".
 * Try a few times to kill a core before giving up, and sleep
 * between tries to give that core enough time to die.
 */
static int tango_cpu_kill(unsigned int cpu)
{
	int i, err;

	for (i = 0; i < 10; ++i) {
		msleep(10);
		err = tango_aux_core_kill(cpu);
		if (!err)
			return true;
	}

	return false;
}

static void tango_cpu_die(unsigned int cpu)
{
	while (tango_aux_core_die(cpu) < 0)
		cpu_relax();

	panic("cpu %d failed to die\n", cpu);
}
#endif

static const struct smp_operations tango_smp_ops __initconst = {
	.smp_boot_secondary	= tango_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= tango_cpu_kill,
	.cpu_die		= tango_cpu_die,
#endif
};

CPU_METHOD_OF_DECLARE(tango4_smp, "sigma,tango4-smp", &tango_smp_ops);
