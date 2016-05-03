#include <linux/init.h>
#include <linux/smp.h>
#include "smc.h"

static int tango_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	tango_set_aux_boot_addr(virt_to_phys(secondary_startup));
	tango_start_aux_core(cpu);
	return 0;
}

static const struct smp_operations tango_smp_ops __initconst = {
	.smp_boot_secondary	= tango_boot_secondary,
};

CPU_METHOD_OF_DECLARE(tango4_smp, "sigma,tango4-smp", &tango_smp_ops);
