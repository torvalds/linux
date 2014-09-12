/*
 * Device Tree support for Rockchip RK3288
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
/*#include <asm/cpuidle.h>*/
#include <asm/cputype.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "cpu_axi.h"
#include "loader.h"
#define CPU 312x
#include "sram.h"
#include "pm.h"
#include "pm-rk312x.c"
#define RK312X_DEVICE(name) \
	{ \
		.virtual	= (unsigned long) RK_##name##_VIRT, \
		.pfn		= __phys_to_pfn(RK312X_##name##_PHYS), \
		.length		= RK312X_##name##_SIZE, \
		.type		= MT_DEVICE, \
	}

static const char * const rk3126_dt_compat[] __initconst = {
	"rockchip,rk3126",
	NULL,
};

static const char * const rk3128_dt_compat[] __initconst = {
	"rockchip,rk3128",
	NULL,
};

#define RK312X_IMEM_VIRT (RK_BOOTRAM_VIRT + SZ_32K)
#define RK312X_TIMER5_VIRT (RK_TIMER_VIRT + 0xa0)

static struct map_desc rk312x_io_desc[] __initdata = {
	RK312X_DEVICE(CRU),
	RK312X_DEVICE(GRF),
	RK312X_DEVICE(ROM),
	RK312X_DEVICE(PMU),
	RK312X_DEVICE(EFUSE),
	RK312X_DEVICE(TIMER),
	RK312X_DEVICE(CPU_AXI_BUS),
	RK_DEVICE(RK_DEBUG_UART_VIRT, RK312X_UART2_PHYS, RK312X_UART_SIZE),
	RK_DEVICE(RK_DDR_VIRT, RK312X_DDR_PCTL_PHYS, RK312X_DDR_PCTL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + RK312X_DDR_PCTL_SIZE, RK312X_DDR_PHY_PHYS, RK312X_DDR_PHY_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(0), RK312X_GPIO0_PHYS, RK312X_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(1), RK312X_GPIO1_PHYS, RK312X_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(2), RK312X_GPIO2_PHYS, RK312X_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(3), RK312X_GPIO3_PHYS, RK312X_GPIO_SIZE),
	RK_DEVICE(RK_GIC_VIRT, RK312X_GIC_DIST_PHYS, RK312X_GIC_DIST_SIZE),
	RK_DEVICE(RK_GIC_VIRT + RK312X_GIC_DIST_SIZE, RK312X_GIC_CPU_PHYS, RK312X_GIC_CPU_SIZE),
	RK_DEVICE(RK312X_IMEM_VIRT, RK312X_IMEM_PHYS, RK312X_IMEM_SIZE),
	RK_DEVICE(RK_PWM_VIRT, RK312X_PWM_PHYS, RK312X_PWM_SIZE),
};

static void __init rk312x_boot_mode_init(void)
{
	u32 flag = readl_relaxed(RK_PMU_VIRT + RK312X_PMU_SYS_REG0);
	u32 mode = readl_relaxed(RK_PMU_VIRT + RK312X_PMU_SYS_REG1);
	u32 rst_st = readl_relaxed(RK_CRU_VIRT + RK312X_CRU_GLB_RST_ST);

	if (flag == (SYS_KERNRL_REBOOT_FLAG | BOOT_RECOVER))
		mode = BOOT_MODE_RECOVERY;
	if (rst_st & ((1 << 2) | (1 << 3)))
		mode = BOOT_MODE_WATCHDOG;

	rockchip_boot_mode_init(flag, mode);
}

static void usb_uart_init(void)
{
#ifdef CONFIG_RK_USB_UART
	u32 soc_status0 = readl_relaxed(RK_GRF_VIRT + RK312X_GRF_SOC_STATUS0);
#endif
	writel_relaxed(0x34000000, RK_GRF_VIRT + RK312X_GRF_UOC1_CON4);
#ifdef CONFIG_RK_USB_UART
	if (!(soc_status0 & (1 << 5)) && (soc_status0 & (1 << 8))) {
		/* software control usb phy enable */
		writel_relaxed(0x007f0055, RK_GRF_VIRT + RK312X_GRF_UOC0_CON0);
		writel_relaxed(0x34003000, RK_GRF_VIRT + RK312X_GRF_UOC1_CON4);
	}
#endif

	writel_relaxed(0x07, RK_DEBUG_UART_VIRT + 0x88);
	writel_relaxed(0x00, RK_DEBUG_UART_VIRT + 0x04);
	writel_relaxed(0x83, RK_DEBUG_UART_VIRT + 0x0c);
	writel_relaxed(0x0d, RK_DEBUG_UART_VIRT + 0x00);
	writel_relaxed(0x00, RK_DEBUG_UART_VIRT + 0x04);
	writel_relaxed(0x03, RK_DEBUG_UART_VIRT + 0x0c);
}

static void __init rk312x_dt_map_io(void)
{
	u32 v;

	iotable_init(rk312x_io_desc, ARRAY_SIZE(rk312x_io_desc));
	debug_ll_io_init();
	usb_uart_init();

	/* pmu reset by second global soft reset */
	v = readl_relaxed(RK_CRU_VIRT + RK312X_CRU_GLB_CNT_TH);
	v &= ~(3 << 12);
	v |= 1 << 12;
	writel_relaxed(v, RK_CRU_VIRT + RK312X_CRU_GLB_CNT_TH);

	/* enable timer5 for core */
	writel_relaxed(0, RK312X_TIMER5_VIRT + 0x10);
	dsb();
	writel_relaxed(0xFFFFFFFF, RK312X_TIMER5_VIRT + 0x00);
	writel_relaxed(0xFFFFFFFF, RK312X_TIMER5_VIRT + 0x04);
	dsb();
	writel_relaxed(1, RK312X_TIMER5_VIRT + 0x10);
	dsb();
	writel_relaxed(0x80000000, RK_CRU_VIRT + RK312X_CRU_MISC_CON);
	dsb();

	rk312x_boot_mode_init();
}

static void __init rk3126_dt_map_io(void)
{
	rockchip_soc_id = ROCKCHIP_SOC_RK3126;

	rk312x_dt_map_io();
}

static void __init rk3128_dt_map_io(void)
{
	rockchip_soc_id = ROCKCHIP_SOC_RK3128;

	rk312x_dt_map_io();
}
static DEFINE_SPINLOCK(pmu_idle_lock);
static const u8 pmu_idle_map[] = {
	[IDLE_REQ_PERI] = 0,
	[IDLE_REQ_VIDEO] = 1,
	[IDLE_REQ_VIO] = 2,
	[IDLE_REQ_GPU] = 3,
	[IDLE_REQ_CORE] = 4,
	[IDLE_REQ_SYS] = 5,
	[IDLE_REQ_MSCH] = 6,
	[IDLE_REQ_CRYPTO] = 7,

};
static int rk312x_pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
	u32 val;
	unsigned long flags;
	u32 bit = pmu_idle_map[req];
	u32 idle_mask = BIT(bit) | BIT(bit + 16);
	u32 idle_target = (idle << bit) | (idle << (bit + 16));
	u32 mask = BIT(bit);

	spin_lock_irqsave(&pmu_idle_lock, flags);
	val = pmu_readl(RK312X_PMU_IDLE_REQ);
	if (idle)
		val |= mask;
	else
		val &= ~mask;
	pmu_writel(val, RK312X_PMU_IDLE_REQ);
	dsb();

	while (((pmu_readl(RK312X_PMU_IDLE_ST) & idle_mask) != idle_target))
		;
	spin_unlock_irqrestore(&pmu_idle_lock, flags);
	return 0;
}
static const u8 pmu_pd_map[] = {
	[PD_GPU] = 1,
	[PD_VIDEO] = 2,
	[PD_VIO] = 3,
};

