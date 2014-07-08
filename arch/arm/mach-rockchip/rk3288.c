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
#include <asm/cpuidle.h>
#include <asm/cputype.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "cpu_axi.h"
#include "loader.h"
#define CPU 3288
#include "sram.h"
#include "pm.h"

#define RK3288_DEVICE(name) \
	{ \
		.virtual	= (unsigned long) RK_##name##_VIRT, \
		.pfn		= __phys_to_pfn(RK3288_##name##_PHYS), \
		.length		= RK3288_##name##_SIZE, \
		.type		= MT_DEVICE, \
	}

#define RK3288_SERVICE_DEVICE(name) \
	RK_DEVICE(RK3288_SERVICE_##name##_VIRT, RK3288_SERVICE_##name##_PHYS, RK3288_SERVICE_##name##_SIZE)

#define RK3288_IMEM_VIRT (RK_BOOTRAM_VIRT + SZ_32K)
#define RK3288_TIMER7_VIRT (RK_TIMER_VIRT + 0x20)

static struct map_desc rk3288_io_desc[] __initdata = {
	RK3288_DEVICE(CRU),
	RK3288_DEVICE(GRF),
	RK3288_DEVICE(SGRF),
	RK3288_DEVICE(PMU),
	RK3288_DEVICE(ROM),
	RK3288_DEVICE(EFUSE),
	RK3288_SERVICE_DEVICE(CORE),
	RK3288_SERVICE_DEVICE(DMAC),
	RK3288_SERVICE_DEVICE(GPU),
	RK3288_SERVICE_DEVICE(PERI),
	RK3288_SERVICE_DEVICE(VIO),
	RK3288_SERVICE_DEVICE(VIDEO),
	RK3288_SERVICE_DEVICE(HEVC),
	RK3288_SERVICE_DEVICE(BUS),
	RK_DEVICE(RK_DDR_VIRT, RK3288_DDR_PCTL0_PHYS, RK3288_DDR_PCTL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + RK3288_DDR_PCTL_SIZE, RK3288_DDR_PUBL0_PHYS, RK3288_DDR_PUBL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + RK3288_DDR_PCTL_SIZE + RK3288_DDR_PUBL_SIZE, RK3288_DDR_PCTL1_PHYS, RK3288_DDR_PCTL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + 2 * RK3288_DDR_PCTL_SIZE + RK3288_DDR_PUBL_SIZE, RK3288_DDR_PUBL1_PHYS, RK3288_DDR_PUBL_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(0), RK3288_GPIO0_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(1), RK3288_GPIO1_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(2), RK3288_GPIO2_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(3), RK3288_GPIO3_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(4), RK3288_GPIO4_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(5), RK3288_GPIO5_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(6), RK3288_GPIO6_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(7), RK3288_GPIO7_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(8), RK3288_GPIO8_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_DEBUG_UART_VIRT, RK3288_UART_DBG_PHYS, RK3288_UART_SIZE),
	RK_DEVICE(RK_GIC_VIRT, RK3288_GIC_DIST_PHYS, RK3288_GIC_DIST_SIZE),
	RK_DEVICE(RK_GIC_VIRT + RK3288_GIC_DIST_SIZE, RK3288_GIC_CPU_PHYS, RK3288_GIC_CPU_SIZE),
	RK_DEVICE(RK_BOOTRAM_VIRT, RK3288_BOOTRAM_PHYS, RK3288_BOOTRAM_SIZE),
	RK_DEVICE(RK3288_IMEM_VIRT, RK3288_IMEM_PHYS, SZ_4K),
	RK_DEVICE(RK_TIMER_VIRT, RK3288_TIMER6_PHYS, RK3288_TIMER_SIZE),
};

static void __init rk3288_boot_mode_init(void)
{
	u32 flag = readl_relaxed(RK_PMU_VIRT + RK3288_PMU_SYS_REG0);
	u32 mode = readl_relaxed(RK_PMU_VIRT + RK3288_PMU_SYS_REG1);
	u32 rst_st = readl_relaxed(RK_CRU_VIRT + RK3288_CRU_GLB_RST_ST);

	if (flag == (SYS_KERNRL_REBOOT_FLAG | BOOT_RECOVER))
		mode = BOOT_MODE_RECOVERY;
	if (rst_st & ((1 << 4) | (1 << 5)))
		mode = BOOT_MODE_WATCHDOG;
	else if (rst_st & ((1 << 2) | (1 << 3)))
		mode = BOOT_MODE_TSADC;
	rockchip_boot_mode_init(flag, mode);
}

static void usb_uart_init(void)
{
	u32 soc_status2;

	writel_relaxed(0x00c00000, RK_GRF_VIRT + RK3288_GRF_UOC0_CON3);
	soc_status2 = (readl_relaxed(RK_GRF_VIRT + RK3288_GRF_SOC_STATUS2));

#ifdef CONFIG_RK_USB_UART
	if (!(soc_status2 & (1<<14)) && (soc_status2 & (1<<17))) {
		/* software control usb phy enable */
		writel_relaxed(0x00040004, RK_GRF_VIRT + RK3288_GRF_UOC0_CON2);
		/* usb phy enter suspend */
		writel_relaxed(0x003f002a, RK_GRF_VIRT + RK3288_GRF_UOC0_CON3);
		writel_relaxed(0x00c000c0, RK_GRF_VIRT + RK3288_GRF_UOC0_CON3);
	}
#endif
}

extern void secondary_startup(void);

static void __init rk3288_dt_map_io(void)
{
	u32 v;

	rockchip_soc_id = ROCKCHIP_SOC_RK3288;

	iotable_init(rk3288_io_desc, ARRAY_SIZE(rk3288_io_desc));
	debug_ll_io_init();
	usb_uart_init();

	/* pmu reset by second global soft reset */
	v = readl_relaxed(RK_CRU_VIRT + RK3288_CRU_GLB_RST_CON);
	v &= ~(3 << 2);
	v |= 1 << 2;
	writel_relaxed(v, RK_CRU_VIRT + RK3288_CRU_GLB_RST_CON);

	/* rkpwm is used instead of old pwm */
	writel_relaxed(0x00010001, RK_GRF_VIRT + RK3288_GRF_SOC_CON2);

	/* disable address remap */
	writel_relaxed(0x08000000, RK_SGRF_VIRT + RK3288_SGRF_SOC_CON0);

	/* enable timer7 for core */
	writel_relaxed(0, RK3288_TIMER7_VIRT + 0x10);
	dsb();
	writel_relaxed(0xFFFFFFFF, RK3288_TIMER7_VIRT + 0x00);
	writel_relaxed(0xFFFFFFFF, RK3288_TIMER7_VIRT + 0x04);
	dsb();
	writel_relaxed(1, RK3288_TIMER7_VIRT + 0x10);
	dsb();

	/* power up/down GPU domain wait 1us */
	writel_relaxed(24, RK_PMU_VIRT + RK3288_PMU_GPU_PWRDWN_CNT);
	writel_relaxed(24, RK_PMU_VIRT + RK3288_PMU_GPU_PWRUP_CNT);

	rk3288_boot_mode_init();
}

static const u8 pmu_st_map[] = {
	[PD_CPU_0] = 0,
	[PD_CPU_1] = 1,
	[PD_CPU_2] = 2,
	[PD_CPU_3] = 3,
	[PD_BUS] = 5,
	[PD_PERI] = 6,
	[PD_VIO] = 7,
	[PD_VIDEO] = 8,
	[PD_GPU] = 9,
	[PD_HEVC] = 10,
	[PD_SCU] = 11,
};

static bool rk3288_pmu_power_domain_is_on(enum pmu_power_domain pd)
{
	/* 1'b0: power on, 1'b1: power off */
	return !(readl_relaxed(RK_PMU_VIRT + RK3288_PMU_PWRDN_ST) & BIT(pmu_st_map[pd]));
}

static DEFINE_SPINLOCK(pmu_idle_lock);

static const u8 pmu_idle_map[] = {
	[IDLE_REQ_BUS] = 0,
	[IDLE_REQ_PERI] = 1,
	[IDLE_REQ_GPU] = 2,
	[IDLE_REQ_VIDEO] = 3,
	[IDLE_REQ_VIO] = 4,
	[IDLE_REQ_CORE] = 5,
	[IDLE_REQ_ALIVE] = 6,
	[IDLE_REQ_DMA] = 7,
	[IDLE_REQ_CPUP] = 8,
	[IDLE_REQ_HEVC] = 9,
};

static int rk3288_pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
	u32 bit = pmu_idle_map[req];
	u32 idle_mask = BIT(bit) | BIT(bit + 16);
	u32 idle_target = (idle << bit) | (idle << (bit + 16));
	u32 mask = BIT(bit);
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&pmu_idle_lock, flags);
	val = readl_relaxed(RK_PMU_VIRT + RK3288_PMU_IDLE_REQ);
	if (idle)
		val |=  mask;
	else
		val &= ~mask;
	writel_relaxed(val, RK_PMU_VIRT + RK3288_PMU_IDLE_REQ);
	dsb();

	while ((readl_relaxed(RK_PMU_VIRT + RK3288_PMU_IDLE_ST) & idle_mask) != idle_target)
		;
	spin_unlock_irqrestore(&pmu_idle_lock, flags);

	return 0;
}

