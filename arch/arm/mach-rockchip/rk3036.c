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
#include <linux/wakeup_reason.h>
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
	RK3036_DEVICE(CPU_AXI_BUS),
	RK_DEVICE(RK_DDR_VIRT, RK3036_DDR_PCTL_PHYS, RK3036_DDR_PCTL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + RK3036_DDR_PCTL_SIZE, RK3036_DDR_PHY_PHYS,
		  RK3036_DDR_PHY_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(0), RK3036_GPIO0_PHYS, RK3036_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(1), RK3036_GPIO1_PHYS, RK3036_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(2), RK3036_GPIO2_PHYS, RK3036_GPIO_SIZE),
	RK_DEVICE(RK_DEBUG_UART_VIRT, RK3036_UART2_PHYS, RK3036_UART_SIZE),
	RK_DEVICE(RK_GIC_VIRT, RK3036_GIC_DIST_PHYS, RK3036_GIC_DIST_SIZE),
	RK_DEVICE(RK_GIC_VIRT + RK3036_GIC_DIST_SIZE, RK3036_GIC_CPU_PHYS,
		  RK3036_GIC_CPU_SIZE),
	RK_DEVICE(RK3036_IMEM_VIRT, RK3036_IMEM_PHYS, SZ_4K),
	RK_DEVICE(RK_TIMER_VIRT, RK3036_TIMER_PHYS, RK3036_TIMER_SIZE),
	RK_DEVICE(RK_PWM_VIRT, RK3036_PWM_PHYS, RK3036_PWM_SIZE),
};

static void __init rk3036_boot_mode_init(void)
{
	u32 flag = readl_relaxed(RK_GRF_VIRT + RK3036_GRF_OS_REG4);
	u32 mode = readl_relaxed(RK_GRF_VIRT + RK3036_GRF_OS_REG5);
	u32 rst_st = readl_relaxed(RK_CRU_VIRT + RK3036_CRU_RST_ST);

	if (flag == (SYS_KERNRL_REBOOT_FLAG | BOOT_RECOVER))
		mode = BOOT_MODE_RECOVERY;
	if (rst_st & ((1 << 2) | (1 << 3)))
		mode = BOOT_MODE_WATCHDOG;
	rockchip_boot_mode_init(flag, mode);
}

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

	writel_relaxed(0x07, RK_DEBUG_UART_VIRT + 0x88);
	writel_relaxed(0x00, RK_DEBUG_UART_VIRT + 0x04);
	writel_relaxed(0x83, RK_DEBUG_UART_VIRT + 0x0c);
	writel_relaxed(0x0d, RK_DEBUG_UART_VIRT + 0x00);
	writel_relaxed(0x00, RK_DEBUG_UART_VIRT + 0x04);
	writel_relaxed(0x03, RK_DEBUG_UART_VIRT + 0x0c);
}