static const u8 pmu_st_map[] = {
	[PD_GPU] = 1,
	[PD_VIDEO] = 2,
	[PD_VIO] = 3,
};

static noinline void rk312x_do_pmu_set_power_domain(enum pmu_power_domain domain
	, bool on)
{
	u8 pd = pmu_pd_map[domain];
	u32 val = pmu_readl(RK312X_PMU_PWRDN_CON);

	if (on)
		val &= ~BIT(pd);
	else
		val |=  BIT(pd);
	pmu_writel(val, RK312X_PMU_PWRDN_CON);
	dsb();

	while ((pmu_readl(RK312X_PMU_PWRDN_ST) & BIT(pmu_st_map[domain])) == on)
		;
}

static bool rk312x_pmu_power_domain_is_on(enum pmu_power_domain pd)
{
	/*1"b0: power on, 1'b1: power off*/
	return !(pmu_readl(RK312X_PMU_PWRDN_ST) & BIT(pmu_st_map[pd]));
}
static DEFINE_SPINLOCK(pmu_pd_lock);
static u32 rga_qos[RK312X_CPU_AXI_QOS_NUM_REGS];
static u32 ebc_qos[RK312X_CPU_AXI_QOS_NUM_REGS];
static u32 iep_qos[RK312X_CPU_AXI_QOS_NUM_REGS];
static u32 lcdc0_qos[RK312X_CPU_AXI_QOS_NUM_REGS];
static u32 vip0_qos[RK312X_CPU_AXI_QOS_NUM_REGS];
static u32 gpu_qos[RK312X_CPU_AXI_QOS_NUM_REGS];
static u32 video_qos[RK312X_CPU_AXI_QOS_NUM_REGS];

