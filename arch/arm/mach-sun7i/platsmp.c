/*
 *  linux/arch/arm/mach-sun7i/platsmp.c
 *
 *  Copyright (C) 2012-2016 Allwinner Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>

#include <mach/includes.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/smp_scu.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>

#include "core.h"

extern void sun7i_secondary_startup(void);

static DEFINE_SPINLOCK(boot_lock);

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 *
 * Note: for arch/arm/kernel/setup.csetup_arch(..)
 */
static void __iomem *scu_base_addr(void)
{
    pr_debug("[%s] enter\n", __FUNCTION__);
    return __io_address(SW_PA_SCU_IO_BASE);
}


void enable_aw_cpu(int cpu)
{
    long paddr;
    u32 pwr_reg;

    paddr = virt_to_phys(sun7i_secondary_startup);
    writel(paddr, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPUCFG_P_REG0);

    /* step1: Assert nCOREPORESET LOW and hold L1RSTDISABLE LOW.
              Ensure DBGPWRDUP is held LOW to prevent any external
              debug access to the processor.
    */
    /* assert cpu core reset */
    writel(0, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + CPUX_RESET_CTL(cpu));
    /* L1RSTDISABLE hold low */
    pwr_reg = readl(IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPUCFG_GENCTL);
    pwr_reg &= ~(1<<cpu);
    writel(pwr_reg, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPUCFG_GENCTL);
    /* DBGPWRDUP hold low */
    pwr_reg = readl(IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPUCFG_DBGCTL1);
    pwr_reg &= ~(1<<cpu);
    writel(pwr_reg, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPUCFG_DBGCTL1);

    /* step2: release power clamp */
    writel(0xff, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWR_CLAMP);
    writel(0x7f, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWR_CLAMP);
    writel(0x3f, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWR_CLAMP);
    writel(0x1f, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWR_CLAMP);
    writel(0x0f, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWR_CLAMP);
    writel(0x07, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWR_CLAMP);
    writel(0x03, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWR_CLAMP);
    writel(0x01, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWR_CLAMP);
    writel(0x00, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWR_CLAMP);
    mdelay(10);

    /* step3: clear power-off gating */
    pwr_reg = readl(IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWROFF_REG);
    pwr_reg &= ~(1);
    writel(pwr_reg, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPU1_PWROFF_REG);
    mdelay(1);

    /* step4: de-assert core reset */
    writel(3, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + CPUX_RESET_CTL(cpu));

    /* step5: assert DBGPWRDUP signal */
    pwr_reg = readl(IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPUCFG_DBGCTL1);
    pwr_reg |= (1<<cpu);
    writel(pwr_reg, IO_ADDRESS(SW_PA_CPUCFG_IO_BASE) + AW_CPUCFG_DBGCTL1);
}

void __init smp_init_cpus(void)
{
    unsigned int i, ncores;

    ncores =  scu_get_core_count(NULL);
    pr_debug("[%s] ncores=%d\n", __FUNCTION__, ncores);

    for (i = 0; i < ncores; i++) {
        set_cpu_possible(i, true);
    }

    set_smp_cross_call(gic_raise_softirq);
}

/*
 * for arch/arm/kernel/smp.c:smp_prepare_cpus(unsigned int max_cpus)
 */
void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
    void __iomem *scu_base;

    pr_debug("[%s] enter\n", __FUNCTION__);
    scu_base = scu_base_addr();
    scu_enable(scu_base);
}

/*
 * for linux/arch/arm/kernel/smp.c:secondary_start_kernel(void)
 */
void __cpuinit platform_secondary_init(unsigned int cpu)
{
    pr_debug("[%s] enter, cpu:%d\n", __FUNCTION__, cpu);
    gic_secondary_init(0);
}

/*
 * for linux/arch/arm/kernel/smp.c:__cpu_up(..)
 */
int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
    pr_debug("[%s] enter\n", __FUNCTION__);
    spin_lock(&boot_lock);
    enable_aw_cpu(cpu);
    spin_unlock(&boot_lock);
    return 0;
}


