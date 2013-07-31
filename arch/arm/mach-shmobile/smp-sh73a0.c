/*
 * SMP support for R-Mobile / SH-Mobile - sh73a0 portion
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2010  Takashi Yoshii
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <mach/common.h>
#include <mach/sh73a0.h>
#include <asm/smp_plat.h>
#include <asm/smp_twd.h>

#define WUPCR		IOMEM(0xe6151010)
#define SRESCR		IOMEM(0xe6151018)
#define PSTR		IOMEM(0xe6151040)
#define SBAR		IOMEM(0xe6180020)
#define APARMBAREA	IOMEM(0xe6f10020)

#define SH73A0_SCU_BASE 0xf0000000

#ifdef CONFIG_HAVE_ARM_TWD
static DEFINE_TWD_LOCAL_TIMER(twd_local_timer, SH73A0_SCU_BASE + 0x600, 29);
void __init sh73a0_register_twd(void)
{
	twd_local_timer_register(&twd_local_timer);
}
#endif

static int __cpuinit sh73a0_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned int lcpu = cpu_logical_map(cpu);
	int ret;

	ret = shmobile_smp_scu_boot_secondary(cpu, idle);
	if (ret)
		return ret;

	if (((__raw_readl(PSTR) >> (4 * lcpu)) & 3) == 3)
		__raw_writel(1 << lcpu, WUPCR);	/* wake up */
	else
		__raw_writel(1 << lcpu, SRESCR);	/* reset */

	return 0;
}

static void __init sh73a0_smp_prepare_cpus(unsigned int max_cpus)
{
	/* Map the reset vector (in headsmp.S) */
	__raw_writel(0, APARMBAREA);      /* 4k */
	__raw_writel(__pa(shmobile_boot_vector), SBAR);

	/* setup sh73a0 specific SCU bits */
	shmobile_scu_base = IOMEM(SH73A0_SCU_BASE);
	shmobile_smp_scu_prepare_cpus(max_cpus);
}

#ifdef CONFIG_HOTPLUG_CPU
static int sh73a0_cpu_disable(unsigned int cpu)
{
	return 0; /* CPU0 and CPU1 supported */
}
#endif /* CONFIG_HOTPLUG_CPU */

struct smp_operations sh73a0_smp_ops __initdata = {
	.smp_prepare_cpus	= sh73a0_smp_prepare_cpus,
	.smp_boot_secondary	= sh73a0_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= sh73a0_cpu_disable,
	.cpu_die		= shmobile_smp_scu_cpu_die,
	.cpu_kill		= shmobile_smp_scu_cpu_kill,
#endif
};