#define SAVE_QOS(array, NAME) RK312X_CPU_AXI_SAVE_QOS(array, RK312X_CPU_AXI_##NAME##_QOS_VIRT)
#define RESTORE_QOS(array, NAME) RK312X_CPU_AXI_RESTORE_QOS(array, RK312X_CPU_AXI_##NAME##_QOS_VIRT)

static int rk312x_pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_pd_lock, flags);
	if (rk312x_pmu_power_domain_is_on(pd) == on)
		goto out;
	if (!on) {
		if (pd == PD_GPU) {
			SAVE_QOS(gpu_qos, GPU);
			rk312x_pmu_set_idle_request(IDLE_REQ_GPU, true);
		} else if (pd == PD_VIO) {
			SAVE_QOS(rga_qos, VIO_RGA);
			SAVE_QOS(ebc_qos, VIO_EBC);
			SAVE_QOS(iep_qos, VIO_IEP);
			SAVE_QOS(lcdc0_qos, VIO_LCDC0);
			SAVE_QOS(vip0_qos, VIO_VIP0);
			rk312x_pmu_set_idle_request(IDLE_REQ_VIO, true);
		} else if (pd == PD_VIDEO) {
			SAVE_QOS(video_qos, VIDEO);
			rk312x_pmu_set_idle_request(IDLE_REQ_VIDEO, true);
		}
	}

	rk312x_do_pmu_set_power_domain(pd, on);

	if (on) {
		if (pd == PD_GPU) {
			rk312x_pmu_set_idle_request(IDLE_REQ_GPU, false);
			RESTORE_QOS(gpu_qos, GPU);
		} else if (pd == PD_VIO) {
			rk312x_pmu_set_idle_request(IDLE_REQ_VIO, false);
			RESTORE_QOS(rga_qos, VIO_RGA);
			RESTORE_QOS(ebc_qos, VIO_EBC);
			RESTORE_QOS(iep_qos, VIO_IEP);
			RESTORE_QOS(lcdc0_qos, VIO_LCDC0);
			RESTORE_QOS(vip0_qos, VIO_VIP0);
		} else if (pd == PD_VIDEO) {
			rk312x_pmu_set_idle_request(IDLE_REQ_VIDEO, false);
			RESTORE_QOS(video_qos, VIDEO);
		}
	}