static void __init rk3036_dt_map_io(void)
{
	rockchip_soc_id = ROCKCHIP_SOC_RK3036;

	iotable_init(rk3036_io_desc, ARRAY_SIZE(rk3036_io_desc));
	debug_ll_io_init();
	usb_uart_init();

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
		if (PD_CPU_1 == pd) {
			writel_relaxed(0x20000
				, RK_CRU_VIRT + RK3036_CRU_SOFTRST0_CON);
			dsb();
			udelay(10);
			writel_relaxed(virt_to_phys(secondary_startup),
					   RK3036_IMEM_VIRT + 8);
			writel_relaxed(0xDEADBEAF, RK3036_IMEM_VIRT + 4);
			dsb_sev();
		}
#endif
	} else {
#ifdef CONFIG_SMP
		if (PD_CPU_1 == pd) {
			writel_relaxed(0x20002
				, RK_CRU_VIRT + RK3036_CRU_SOFTRST0_CON);
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

#ifdef CONFIG_PM
static inline void rk3036_uart_printch(char byte)
{
write_uart:
	writel_relaxed(byte, RK_DEBUG_UART_VIRT);
	dsb();

	/* loop check LSR[6], Transmitter Empty bit */
	while (!(readl_relaxed(RK_DEBUG_UART_VIRT + 0x14) & 0x40))
		barrier();

	if (byte == '\n') {
		byte = '\r';
		goto write_uart;
	}
}

static void rk3036_ddr_printch(char byte)
{
	rk3036_uart_printch(byte);

	rk_last_log_text(&byte, 1);

	if (byte == '\n') {
		byte = '\r';
		rk_last_log_text(&byte, 1);
	}
}

enum rk_plls_id {
	APLL_ID = 0,
	DPLL_ID,
	GPLL_ID,
	RK3036_END_PLL_ID,
};

#define GPIO_INTEN 0x30
#define GPIO_INT_STATUS 0x40
#define GIC_DIST_PENDING_SET 0x200
static void rk3036_pm_dump_irq(void)
{
	u32 irq_gpio = (readl_relaxed(RK_GIC_VIRT
		+ GIC_DIST_PENDING_SET + 8) >> 4) & 7;
	u32 irq[4];
	int i;

	for (i = 0; i < ARRAY_SIZE(irq); i++) {
		irq[i] = readl_relaxed(RK_GIC_VIRT + GIC_DIST_PENDING_SET +
					   (1 + i) * 4);
		if (irq[i])
			log_wakeup_reason(32 * (i + 1) + fls(irq[i]) - 1);
	}
	pr_info("wakeup irq: %08x %08x %08x %08x\n",
		irq[0], irq[1], irq[2], irq[3]);
	for (i = 0; i <= 2; i++) {
		if (irq_gpio & (1 << i))
			pr_info("wakeup gpio%d: %08x\n", i,
				readl_relaxed(RK_GPIO_VIRT(i) +
						  GPIO_INT_STATUS));
	}
}

#define DUMP_GPIO_INTEN(ID) \
	do { \
		u32 en = readl_relaxed(RK_GPIO_VIRT(ID) + GPIO_INTEN); \
		if (en) { \
			pr_info("GPIO%d_INTEN: %08x\n", ID, en); \
		} \
	} while (0)

static void rk3036_pm_dump_inten(void)
{
	DUMP_GPIO_INTEN(0);
	DUMP_GPIO_INTEN(1);
	DUMP_GPIO_INTEN(2);
}

static void rkpm_prepare(void)
{
	rk3036_pm_dump_inten();
}
static void rkpm_finish(void)
{
	rk3036_pm_dump_irq();
}

static u32 clk_ungt_msk[RK3036_CRU_CLKGATES_CON_CNT];
/*first clk gating setting*/

static u32 clk_ungt_msk_1[RK3036_CRU_CLKGATES_CON_CNT];
/* first clk gating setting*/

static u32 clk_ungt_save[RK3036_CRU_CLKGATES_CON_CNT];
/*first clk gating value saveing*/

static u32 *p_rkpm_clkgt_last_set;
#define CLK_MSK_GATING(msk, con) cru_writel((msk << 16) | 0xffff, con)
#define CLK_MSK_UNGATING(msk, con) cru_writel(((~msk) << 16) | 0xffff, con)

static void gtclks_suspend(void)
{
	int i;

	for (i = 0; i < RK3036_CRU_CLKGATES_CON_CNT; i++) {
		clk_ungt_save[i] = cru_readl(RK3036_CRU_CLKGATES_CON(i));
		if (i != 10)
			CLK_MSK_UNGATING(clk_ungt_msk[i]
			, RK3036_CRU_CLKGATES_CON(i));
		else
			cru_writel(clk_ungt_msk[i], RK3036_CRU_CLKGATES_CON(i));
	}

	/*gpio0_a1 clk gate should be disable for volt adjust*/
	if (cru_readl(RK3036_CRU_CLKGATES_CON(8)) & 0x200)
		cru_writel(0x02000000, RK3036_CRU_CLKGATES_CON(8));
}

static void gtclks_resume(void)
{
	int i;

	for (i = 0; i < RK3036_CRU_CLKGATES_CON_CNT; i++) {
		if (i != 10)
			cru_writel(clk_ungt_save[i] | 0xffff0000
				, RK3036_CRU_CLKGATES_CON(i));
		else
			cru_writel(clk_ungt_save[i]
				, RK3036_CRU_CLKGATES_CON(i));
	}
}

static void clks_gating_suspend_init(void)
{
	p_rkpm_clkgt_last_set = &clk_ungt_msk_1[0];
	if (clk_suspend_clkgt_info_get(clk_ungt_msk, p_rkpm_clkgt_last_set
		, RK3036_CRU_CLKGATES_CON_CNT) == RK3036_CRU_CLKGATES_CON(0))
		rkpm_set_ops_gtclks(gtclks_suspend, gtclks_resume);
}

#define RK3036_PLL_BYPASS CRU_W_MSK_SETBITS(1, 0xF, 0x01)
#define RK3036_PLL_NOBYPASS CRU_W_MSK_SETBITS(0, 0xF, 0x01)
#define RK3036_PLL_POWERDOWN CRU_W_MSK_SETBITS(1, 0xD, 0x01)
#define RK3036_PLL_POWERON CRU_W_MSK_SETBITS(0, 0xD, 0x01)

#define grf_readl(offset) readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v, offset) do { writel_relaxed(v, \
	RK_GRF_VIRT + offset); dsb(); } while (0)

