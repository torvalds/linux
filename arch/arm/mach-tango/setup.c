#include <linux/smp.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>
#include "smc.h"

static int tango4_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	tango_set_aux_boot_addr(virt_to_phys(secondary_startup));
	tango_start_aux_core(cpu);
	return 0;
}

static struct smp_operations tango4_smp_ops __initdata = {
	.smp_boot_secondary	= tango4_boot_secondary,
};

CPU_METHOD_OF_DECLARE(tango4_smp, "sigma,tango4-smp", &tango4_smp_ops);

static void tango_l2c_write(unsigned long val, unsigned int reg)
{
	if (reg == L2X0_CTRL)
		tango_set_l2_control(val);
}

static const char *const tango_dt_compat[] = { "sigma,tango4", NULL };

DT_MACHINE_START(TANGO_DT, "Sigma Tango DT")
	.dt_compat	= tango_dt_compat,
	.l2c_aux_mask	= ~0,
	.l2c_write_sec	= tango_l2c_write,
MACHINE_END