static const u8 pmu_pd_map[] = {
	[PD_CPU_0] = 0,
	[PD_CPU_1] = 1,
	[PD_CPU_2] = 2,
	[PD_CPU_3] = 3,
	[PD_BUS] = 5,
	[PD_PERI] = 6,
	[PD_VIO] = 7,
	[PD_VIDEO] = 8,
	[PD_GPU] = 9,
	[PD_SCU] = 11,
	[PD_HEVC] = 14,
};

static DEFINE_SPINLOCK(pmu_pd_lock);

static noinline void rk3288_do_pmu_set_power_domain(enum pmu_power_domain domain, bool on)
{
	u8 pd = pmu_pd_map[domain];
	u32 val = readl_relaxed(RK_PMU_VIRT + RK3288_PMU_PWRDN_CON);
	if (on)
		val &= ~BIT(pd);
	else
		val |=  BIT(pd);
	writel_relaxed(val, RK_PMU_VIRT + RK3288_PMU_PWRDN_CON);
	dsb();

	while ((readl_relaxed(RK_PMU_VIRT + RK3288_PMU_PWRDN_ST) & BIT(pmu_st_map[domain])) == on)
		;
}

static u32 gpu_r_qos[CPU_AXI_QOS_NUM_REGS];
static u32 gpu_w_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vio0_iep_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vio0_vip_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vio0_vop_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vio1_isp_r_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vio1_isp_w0_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vio1_isp_w1_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vio1_vop_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vio2_rga_r_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vio2_rga_w_qos[CPU_AXI_QOS_NUM_REGS];
static u32 video_qos[CPU_AXI_QOS_NUM_REGS];
static u32 hevc_r_qos[CPU_AXI_QOS_NUM_REGS];
static u32 hevc_w_qos[CPU_AXI_QOS_NUM_REGS];

