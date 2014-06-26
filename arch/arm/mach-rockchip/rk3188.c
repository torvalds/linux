/*
 * Device Tree support for Rockchip RK3188
 *
 * Copyright (C) 2013-2014 ROCKCHIP, Inc.
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
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/rockchip/dvfs.h>
#include <linux/rockchip/common.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/pmu.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "cpu_axi.h"
#include "loader.h"
#include "sram.h"

#define RK3188_DEVICE(name) \
	{ \
		.virtual	= (unsigned long) RK_##name##_VIRT, \
		.pfn		= __phys_to_pfn(RK3188_##name##_PHYS), \
		.length		= RK3188_##name##_SIZE, \
		.type		= MT_DEVICE, \
	}

static struct map_desc rk3188_io_desc[] __initdata = {
	RK3188_DEVICE(CRU),
	RK3188_DEVICE(GRF),
	RK3188_DEVICE(PMU),
	RK3188_DEVICE(ROM),
	RK3188_DEVICE(EFUSE),
	RK3188_DEVICE(CPU_AXI_BUS),
	RK_DEVICE(RK_DDR_VIRT, RK3188_DDR_PCTL_PHYS, RK3188_DDR_PCTL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + RK3188_DDR_PCTL_SIZE, RK3188_DDR_PUBL_PHYS, RK3188_DDR_PUBL_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(0), RK3188_GPIO0_PHYS, RK3188_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(1), RK3188_GPIO1_PHYS, RK3188_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(2), RK3188_GPIO2_PHYS, RK3188_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(3), RK3188_GPIO3_PHYS, RK3188_GPIO_SIZE),
	RK_DEVICE(RK_DEBUG_UART_VIRT, RK3188_UART2_PHYS, RK3188_UART_SIZE),
};

static void __init rk3188_boot_mode_init(void)
{
	u32 flag = readl_relaxed(RK_PMU_VIRT + RK3188_PMU_SYS_REG0);
	u32 mode = readl_relaxed(RK_PMU_VIRT + RK3188_PMU_SYS_REG1);

	if (flag == (SYS_KERNRL_REBOOT_FLAG | BOOT_RECOVER)) {
		mode = BOOT_MODE_RECOVERY;
	}
	rockchip_boot_mode_init(flag, mode);
#ifdef CONFIG_RK29_WATCHDOG
	writel_relaxed(BOOT_MODE_WATCHDOG, RK_PMU_VIRT + RK3188_PMU_SYS_REG1);
#endif
}
static void usb_uart_init(void)
{
	u32 soc_status0;

	writel_relaxed(0x03100000, RK_GRF_VIRT + RK3188_GRF_UOC0_CON0);
	soc_status0 = (readl_relaxed(RK_GRF_VIRT + RK3188_GRF_SOC_STATUS0));

#ifdef CONFIG_RK_USB_UART
	if (!(soc_status0 & (1<<10)) && (soc_status0 & (1<<13))) {
		/* software control usb phy enable */
		writel_relaxed(0x00040004, RK_GRF_VIRT + RK3188_GRF_UOC0_CON2);
		/* usb phy enter suspend */
		writel_relaxed(0x003f002a, RK_GRF_VIRT + RK3188_GRF_UOC0_CON3);
		writel_relaxed(0x03000300, RK_GRF_VIRT + RK3188_GRF_UOC0_CON0);
    }    
#endif
}

static void __init rk3188_dt_map_io(void)
{
	iotable_init(rk3188_io_desc, ARRAY_SIZE(rk3188_io_desc));
	debug_ll_io_init();
	usb_uart_init();

	rockchip_soc_id = ROCKCHIP_SOC_RK3188;
	if (readl_relaxed(RK_ROM_VIRT + 0x27f0) == 0x33313042
	 && readl_relaxed(RK_ROM_VIRT + 0x27f4) == 0x32303133
	 && readl_relaxed(RK_ROM_VIRT + 0x27f8) == 0x30313331
	 && readl_relaxed(RK_ROM_VIRT + 0x27fc) == 0x56313031)
		rockchip_soc_id = ROCKCHIP_SOC_RK3188PLUS;

	/* rki2c is used instead of old i2c */
	writel_relaxed(0xF800F800, RK_GRF_VIRT + RK3188_GRF_SOC_CON1);

	rk3188_boot_mode_init();
}

