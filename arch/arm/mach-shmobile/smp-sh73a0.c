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
#include <mach/common.h>
#include <asm/smp_scu.h>
#include <asm/smp_twd.h>
#include <asm/hardware/gic.h>

#define WUPCR		0xe6151010
#define SRESCR		0xe6151018
#define PSTR		0xe6151040
#define SBAR            0xe6180020
#define APARMBAREA      0xe6f10020

static void __iomem *scu_base_addr(void)
{
	return (void __iomem *)0xf0000000;
}

static DEFINE_SPINLOCK(scu_lock);
static unsigned long tmp;

static void modify_scu_cpu_psr(unsigned long set, unsigned long clr)
{
	void __iomem *scu_base = scu_base_addr();

	spin_lock(&scu_lock);
	tmp = __raw_readl(scu_base + 8);
	tmp &= ~clr;
	tmp |= set;
	spin_unlock(&scu_lock);

	/* disable cache coherency after releasing the lock */
	__raw_writel(tmp, scu_base + 8);
}

unsigned int __init sh73a0_get_core_count(void)
{
	void __iomem *scu_base = scu_base_addr();

	return scu_get_core_count(scu_base);
}

void __cpuinit sh73a0_secondary_init(unsigned int cpu)
{
	gic_secondary_init(0);
}

int __cpuinit sh73a0_boot_secondary(unsigned int cpu)
{
	/* enable cache coherency */
	modify_scu_cpu_psr(0, 3 << (cpu * 8));

	if (((__raw_readw(__io(PSTR)) >> (4 * cpu)) & 3) == 3)
		__raw_writel(1 << cpu, __io(WUPCR));	/* wake up */
	else
		__raw_writel(1 << cpu, __io(SRESCR));	/* reset */

	return 0;
}

void __init sh73a0_smp_prepare_cpus(void)
{
#ifdef CONFIG_HAVE_ARM_TWD
	twd_base = (void __iomem *)0xf0000600;
#endif

	scu_enable(scu_base_addr());

	/* Map the reset vector (in headsmp.S) */
	__raw_writel(0, __io(APARMBAREA));      /* 4k */
	__raw_writel(__pa(shmobile_secondary_vector), __io(SBAR));

	/* enable cache coherency on CPU0 */
	modify_scu_cpu_psr(0, 3 << (0 * 8));
}
