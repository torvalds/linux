/*
 * Device Tree support for Rockchip RK3036
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/cpuidle.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/rockchip/common.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/dvfs.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/pmu.h>
#include <asm/cpuidle.h>
#include <asm/cputype.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "cpu_axi.h"
#include "loader.h"
#define CPU 3036
#include "sram.h"
#include "pm.h"

#define RK3036_DEVICE(name) \
	{ \
		.virtual	= (unsigned long) RK_##name##_VIRT, \
		.pfn		= __phys_to_pfn(RK3036_##name##_PHYS), \
		.length		= RK3036_##name##_SIZE, \
		.type		= MT_DEVICE, \
	}

#define RK3036_IMEM_VIRT (RK_BOOTRAM_VIRT + SZ_32K)
#define RK3036_TIMER5_VIRT (RK_TIMER_VIRT + 0xa0)


static struct map_desc rk3036_io_desc[] __initdata = {
    RK3036_DEVICE(CRU),
    RK3036_DEVICE(GRF),
    RK3036_DEVICE(ROM),
    RK3036_DEVICE(EFUSE),
    RK_DEVICE(RK_DDR_VIRT, RK3036_DDR_PCTL_PHYS, RK3036_DDR_PCTL_SIZE),
    RK_DEVICE(RK_DDR_VIRT + RK3036_DDR_PCTL_SIZE, RK3036_DDR_PHY_PHYS, RK3036_DDR_PHY_SIZE),
    RK_DEVICE(RK_GPIO_VIRT(0), RK3036_GPIO0_PHYS, RK3036_GPIO_SIZE),
    RK_DEVICE(RK_GPIO_VIRT(1), RK3036_GPIO1_PHYS, RK3036_GPIO_SIZE),
    RK_DEVICE(RK_GPIO_VIRT(2), RK3036_GPIO2_PHYS, RK3036_GPIO_SIZE),
    RK_DEVICE(RK_DEBUG_UART_VIRT, RK3036_UART2_PHYS, RK3036_UART_SIZE),
    RK_DEVICE(RK_GIC_VIRT, RK3036_GIC_DIST_PHYS, RK3036_GIC_DIST_SIZE),
    RK_DEVICE(RK_GIC_VIRT + RK3036_GIC_DIST_SIZE,RK3036_GIC_CPU_PHYS, RK3036_GIC_CPU_SIZE),
    RK_DEVICE(RK3036_IMEM_VIRT, RK3036_IMEM_PHYS, SZ_4K),
    RK_DEVICE(RK_TIMER_VIRT, RK3036_TIMER_PHYS, RK3036_TIMER_SIZE),
};

static void __init rk3036_boot_mode_init(void)
{
    u32 flag = readl_relaxed(RK_GRF_VIRT + RK3036_GRF_OS_REG0);
    u32 mode = readl_relaxed(RK_GRF_VIRT + RK3036_GRF_OS_REG1);
    u32 rst_st = readl_relaxed(RK_CRU_VIRT + RK3036_CRU_RST_ST);

    if (flag == (SYS_KERNRL_REBOOT_FLAG | BOOT_RECOVER))
        mode = BOOT_MODE_RECOVERY;
    if (rst_st & ((1 << 2) | (1 << 3)))
        mode = BOOT_MODE_WATCHDOG;
    rockchip_boot_mode_init(flag, mode);
}

#if 0
static void usb_uart_init(void)
{
#ifdef CONFIG_RK_USB_UART
    u32 soc_status0 = readl_relaxed(RK_GRF_VIRT + RK3036_GRF_SOC_STATUS0);
#endif
    writel_relaxed(0x34000000, RK_GRF_VIRT + RK3036_GRF_UOC1_CON4);
#ifdef CONFIG_RK_USB_UART
    if (!(soc_status0 & (1 << 14)) && (soc_status0 & (1 << 17))) {
            /* software control usb phy enable */
            writel_relaxed(0x007f0055, RK_GRF_VIRT + RK3036_GRF_UOC0_CON5);
            writel_relaxed(0x34003000, RK_GRF_VIRT + RK3036_GRF_UOC1_CON4);
    }