#define gpio0_readl(offset) readl_relaxed(RK_GPIO_VIRT(0) + offset)
#define gpio0_writel(v, offset) do { writel_relaxed(v, RK_GPIO_VIRT(0) \
	+ offset); dsb(); } while (0)

static u32 plls_con0_save[RK3036_END_PLL_ID];
static u32 plls_con1_save[RK3036_END_PLL_ID];
static u32 plls_con2_save[RK3036_END_PLL_ID];

static u32 cru_mode_con;
static u32 clk_sel0, clk_sel1, clk_sel10;
static void pm_pll_wait_lock(u32 pll_idx)
{
	u32 delay = 600000U;

	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	while (delay > 0) {
		if ((cru_readl(RK3036_PLL_CONS(pll_idx, 1)) & (0x1 << 10)))
			break;
		delay--;
	}
	if (delay == 0) {
		rkpm_ddr_printascii("unlock-pll:");
		rkpm_ddr_printhex(pll_idx);
		rkpm_ddr_printch('\n');
	}
}

static void pll_udelay(u32 udelay)
{
	u32 mode;

	mode = cru_readl(RK3036_CRU_MODE_CON);
	cru_writel(RK3036_PLL_MODE_SLOW(APLL_ID), RK3036_CRU_MODE_CON);
	rkpm_udelay(udelay * 5);
	cru_writel(mode|(RK3036_PLL_MODE_MSK(APLL_ID)
		<< 16), RK3036_CRU_MODE_CON);
}

static inline void plls_suspend(u32 pll_id)
{
	plls_con0_save[pll_id] = cru_readl(RK3036_PLL_CONS((pll_id), 0));
	plls_con1_save[pll_id] = cru_readl(RK3036_PLL_CONS((pll_id), 1));
	plls_con2_save[pll_id] = cru_readl(RK3036_PLL_CONS((pll_id), 2));

	/*cru_writel(RK3036_PLL_BYPASS, RK3036_PLL_CONS((pll_id), 0));*/
	cru_writel(RK3036_PLL_POWERDOWN, RK3036_PLL_CONS((pll_id), 1));
}
static inline void plls_resume(u32 pll_id)
{
	u32 pllcon0, pllcon1, pllcon2;

	pllcon0 = plls_con0_save[pll_id];
	pllcon1 = plls_con1_save[pll_id];
	pllcon2 = plls_con2_save[pll_id];
/*
	cru_writel(pllcon0 | 0xffff0000, RK3036_PLL_CONS(pll_id, 0));
	cru_writel(pllcon1 | 0xf5ff0000, RK3036_PLL_CONS(pll_id, 1));
	cru_writel(pllcon2, RK3036_PLL_CONS(pll_id, 2));
*/
	cru_writel(RK3036_PLL_POWERON, RK3036_PLL_CONS((pll_id), 1));

	pll_udelay(5);

	pll_udelay(168);
	pm_pll_wait_lock(pll_id);
}

