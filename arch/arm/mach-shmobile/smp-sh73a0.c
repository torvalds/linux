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
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <mach/common.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <mach/sh73a0.h>
#include <asm/smp_scu.h>
#include <asm/smp_twd.h>

#define WUPCR		IOMEM(0xe6151010)
#define SRESCR		IOMEM(0xe6151018)
#define PSTR		IOMEM(0xe6151040)
#define SBAR		IOMEM(0xe6180020)
#define APARMBAREA	IOMEM(0xe6f10020)

#define PSTR_SHUTDOWN_MODE	3

#define SH73A0_SCU_BASE 0xf0000000

#ifdef CONFIG_HAVE_ARM_TWD
static DEFINE_TWD_LOCAL_TIMER(twd_local_timer, SH73A0_SCU_BASE + 0x600, 29);
void __init sh73a0_register_twd(void)
{
	twd_local_timer_register(&twd_local_timer);
}
#endif

static int sh73a0_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	cpu = cpu_logical_map(cpu);

	if (((__raw_readl(PSTR) >> (4 * cpu)) & 3) == 3)
		__raw_writel(1 << cpu, WUPCR);	/* wake up */
	else
		__raw_writel(1 << cpu, SRESCR);	/* reset */

	return 0;
}

static void __init sh73a0_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(shmobile_scu_base);

	/* Map the reset vector (in headsmp-scu.S, headsmp.S) */
	__raw_writel(0, APARMBAREA);      /* 4k */
	__raw_writel(__pa(shmobile_boot_vector), SBAR);
	shmobile_boot_fn = virt_to_phys(shmobile_boot_scu);
	shmobile_boot_arg = (unsigned long)shmobile_scu_base;

	/* enable cache coherency on booting CPU */
	scu_power_mode(shmobile_scu_base, SCU_PM_NORMAL);
}

static void __init sh73a0_smp_init_cpus(void)
{
	/* setup sh73a0 specific SCU base */
	shmobile_scu_base = IOMEM(SH73A0_SCU_BASE);

	shmobile_smp_init_cpus(scu_get_core_count(shmobile_scu_base));
}

#ifdef CONFIG_HOTPLUG_CPU
static int sh73a0_cpu_kill(unsigned int cpu)
{

	int k;
	u32 pstr;

	/*
	 * wait until the power status register confirms the shutdown of the
	 * offline target
	 */
	for (k = 0; k < 1000; k++) {
		pstr = (__raw_readl(PSTR) >> (4 * cpu)) & 3;
		if (pstr == PSTR_SHUTDOWN_MODE)
			return 1;

		mdelay(1);
	}

	return 0;
}

static void sh73a0_cpu_die(unsigned int cpu)
{
	/* Set power off mode. This takes the CPU out of the MP cluster */
	scu_power_mode(shmobile_scu_base, SCU_PM_POWEROFF);

	/* Enter shutdown mode */
	cpu_do_idle();
}

static int sh73a0_cpu_disable(unsigned int cpu)
{
	return 0; /* CPU0 and CPU1 supported */
}
#endif /* CONFIG_HOTPLUG_CPU */

struct smp_operations sh73a0_smp_ops __initdata = {
	.smp_init_cpus		= sh73a0_smp_init_cpus,
	.smp_prepare_cpus	= sh73a0_smp_prepare_cpus,
	.smp_boot_secondary	= sh73a0_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= sh73a0_cpu_kill,
	.cpu_die		= sh73a0_cpu_die,
	.cpu_disable		= sh73a0_cpu_disable,
#endif
};