#define SAVE_QOS(array, NAME) CPU_AXI_SAVE_QOS(array, RK3288_CPU_AXI_##NAME##_QOS_VIRT)
#define RESTORE_QOS(array, NAME) CPU_AXI_RESTORE_QOS(array, RK3288_CPU_AXI_##NAME##_QOS_VIRT)

static int rk3288_pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_pd_lock, flags);
	if (rk3288_pmu_power_domain_is_on(pd) == on)
		goto out;

	if (!on) {
		/* if power down, idle request to NIU first */
		if (pd == PD_VIO) {
			SAVE_QOS(vio0_iep_qos, VIO0_IEP);
			SAVE_QOS(vio0_vip_qos, VIO0_VIP);
			SAVE_QOS(vio0_vop_qos, VIO0_VOP);
			SAVE_QOS(vio1_isp_r_qos, VIO1_ISP_R);
			SAVE_QOS(vio1_isp_w0_qos, VIO1_ISP_W0);
			SAVE_QOS(vio1_isp_w1_qos, VIO1_ISP_W1);
			SAVE_QOS(vio1_vop_qos, VIO1_VOP);
			SAVE_QOS(vio2_rga_r_qos, VIO2_RGA_R);
			SAVE_QOS(vio2_rga_w_qos, VIO2_RGA_W);
			rk3288_pmu_set_idle_request(IDLE_REQ_VIO, true);
		} else if (pd == PD_VIDEO) {
			SAVE_QOS(video_qos, VIDEO);
			rk3288_pmu_set_idle_request(IDLE_REQ_VIDEO, true);
		} else if (pd == PD_GPU) {
			SAVE_QOS(gpu_r_qos, GPU_R);
			SAVE_QOS(gpu_w_qos, GPU_W);
			rk3288_pmu_set_idle_request(IDLE_REQ_GPU, true);
		} else if (pd == PD_HEVC) {
			SAVE_QOS(hevc_r_qos, HEVC_R);
			SAVE_QOS(hevc_w_qos, HEVC_W);
			rk3288_pmu_set_idle_request(IDLE_REQ_HEVC, true);
		} else if (pd >= PD_CPU_1 && pd <= PD_CPU_3) {
			writel_relaxed(0x20002 << (pd - PD_CPU_1), RK_CRU_VIRT + RK3288_CRU_SOFTRSTS_CON(0));
			dsb();
		}
                 else if (pd == PD_PERI) {
			rk3288_pmu_set_idle_request(IDLE_REQ_PERI, true);
		}
        
	}

	rk3288_do_pmu_set_power_domain(pd, on);

	if (on) {
		/* if power up, idle request release to NIU */
		if (pd == PD_VIO) {
			rk3288_pmu_set_idle_request(IDLE_REQ_VIO, false);
			RESTORE_QOS(vio0_iep_qos, VIO0_IEP);
			RESTORE_QOS(vio0_vip_qos, VIO0_VIP);
			RESTORE_QOS(vio0_vop_qos, VIO0_VOP);
			RESTORE_QOS(vio1_isp_r_qos, VIO1_ISP_R);
			RESTORE_QOS(vio1_isp_w0_qos, VIO1_ISP_W0);
			RESTORE_QOS(vio1_isp_w1_qos, VIO1_ISP_W1);
			RESTORE_QOS(vio1_vop_qos, VIO1_VOP);
			RESTORE_QOS(vio2_rga_r_qos, VIO2_RGA_R);
			RESTORE_QOS(vio2_rga_w_qos, VIO2_RGA_W);
		} else if (pd == PD_VIDEO) {
			rk3288_pmu_set_idle_request(IDLE_REQ_VIDEO, false);
			RESTORE_QOS(video_qos, VIDEO);
		} else if (pd == PD_GPU) {
			rk3288_pmu_set_idle_request(IDLE_REQ_GPU, false);
			RESTORE_QOS(gpu_r_qos, GPU_R);
			RESTORE_QOS(gpu_w_qos, GPU_W);
		} else if (pd == PD_HEVC) {
			rk3288_pmu_set_idle_request(IDLE_REQ_HEVC, false);
			RESTORE_QOS(hevc_r_qos, HEVC_R);
			RESTORE_QOS(hevc_w_qos, HEVC_W);
		} else if (pd >= PD_CPU_1 && pd <= PD_CPU_3) {
#ifdef CONFIG_SMP
			writel_relaxed(0x20000 << (pd - PD_CPU_1), RK_CRU_VIRT + RK3288_CRU_SOFTRSTS_CON(0));
			dsb();
			udelay(10);
			writel_relaxed(virt_to_phys(secondary_startup), RK3288_IMEM_VIRT + 8);
			writel_relaxed(0xDEADBEAF, RK3288_IMEM_VIRT + 4);
			dsb_sev();
#endif
		}
                else if (pd == PD_PERI) {
			rk3288_pmu_set_idle_request(IDLE_REQ_PERI, false);
		}
	}