static void pm_plls_suspend(void)
{
	cru_mode_con  = cru_readl(RK3036_CRU_MODE_CON);

	clk_sel0 = cru_readl(RK3036_CRU_CLKSELS_CON(0));
	clk_sel1 = cru_readl(RK3036_CRU_CLKSELS_CON(1));
	clk_sel10 = cru_readl(RK3036_CRU_CLKSELS_CON(10));

	cru_writel(RK3036_PLL_MODE_SLOW(GPLL_ID), RK3036_CRU_MODE_CON);
	cru_writel(0
						|CRU_W_MSK_SETBITS(0, 0, 0x1f)
						|CRU_W_MSK_SETBITS(0, 8, 0x3)
						|CRU_W_MSK_SETBITS(0, 12, 0x3)
						, RK3036_CRU_CLKSELS_CON(10));
	plls_suspend(GPLL_ID);


	cru_writel(RK3036_PLL_MODE_SLOW(APLL_ID), RK3036_CRU_MODE_CON);

	cru_writel(0
						|CRU_W_MSK_SETBITS(0, 0, 0x1f)
						|CRU_W_MSK_SETBITS(0, 8, 0x1f)
					  , RK3036_CRU_CLKSELS_CON(0));

	cru_writel(0
						|CRU_W_MSK_SETBITS(0, 0, 0xf)
						|CRU_W_MSK_SETBITS(0, 4, 0x7)
						|CRU_W_MSK_SETBITS(0, 8, 0x3)
						|CRU_W_MSK_SETBITS(0, 12, 0x7)
					 , RK3036_CRU_CLKSELS_CON(1));

	plls_suspend(APLL_ID);
}

static void pm_plls_resume(void)
{
	plls_resume(APLL_ID);
	cru_writel(clk_sel0 | (CRU_W_MSK(0, 0x1f) | CRU_W_MSK(8, 0x1f))
		, RK3036_CRU_CLKSELS_CON(0));
	cru_writel(clk_sel1 | (CRU_W_MSK(0, 0xf) | CRU_W_MSK(4, 0x7)
		|CRU_W_MSK(8, 0x3) | CRU_W_MSK(12, 0x7))
		, RK3036_CRU_CLKSELS_CON(1));
	cru_writel(cru_mode_con | (RK3036_PLL_MODE_MSK(APLL_ID) << 16)
		, RK3036_CRU_MODE_CON);

	plls_resume(GPLL_ID);
	cru_writel(clk_sel10 | (CRU_W_MSK(0, 0x1f) | CRU_W_MSK(8, 0x3)
		| CRU_W_MSK(12, 0x3)), RK3036_CRU_CLKSELS_CON(10));
	cru_writel(cru_mode_con | (RK3036_PLL_MODE_MSK(GPLL_ID)
		<< 16), RK3036_CRU_MODE_CON);
}

#include "ddr_rk3036.c"
#include "pm-pie.c"

char PIE_DATA(sram_stack)[1024];
EXPORT_PIE_SYMBOL(DATA(sram_stack));

static int __init rk3036_pie_init(void)
{
	int err;

	if (!cpu_is_rk3036())
		return 0;

	err = rockchip_pie_init();
	if (err)
		return err;

	rockchip_pie_chunk = pie_load_sections(rockchip_sram_pool, rk3036);
	if (IS_ERR(rockchip_pie_chunk)) {
		err = PTR_ERR(rockchip_pie_chunk);
		pr_err("%s: failed to load section %d\n", __func__, err);
		rockchip_pie_chunk = NULL;
		return err;
	}

	rockchip_sram_virt = kern_to_pie(rockchip_pie_chunk
		, &__pie_common_start[0]);
	rockchip_sram_stack = kern_to_pie(rockchip_pie_chunk
		, (char *)DATA(sram_stack) + sizeof(DATA(sram_stack)));

	return 0;
}
arch_initcall(rk3036_pie_init);