static const u8 pmu_pd_map[] = {
	[PD_CPU_0] = 0,
	[PD_CPU_1] = 1,
	[PD_CPU_2] = 2,
	[PD_CPU_3] = 3,
	[PD_SCU] = 4,
	[PD_BUS] = 5,
	[PD_PERI] = 6,
	[PD_VIO] = 7,
	[PD_VIDEO] = 8,
	[PD_GPU] = 9,
	[PD_CS] = 10,
};

static bool rk3188_pmu_power_domain_is_on(enum pmu_power_domain pd)
{
	/* 1'b0: power on, 1'b1: power off */
	return !(readl_relaxed(RK_PMU_VIRT + RK3188_PMU_PWRDN_ST) & BIT(pmu_pd_map[pd]));
}

static noinline void rk3188_do_pmu_set_power_domain(enum pmu_power_domain domain, bool on)
{
	u8 pd = pmu_pd_map[domain];
	u32 val = readl_relaxed(RK_PMU_VIRT + RK3188_PMU_PWRDN_CON);
	if (on)
		val &= ~BIT(pd);
	else
		val |=  BIT(pd);
	writel_relaxed(val, RK_PMU_VIRT + RK3188_PMU_PWRDN_CON);
	dsb();

	while ((readl_relaxed(RK_PMU_VIRT + RK3188_PMU_PWRDN_ST) & BIT(pd)) == on)
		;
}

static DEFINE_SPINLOCK(pmu_misc_con1_lock);

static const u8 pmu_req_map[] = {
	[IDLE_REQ_BUS] = 1,
	[IDLE_REQ_PERI] = 2,
	[IDLE_REQ_GPU] = 3,
	[IDLE_REQ_VIDEO] = 4,
	[IDLE_REQ_VIO] = 5,
};

static const u8 pmu_idle_map[] = {
	[IDLE_REQ_DMA] = 14,
	[IDLE_REQ_CORE] = 15,
	[IDLE_REQ_VIO] = 22,
	[IDLE_REQ_VIDEO] = 23,
	[IDLE_REQ_GPU] = 24,
	[IDLE_REQ_PERI] = 25,
	[IDLE_REQ_BUS] = 26,
};

static const u8 pmu_ack_map[] = {
	[IDLE_REQ_DMA] = 17,
	[IDLE_REQ_CORE] = 18,
	[IDLE_REQ_VIO] = 27,
	[IDLE_REQ_VIDEO] = 28,
	[IDLE_REQ_GPU] = 29,
	[IDLE_REQ_PERI] = 30,
	[IDLE_REQ_BUS] = 31,
};

static int rk3188_pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
	u32 idle_mask = BIT(pmu_idle_map[req]);
	u32 idle_target = idle << pmu_idle_map[req];
	u32 ack_mask = BIT(pmu_ack_map[req]);
	u32 ack_target = idle << pmu_ack_map[req];
	u32 mask = BIT(pmu_req_map[req]);
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&pmu_misc_con1_lock, flags);
	val = readl_relaxed(RK_PMU_VIRT + RK3188_PMU_MISC_CON1);
	if (idle)
		val |=  mask;
	else
		val &= ~mask;
	writel_relaxed(val, RK_PMU_VIRT + RK3188_PMU_MISC_CON1);
	dsb();

	while ((readl_relaxed(RK_PMU_VIRT + RK3188_PMU_PWRDN_ST) & ack_mask) != ack_target)
		;
	while ((readl_relaxed(RK_PMU_VIRT + RK3188_PMU_PWRDN_ST) & idle_mask) != idle_target)
		;
	spin_unlock_irqrestore(&pmu_misc_con1_lock, flags);

	return 0;
}