out:
	spin_unlock_irqrestore(&pmu_pd_lock, flags);

	return 0;
}
extern void secondary_startup(void);
static int rk312x_sys_set_power_domain(enum pmu_power_domain pd, bool on)
{
	u32 clks_save[RK312X_CRU_CLKGATES_CON_CNT];
	u32 clks_ungating[RK312X_CRU_CLKGATES_CON_CNT];
	u32 i, ret = 0;

	for (i = 0; i < RK312X_CRU_CLKGATES_CON_CNT; i++) {
		clks_save[i] = cru_readl(RK312X_CRU_CLKGATES_CON(i));
		clks_ungating[i] = 0;
	}
	for (i = 0; i < RK312X_CRU_CLKGATES_CON_CNT; i++)
		cru_writel(0xffff0000, RK312X_CRU_CLKGATES_CON(i));

	if (on) {
#ifdef CONFIG_SMP
		if (pd >= PD_CPU_1 && pd <= PD_CPU_3) {
			writel_relaxed(0x20000 << (pd - PD_CPU_1),
				       RK_CRU_VIRT + RK312X_CRU_SOFTRSTS_CON(0));
			dsb();
			udelay(10);
			writel_relaxed(virt_to_phys(secondary_startup),
				       RK312X_IMEM_VIRT + 8);
			writel_relaxed(0xDEADBEAF, RK312X_IMEM_VIRT + 4);
			dsb_sev();
		}
#endif
	} else {
#ifdef CONFIG_SMP
		if (pd >= PD_CPU_1 && pd <= PD_CPU_3) {
			writel_relaxed(0x20002 << (pd - PD_CPU_1),
				       RK_CRU_VIRT + RK312X_CRU_SOFTRSTS_CON(0));
			dsb();
		}
#endif
	}

	if (((pd == PD_GPU) || (pd == PD_VIO) || (pd == PD_VIDEO)))
		ret = rk312x_pmu_set_power_domain(pd, on);

	for (i = 0; i < RK312X_CRU_CLKGATES_CON_CNT; i++) {
		cru_writel(clks_save[i] | 0xffff0000
			, RK312X_CRU_CLKGATES_CON(i));
	}

	return ret;
}

static void __init rk312x_dt_init_timer(void)
{
	rockchip_pmu_ops.set_power_domain = rk312x_sys_set_power_domain;
	rockchip_pmu_ops.power_domain_is_on = rk312x_pmu_power_domain_is_on;
	rockchip_pmu_ops.set_idle_request = rk312x_pmu_set_idle_request;
	of_clk_init(NULL);
	clocksource_of_init();
	of_dvfs_init();
}

static void __init rk312x_reserve(void)
{
	/* reserve memory for ION */
	rockchip_ion_reserve();
}
#ifdef CONFIG_PM
static void __init rk321x_init_suspend(void);
#endif
static void __init rk312x_init_late(void)
{
#ifdef CONFIG_PM
	rk321x_init_suspend();
#endif
	if (rockchip_jtag_enabled)
		clk_prepare_enable(clk_get_sys(NULL, "clk_jtag"));
}

static void rk312x_restart(char mode, const char *cmd)
{
	u32 boot_flag, boot_mode;

	rockchip_restart_get_boot_mode(cmd, &boot_flag, &boot_mode);

	/* for loader */
	writel_relaxed(boot_flag, RK_PMU_VIRT + RK312X_PMU_SYS_REG0);
	/* for linux */
	writel_relaxed(boot_mode, RK_PMU_VIRT + RK312X_PMU_SYS_REG1);

	dsb();

	/* pll enter slow mode */
	writel_relaxed(0x11010000, RK_CRU_VIRT + RK312X_CRU_MODE_CON);
	dsb();
	writel_relaxed(0xeca8, RK_CRU_VIRT + RK312X_CRU_GLB_SRST_SND_VALUE);
	dsb();
}