static void reg_pread(void)
{
	volatile u32 n;
	int i;

	volatile u32 *temp = (volatile unsigned int *)rockchip_sram_virt;

	flush_cache_all();
	outer_flush_all();
	local_flush_tlb_all();

	for (i = 0; i < 2; i++) {
		n = temp[1024 * i];
		barrier();
	}

	n = readl_relaxed(RK_GPIO_VIRT(0));
	n = readl_relaxed(RK_GPIO_VIRT(1));
	n = readl_relaxed(RK_GPIO_VIRT(2));

	n = readl_relaxed(RK_DEBUG_UART_VIRT);
	n = readl_relaxed(RK_CPU_AXI_BUS_VIRT);
	n = readl_relaxed(RK_DDR_VIRT);
	n = readl_relaxed(RK_GRF_VIRT);
	n = readl_relaxed(RK_CRU_VIRT);
	n = readl_relaxed(RK_PWM_VIRT);
}

#define RK3036_CRU_UNGATING_OPS(id) cru_writel(\
	CRU_W_MSK_SETBITS(0,  (id), 0x1), RK3036_CRU_UART_GATE)
#define RK3036_CRU_GATING_OPS(id) cru_writel(\
	CRU_W_MSK_SETBITS(1, (id), 0x1), RK3036_CRU_UART_GATE)

static inline void  uart_printch(char bbyte)
{
	u32 reg_save;
	u32 u_clk_id = (RK3036_CLKGATE_UART0_SRC + CONFIG_RK_DEBUG_UART * 2);
	u32 u_pclk_id = (RK3036_CLKGATE_UART0_PCLK + CONFIG_RK_DEBUG_UART * 2);

	reg_save = cru_readl(RK3036_CRU_UART_GATE);
	RK3036_CRU_UNGATING_OPS(u_clk_id);
	RK3036_CRU_UNGATING_OPS(u_pclk_id);
	rkpm_udelay(1);


write_uart:
	writel_relaxed(bbyte, RK_DEBUG_UART_VIRT);
	dsb();

	while (!(readl_relaxed(RK_DEBUG_UART_VIRT + 0x14) & 0x40))
		barrier();

	if (bbyte == '\n') {
		bbyte = '\r';
		goto write_uart;
	}

	cru_writel(reg_save | CRU_W_MSK(u_clk_id
		, 0x1), RK3036_CRU_UART_GATE);
	cru_writel(reg_save | CRU_W_MSK(u_pclk_id
		, 0x1), RK3036_CRU_UART_GATE);


	if (0) {
write_uart1:
		writel_relaxed(bbyte, RK_DEBUG_UART_VIRT);
		dsb();

		while (!(readl_relaxed(RK_DEBUG_UART_VIRT + 0x14) & 0x40))
			barrier();
	if (bbyte == '\n') {
		bbyte = '\r';
		goto write_uart1;
		}
	}
}


void PIE_FUNC(sram_printch)(char byte)
{
	uart_printch(byte);
}

static __sramdata u32 rkpm_pwm_duty0;
static __sramdata u32 rkpm_pwm_duty1;
static __sramdata u32 rkpm_pwm_duty2;
#define PWM_VOLTAGE 0x600

void PIE_FUNC(pwm_regulator_suspend)(void)
{
	if (rkpm_chk_sram_ctrbit(RKPM_CTR_VOL_PWM0)) {
		rkpm_pwm_duty0 = readl_relaxed(RK_PWM_VIRT + 0x08);
		writel_relaxed(PWM_VOLTAGE, RK_PWM_VIRT + 0x08);
	}

	if (rkpm_chk_sram_ctrbit(RKPM_CTR_VOL_PWM1)) {
		rkpm_pwm_duty1 = readl_relaxed(RK_PWM_VIRT + 0x18);
		writel_relaxed(PWM_VOLTAGE, RK_PWM_VIRT + 0x18);
	}

	if (rkpm_chk_sram_ctrbit(RKPM_CTR_VOL_PWM2)) {
		FUNC(sram_printch)('p');
		FUNC(sram_printch)('o');
		FUNC(sram_printch)('l');
	rkpm_pwm_duty2 = readl_relaxed(RK_PWM_VIRT + 0x28);
	writel_relaxed(PWM_VOLTAGE, RK_PWM_VIRT + 0x28);
	}
	rkpm_udelay(30);
}