out:
	spin_unlock_irqrestore(&pmu_pd_lock, flags);
	return 0;
}

static int rk3288_sys_set_power_domain(enum pmu_power_domain pd, bool on)
{
	u32 clks_ungating[RK3288_CRU_CLKGATES_CON_CNT];
	u32 clks_save[RK3288_CRU_CLKGATES_CON_CNT];
	u32 i, ret;

	for (i = 0; i < RK3288_CRU_CLKGATES_CON_CNT; i++) {
		clks_save[i] = cru_readl(RK3288_CRU_CLKGATES_CON(i));
		clks_ungating[i] = 0;
	}

	switch (pd) {
	case PD_GPU:
		/* gpu */
		clks_ungating[5] = 1 << 7;
		/* aclk_gpu */
		clks_ungating[18] = 1 << 0;
		break;
	case PD_VIDEO:
		/* aclk_vdpu_src hclk_vpu aclk_vepu_src */
		clks_ungating[3] = 1 << 11 | 1 << 10 | 1 << 9;
		/* hclk_video aclk_video */
		clks_ungating[9] = 1 << 1 | 1 << 0;
		break;
	case PD_VIO:
		/* aclk_lcdc0/1_src dclk_lcdc0/1_src rga_core aclk_rga_src */
		/* edp_24m edp isp isp_jpeg */
		clks_ungating[3] =
		    1 << 0 | 1 << 1 | 1 << 2 | 1 << 3 | 1 << 4 | 1 << 5 |
		    1 << 12 | 1 << 13 | 1 << 14 | 1 << 15;
		clks_ungating[15] = 0xffff;
		clks_ungating[16] = 0x0fff;
		break;
	case PD_HEVC:
		/* hevc_core hevc_cabac aclk_hevc */
		clks_ungating[13] = 1 << 15 | 1 << 14 | 1 << 13;
		break;
#if 0
	case PD_CS:
		clks_ungating[12] = 1 << 11 | 1 < 10 | 1 << 9 | 1 << 8;
		break;
#endif
	default:
		break;
	}

	for (i = 0; i < RK3288_CRU_CLKGATES_CON_CNT; i++) {
		if (clks_ungating[i])
			cru_writel(clks_ungating[i] << 16, RK3288_CRU_CLKGATES_CON(i));
	}

	ret = rk3288_pmu_set_power_domain(pd, on);

	for (i = 0; i < RK3288_CRU_CLKGATES_CON_CNT; i++) {
		if (clks_ungating[i])
			cru_writel(clks_save[i] | 0xffff0000, RK3288_CRU_CLKGATES_CON(i));
	}

	return ret;
}