/*
 *  software should power down or power up power domain one by one. Power down or
 *  power up multiple power domains simultaneously will result in chip electric current
 *  change dramatically which will affect the chip function.
 */
static DEFINE_SPINLOCK(pmu_pd_lock);
static u32 lcdc0_qos[CPU_AXI_QOS_NUM_REGS];
static u32 lcdc1_qos[CPU_AXI_QOS_NUM_REGS];
static u32 cif0_qos[CPU_AXI_QOS_NUM_REGS];
static u32 cif1_qos[CPU_AXI_QOS_NUM_REGS];
static u32 ipp_qos[CPU_AXI_QOS_NUM_REGS];
static u32 rga_qos[CPU_AXI_QOS_NUM_REGS];
static u32 gpu_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vpu_qos[CPU_AXI_QOS_NUM_REGS];

#define SAVE_QOS(array, NAME) CPU_AXI_SAVE_QOS(array, RK3188_CPU_AXI_##NAME##_QOS_VIRT)
#define RESTORE_QOS(array, NAME) CPU_AXI_RESTORE_QOS(array, RK3188_CPU_AXI_##NAME##_QOS_VIRT)

static int rk3188_pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_pd_lock, flags);
	if (rk3188_pmu_power_domain_is_on(pd) == on) {
		spin_unlock_irqrestore(&pmu_pd_lock, flags);
		return 0;
	}
	if (!on) {
		/* if power down, idle request to NIU first */
		if (pd == PD_VIO) {
			SAVE_QOS(lcdc0_qos, LCDC0);
			SAVE_QOS(lcdc1_qos, LCDC1);
			SAVE_QOS(cif0_qos, CIF0);
			SAVE_QOS(cif1_qos, CIF1);
			SAVE_QOS(ipp_qos, IPP);
			SAVE_QOS(rga_qos, RGA);
			rk3188_pmu_set_idle_request(IDLE_REQ_VIO, true);
		} else if (pd == PD_VIDEO) {
			SAVE_QOS(vpu_qos, VPU);
			rk3188_pmu_set_idle_request(IDLE_REQ_VIDEO, true);
		} else if (pd == PD_GPU) {
			SAVE_QOS(gpu_qos, GPU);
			rk3188_pmu_set_idle_request(IDLE_REQ_GPU, true);
		}
	}
	rk3188_do_pmu_set_power_domain(pd, on);
	if (on) {
		/* if power up, idle request release to NIU */
		if (pd == PD_VIO) {
			rk3188_pmu_set_idle_request(IDLE_REQ_VIO, false);
			RESTORE_QOS(lcdc0_qos, LCDC0);
			RESTORE_QOS(lcdc1_qos, LCDC1);
			RESTORE_QOS(cif0_qos, CIF0);
			RESTORE_QOS(cif1_qos, CIF1);
			RESTORE_QOS(ipp_qos, IPP);
			RESTORE_QOS(rga_qos, RGA);
		} else if (pd == PD_VIDEO) {
			rk3188_pmu_set_idle_request(IDLE_REQ_VIDEO, false);
			RESTORE_QOS(vpu_qos, VPU);
		} else if (pd == PD_GPU) {
			rk3188_pmu_set_idle_request(IDLE_REQ_GPU, false);
			RESTORE_QOS(gpu_qos, GPU);
		}
	}
	spin_unlock_irqrestore(&pmu_pd_lock, flags);

	return 0;
}

static void __init rk3188_dt_init_timer(void)
{
	rockchip_pmu_ops.set_power_domain = rk3188_pmu_set_power_domain;
	rockchip_pmu_ops.power_domain_is_on = rk3188_pmu_power_domain_is_on;
	rockchip_pmu_ops.set_idle_request = rk3188_pmu_set_idle_request;
	of_clk_init(NULL);
	clocksource_of_init();
	of_dvfs_init();
}