DT_MACHINE_START(RK3126_DT, "Rockchip RK3126")
	.smp		= smp_ops(rockchip_smp_ops),
	.map_io		= rk3126_dt_map_io,
	.init_time	= rk312x_dt_init_timer,
	.dt_compat	= rk3126_dt_compat,
	.init_late	= rk312x_init_late,
	.reserve	= rk312x_reserve,
	.restart	= rk312x_restart,
MACHINE_END

DT_MACHINE_START(RK3128_DT, "Rockchip RK3128")
	.smp		= smp_ops(rockchip_smp_ops),
	.map_io		= rk3128_dt_map_io,
	.init_time	= rk312x_dt_init_timer,
	.dt_compat	= rk3128_dt_compat,
	.init_late	= rk312x_init_late,
	.reserve	= rk312x_reserve,
	.restart	= rk312x_restart,
MACHINE_END


char PIE_DATA(sram_stack)[1024];
EXPORT_PIE_SYMBOL(DATA(sram_stack));

static int __init rk312x_pie_init(void)
{
	int err;

	if (!cpu_is_rk312x())
		return 0;

	err = rockchip_pie_init();
	if (err)
		return err;

	rockchip_pie_chunk = pie_load_sections(rockchip_sram_pool, rk312x);
	if (IS_ERR(rockchip_pie_chunk)) {
		err = PTR_ERR(rockchip_pie_chunk);
		pr_err("%s: failed to load section %d\n", __func__, err);
		rockchip_pie_chunk = NULL;
		return err;
	}

	rockchip_sram_virt = kern_to_pie(rockchip_pie_chunk, &__pie_common_start[0]);
	rockchip_sram_stack = kern_to_pie(rockchip_pie_chunk, (char *)DATA(sram_stack) + sizeof(DATA(sram_stack)));

	return 0;
}
arch_initcall(rk312x_pie_init);

#include "ddr_rk3126.c"
static int __init rk312x_ddr_init(void)
{
	if (cpu_is_rk312x()) {
		ddr_change_freq = _ddr_change_freq;
		ddr_round_rate = _ddr_round_rate;
		ddr_set_auto_self_refresh = _ddr_set_auto_self_refresh;
		ddr_bandwidth_get = _ddr_bandwidth_get;
		ddr_init(DDR3_DEFAULT, 300);
		}
	return 0;
}
arch_initcall_sync(rk312x_ddr_init);

#ifdef CONFIG_PM
static u32 rk_pmu_pwrdn_st;
static inline void rk_pm_soc_pd_suspend(void)
{
	rk_pmu_pwrdn_st = pmu_readl(RK312X_PMU_PWRDN_ST);
	if (!(rk_pmu_pwrdn_st & BIT(pmu_st_map[PD_GPU])))
		rk312x_sys_set_power_domain(PD_GPU, false);

	if (!(rk_pmu_pwrdn_st & BIT(pmu_st_map[PD_VIO])))
		rk312x_sys_set_power_domain(PD_VIO, false);

	if (!(rk_pmu_pwrdn_st & BIT(pmu_st_map[PD_VIDEO])))
		rk312x_sys_set_power_domain(PD_VIDEO, false);
}
static inline void rk_pm_soc_pd_resume(void)
{
	if (!(rk_pmu_pwrdn_st & BIT(pmu_st_map[PD_VIDEO])))
		rk312x_sys_set_power_domain(PD_VIDEO, true);

	if (!(rk_pmu_pwrdn_st & BIT(pmu_st_map[PD_VIO])))
		rk312x_sys_set_power_domain(PD_VIO, true);

	if (!(rk_pmu_pwrdn_st & BIT(pmu_st_map[PD_GPU])))
		rk312x_sys_set_power_domain(PD_GPU, true);
}
static void __init rk321x_init_suspend(void)
{
	pr_info("%s\n", __func__);
	rockchip_suspend_init();
	rkpm_pie_init();
	rk312x_suspend_init();
	rkpm_set_ops_pwr_dmns(rk_pm_soc_pd_suspend, rk_pm_soc_pd_resume);
}
#endif