#endif
#ifdef RK_DEBUG_UART_VIRT
    writel_relaxed(0x07, RK_DEBUG_UART_VIRT + 0x88);
    writel_relaxed(0x07, RK_DEBUG_UART_VIRT + 0x88);
    writel_relaxed(0x00, RK_DEBUG_UART_VIRT + 0x04);
    writel_relaxed(0x83, RK_DEBUG_UART_VIRT + 0x0c);
    writel_relaxed(0x0d, RK_DEBUG_UART_VIRT + 0x00);
    writel_relaxed(0x00, RK_DEBUG_UART_VIRT + 0x04);
    writel_relaxed(0x03, RK_DEBUG_UART_VIRT + 0x0c);
#endif //end of DEBUG_UART_BASE
}
#endif

static void __init rk3036_dt_map_io(void)
{
    rockchip_soc_id = ROCKCHIP_SOC_RK3036;

    iotable_init(rk3036_io_desc, ARRAY_SIZE(rk3036_io_desc));
    debug_ll_io_init();
    //usb_uart_init();

    /* enable timer5 for core */
    writel_relaxed(0, RK3036_TIMER5_VIRT + 0x10);
    dsb();
    writel_relaxed(0xFFFFFFFF, RK3036_TIMER5_VIRT + 0x00);
    writel_relaxed(0xFFFFFFFF, RK3036_TIMER5_VIRT + 0x04);
    dsb();
    writel_relaxed(1, RK3036_TIMER5_VIRT + 0x10);
    dsb();

    rk3036_boot_mode_init();
}

extern void secondary_startup(void);
static int rk3036_sys_set_power_domain(enum pmu_power_domain pd, bool on)
{
    if (on) {
#ifdef CONFIG_SMP
        if(PD_CPU_1 == pd) {
            writel_relaxed(0x20000, RK_CRU_VIRT + RK3036_CRU_SOFTRST0_CON);
            dsb();
            udelay(10);
            writel_relaxed(virt_to_phys(secondary_startup), RK3036_IMEM_VIRT + 8);
            writel_relaxed(0xDEADBEAF, RK3036_IMEM_VIRT + 4);
            dsb_sev();
        }
#endif
    } else {
#ifdef CONFIG_SMP
        if(PD_CPU_1 == pd) {
            writel_relaxed(0x20002, RK_CRU_VIRT + RK3036_CRU_SOFTRST0_CON);
            dsb();
        }
#endif
    }

    return 0;
}

static bool rk3036_pmu_power_domain_is_on(enum pmu_power_domain pd)
{
    return 1;
}

static int rk3036_pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
    return 0;
}

static void __init rk3036_dt_init_timer(void)
{
    rockchip_pmu_ops.set_power_domain = rk3036_sys_set_power_domain;
    rockchip_pmu_ops.power_domain_is_on = rk3036_pmu_power_domain_is_on;
    rockchip_pmu_ops.set_idle_request = rk3036_pmu_set_idle_request;
    of_clk_init(NULL);
    clocksource_of_init();
}

static void __init rk3036_init_late(void)
{
    return;
}

static void __init rk3036_reserve(void)
{
    /* reserve memory for ION */
    //rockchip_ion_reserve();

    return;
}

static void rk3036_restart(char mode, const char *cmd)
{
    u32 boot_flag, boot_mode;

    rockchip_restart_get_boot_mode(cmd, &boot_flag, &boot_mode);

    writel_relaxed(boot_flag, RK_GRF_VIRT + RK3036_GRF_OS_REG0);	// for loader
    writel_relaxed(boot_mode, RK_GRF_VIRT + RK3036_GRF_OS_REG1);	// for linux
    dsb();

    /* pll enter slow mode */
    writel_relaxed(0x30110000, RK_CRU_VIRT + RK3036_CRU_MODE_CON);
    dsb();
    writel_relaxed(0xeca8, RK_CRU_VIRT + RK3036_CRU_GLB_SRST_SND_VALUE);
    dsb();
}

static const char * const rk3036_dt_compat[] __initconst = {
    "rockchip,rk3036",
    NULL,
};

DT_MACHINE_START(RK3036_DT, "Rockchip RK3036")
    .dt_compat	= rk3036_dt_compat,
    .smp		= smp_ops(rockchip_smp_ops),
    .reserve	= rk3036_reserve,
    .map_io		= rk3036_dt_map_io,
    .init_time	= rk3036_dt_init_timer,
    .init_late	= rk3036_init_late,
    .reserve	= rk3036_reserve,
    .restart	= rk3036_restart,
MACHINE_END