void PIE_FUNC(pwm_regulator_resume)(void)
{
	rkpm_udelay(30);


	if (rkpm_chk_sram_ctrbit(RKPM_CTR_VOL_PWM0))
		writel_relaxed(rkpm_pwm_duty0, RK_PWM_VIRT + 0x08);

	if (rkpm_chk_sram_ctrbit(RKPM_CTR_VOL_PWM1))
		writel_relaxed(rkpm_pwm_duty1, RK_PWM_VIRT + 0x18);

	if (rkpm_chk_sram_ctrbit(RKPM_CTR_VOL_PWM2))
		writel_relaxed(rkpm_pwm_duty2, RK_PWM_VIRT + 0x28);
	rkpm_udelay(30);
}

static void __init rk3036_suspend_init(void)
{
	struct device_node *parent;
	u32 pm_ctrbits;

	PM_LOG("%s enter\n", __func__);

	parent = of_find_node_by_name(NULL, "rockchip_suspend");

	if (IS_ERR_OR_NULL(parent)) {
		PM_ERR("%s dev node err\n", __func__);
		return;
	}

	if (of_property_read_u32_array(parent, "rockchip,ctrbits"
		, &pm_ctrbits, 1)) {
			PM_ERR("%s:get pm ctr error\n", __func__);
			return;
	}
	PM_LOG("%s: pm_ctrbits =%x\n", __func__, pm_ctrbits);
	rkpm_set_ctrbits(pm_ctrbits);

	clks_gating_suspend_init();
	rkpm_set_ops_prepare_finish(rkpm_prepare, rkpm_finish);
	rkpm_set_ops_plls(pm_plls_suspend, pm_plls_resume);

	rkpm_set_ops_regs_pread(reg_pread);
	rkpm_set_sram_ops_ddr(fn_to_pie(rockchip_pie_chunk
		, &FUNC(ddr_suspend))
		, fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_resume)));

	rkpm_set_sram_ops_volt(fn_to_pie(rockchip_pie_chunk
		, &FUNC(pwm_regulator_suspend))
		, fn_to_pie(rockchip_pie_chunk, &FUNC(pwm_regulator_resume)));


	rkpm_set_sram_ops_printch(fn_to_pie(rockchip_pie_chunk
		, &FUNC(sram_printch)));
	rkpm_set_ops_printch(rk3036_ddr_printch);
}
#endif

static void __init rk3036_init_suspend(void)
{
	pr_info("%s\n", __func__);
	rockchip_suspend_init();
	rkpm_pie_init();
	rk3036_suspend_init();
}

static void __init rk3036_init_late(void)
{
#ifdef CONFIG_PM
	rk3036_init_suspend();
#endif
}

static void __init rk3036_reserve(void)
{
	/* reserve memory for ION */
	rockchip_ion_reserve();
}

static void rk3036_restart(char mode, const char *cmd)
{
	u32 boot_flag, boot_mode;

	rockchip_restart_get_boot_mode(cmd, &boot_flag, &boot_mode);
	/* for loader */
	writel_relaxed(boot_flag, RK_GRF_VIRT + RK3036_GRF_OS_REG4);
	/* for linux */
	writel_relaxed(boot_mode, RK_GRF_VIRT + RK3036_GRF_OS_REG5);
	dsb();

	/* pll enter slow mode */
	writel_relaxed(0x30110000, RK_CRU_VIRT + RK3036_CRU_MODE_CON);
	dsb();
	writel_relaxed(0xeca8, RK_CRU_VIRT + RK3036_CRU_GLB_SRST_SND_VALUE);
	dsb();
}

static const char *const rk3036_dt_compat[] __initconst = {
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