static void __init rk3288_dt_init_timer(void)
{
	rockchip_pmu_ops.set_power_domain = rk3288_sys_set_power_domain;
	rockchip_pmu_ops.power_domain_is_on = rk3288_pmu_power_domain_is_on;
	rockchip_pmu_ops.set_idle_request = rk3288_pmu_set_idle_request;
	of_clk_init(NULL);
	clocksource_of_init();
	of_dvfs_init();
}

static void __init rk3288_reserve(void)
{
	/* reserve memory for ION */
	rockchip_ion_reserve();
}

static const char * const rk3288_dt_compat[] __initconst = {
	"rockchip,rk3288",
	NULL,
};

static void rk3288_restart(char mode, const char *cmd)
{
	u32 boot_flag, boot_mode;

	rockchip_restart_get_boot_mode(cmd, &boot_flag, &boot_mode);

	writel_relaxed(boot_flag, RK_PMU_VIRT + RK3288_PMU_SYS_REG0);	// for loader
	writel_relaxed(boot_mode, RK_PMU_VIRT + RK3288_PMU_SYS_REG1);	// for linux
	dsb();

	/* pll enter slow mode */
	writel_relaxed(0xf3030000, RK_CRU_VIRT + RK3288_CRU_MODE_CON);
	dsb();
	writel_relaxed(0xeca8, RK_CRU_VIRT + RK3288_CRU_GLB_SRST_SND_VALUE);
	dsb();
}

static struct cpuidle_driver rk3288_cpuidle_driver = {
	.name = "rk3288_cpuidle",
	.owner = THIS_MODULE,
	.states[0] = ARM_CPUIDLE_WFI_STATE,
	.state_count = 1,
};

static int rk3288_cpuidle_enter(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	void *sel = RK_CRU_VIRT + RK3288_CRU_CLKSELS_CON(36);
	u32 con = readl_relaxed(sel);
	u32 cpu = MPIDR_AFFINITY_LEVEL(read_cpuid_mpidr(), 0);
	writel_relaxed(0x70007 << (cpu << 2), sel);
	cpu_do_idle();
	writel_relaxed((0x70000 << (cpu << 2)) | con, sel);
	dsb();
	return index;
}

static void __init rk3288_init_cpuidle(void)
{
	int ret;

	if (!rockchip_jtag_enabled)
		rk3288_cpuidle_driver.states[0].enter = rk3288_cpuidle_enter;
	ret = cpuidle_register(&rk3288_cpuidle_driver, NULL);
	if (ret)
		pr_err("%s: failed to register cpuidle driver: %d\n", __func__, ret);
}
#ifdef CONFIG_PM
static void __init rk3288_init_suspend(void);
#endif
static void __init rk3288_init_late(void)
{
#ifdef CONFIG_PM
	rk3288_init_suspend();
#endif
#ifdef CONFIG_CPU_IDLE
	rk3288_init_cpuidle();
#endif
	if (rockchip_jtag_enabled)
		clk_prepare_enable(clk_get_sys(NULL, "clk_jtag"));
}