static void __init rk3188_reserve(void)
{
	/* reserve memory for ION */
	rockchip_ion_reserve();
}

static const char * const rk3188_dt_compat[] __initconst = {
	"rockchip,rk3188",
	NULL,
};

static void rk3188_restart(char mode, const char *cmd)
{
	u32 boot_flag, boot_mode;

	rockchip_restart_get_boot_mode(cmd, &boot_flag, &boot_mode);

	writel_relaxed(boot_flag, RK_PMU_VIRT + RK3188_PMU_SYS_REG0);	// for loader
	writel_relaxed(boot_mode, RK_PMU_VIRT + RK3188_PMU_SYS_REG1);	// for linux
	dsb();

	/* disable remap */
	writel_relaxed(1 << (12 + 16), RK_GRF_VIRT + RK3188_GRF_SOC_CON0);
	/* pll enter slow mode */
	writel_relaxed(RK3188_PLL_MODE_SLOW(RK3188_APLL_ID) |
		       RK3188_PLL_MODE_SLOW(RK3188_CPLL_ID) |
		       RK3188_PLL_MODE_SLOW(RK3188_GPLL_ID),
		       RK_CRU_VIRT + RK3188_CRU_MODE_CON);
	dsb();
	writel_relaxed(0xeca8, RK_CRU_VIRT + RK3188_CRU_GLB_SRST_SND);
	dsb();
}
#ifdef CONFIG_PM
static void __init rk3188_init_suspend(void);
#endif
DT_MACHINE_START(RK3188_DT, "RK30board")
	.smp		= smp_ops(rockchip_smp_ops),
	.map_io		= rk3188_dt_map_io,
	.init_time	= rk3188_dt_init_timer,
	.dt_compat	= rk3188_dt_compat,
#ifdef CONFIG_PM
	.init_late	= rk3188_init_suspend,
#endif
	.reserve	= rk3188_reserve,
	.restart	= rk3188_restart,
MACHINE_END

#define CPU 3188
char PIE_DATA(sram_stack)[1024];
EXPORT_PIE_SYMBOL(DATA(sram_stack));

static int __init rk3188_pie_init(void)
{
	int err;

	if (!cpu_is_rk3188())
		return 0;

	err = rockchip_pie_init();
	if (err)
		return err;

	rockchip_pie_chunk = pie_load_sections(rockchip_sram_pool, rk3188);
	if (IS_ERR(rockchip_pie_chunk)) {
		err = PTR_ERR(rockchip_pie_chunk);
		pr_err("%s: failed to load section %d\n", __func__, err);
		rockchip_pie_chunk = NULL;
		return err;
	}

	rockchip_sram_virt = kern_to_pie(rockchip_pie_chunk, &__pie_common_start[0]);
	rockchip_sram_stack = kern_to_pie(rockchip_pie_chunk, (char *) DATA(sram_stack) + sizeof(DATA(sram_stack)));

	return 0;
}
arch_initcall(rk3188_pie_init);

#ifdef CONFIG_PM
#include "pm-rk3188.c"
static void __init rk3188_init_suspend(void)
{
        rockchip_suspend_init();
        rkpm_pie_init();
        rk3188_suspend_init();
}
#endif
#define CONFIG_ARCH_RK3188
#define RK30_DDR_PCTL_BASE RK_DDR_VIRT
#define RK30_DDR_PUBL_BASE (RK_DDR_VIRT + RK3188_DDR_PCTL_SIZE)
#define rk_pll_flag() 0 /* FIXME */
#define sram_printascii(s) do {} while (0) /* FIXME */
#include "ddr_rk30.c"

static int __init rk3188_ddr_init(void)
{
	if (cpu_is_rk3188()) {

		ddr_change_freq = _ddr_change_freq;
		ddr_round_rate = _ddr_round_rate;
		ddr_set_auto_self_refresh = _ddr_set_auto_self_refresh;

		ddr_init(DDR3_DEFAULT, 300);
	}

	return 0;
}
arch_initcall_sync(rk3188_ddr_init);