DT_MACHINE_START(RK3288_DT, "Rockchip RK3288 (Flattened Device Tree)")
	.smp		= smp_ops(rockchip_smp_ops),
	.map_io		= rk3288_dt_map_io,
	.init_time	= rk3288_dt_init_timer,
	.dt_compat	= rk3288_dt_compat,
	.init_late	= rk3288_init_late,
	.reserve	= rk3288_reserve,
	.restart	= rk3288_restart,
MACHINE_END

char PIE_DATA(sram_stack)[1024];
EXPORT_PIE_SYMBOL(DATA(sram_stack));

static int __init rk3288_pie_init(void)
{
	int err;
	if (!cpu_is_rk3288())
		return 0;

	err = rockchip_pie_init();
	if (err)
		return err;

	rockchip_pie_chunk = pie_load_sections(rockchip_sram_pool, rk3288);
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
arch_initcall(rk3288_pie_init);
#ifdef CONFIG_PM
#include "pm-rk3288.c"

static u32 rk_pmu_pwrdn_st;
static inline void rk_pm_soc_pd_suspend(void)
{
    rk_pmu_pwrdn_st = pmu_readl(RK3288_PMU_PWRDN_ST);

    if(!(rk_pmu_pwrdn_st&BIT(pmu_st_map[PD_GPU])))
    rk3288_sys_set_power_domain(PD_GPU, false);

    if(!(rk_pmu_pwrdn_st&BIT(pmu_st_map[PD_HEVC])))
    rk3288_sys_set_power_domain(PD_HEVC, false);

    if(!(rk_pmu_pwrdn_st&BIT(pmu_st_map[PD_VIO])))
    rk3288_sys_set_power_domain(PD_VIO, false);

    if(!(rk_pmu_pwrdn_st&BIT(pmu_st_map[PD_VIDEO])))
    rk3288_sys_set_power_domain(PD_VIDEO, false);
#if 0
    rkpm_ddr_printascii("pd state:");
    rkpm_ddr_printhex(rk_pmu_pwrdn_st);        
    rkpm_ddr_printhex(pmu_readl(RK3288_PMU_PWRDN_ST));        
    rkpm_ddr_printascii("\n");
 #endif  
}
static inline void rk_pm_soc_pd_resume(void)
{
    if(!(rk_pmu_pwrdn_st&BIT(pmu_st_map[PD_GPU])))
        rk3288_sys_set_power_domain(PD_GPU, true);

    if(!(rk_pmu_pwrdn_st&BIT(pmu_st_map[PD_HEVC])))
        rk3288_sys_set_power_domain(PD_HEVC, true);

    if(!(rk_pmu_pwrdn_st&BIT(pmu_st_map[PD_VIO])))
     rk3288_sys_set_power_domain(PD_VIO, true);

    if(!(rk_pmu_pwrdn_st&BIT(pmu_st_map[PD_VIDEO])))
        rk3288_sys_set_power_domain(PD_VIDEO, true);

#if 0
    rkpm_ddr_printascii("pd state:");
    rkpm_ddr_printhex(pmu_readl(RK3288_PMU_PWRDN_ST));        
    rkpm_ddr_printascii("\n");
#endif    
}
void inline rkpm_periph_pd_dn(bool on)
{
    rk3288_sys_set_power_domain(PD_PERI, on);
}

static void __init rk3288_init_suspend(void)
{
    printk("%s\n",__FUNCTION__);
    rockchip_suspend_init();       
    rkpm_pie_init();
    rk3288_suspend_init();
   rkpm_set_ops_pwr_dmns(rk_pm_soc_pd_suspend,rk_pm_soc_pd_resume);  
}

#if 0
extern bool console_suspend_enabled;

static int  __init rk3288_pm_dbg(void)
{
#if 1    
        console_suspend_enabled=0;
        do{
            pm_suspend(PM_SUSPEND_MEM);
        }
        while(1);
        
#endif

}

//late_initcall_sync(rk3288_pm_dbg);
#endif


#endif
#define sram_printascii(s) do {} while (0) /* FIXME */
#include "ddr_rk32.c"

static int __init rk3288_ddr_init(void)
{
    if (cpu_is_rk3288())
    {
        ddr_change_freq = _ddr_change_freq;
        ddr_round_rate = _ddr_round_rate;
        ddr_set_auto_self_refresh = _ddr_set_auto_self_refresh;

        ddr_init(DDR3_DEFAULT, 300);
    }

    return 0;
}
arch_initcall_sync(rk3288_ddr_init);

