// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/suspend.h>
#include <linux/mfd/syscon.h>

#include <asm/cacheflush.h>
#include <asm/fiq_glue.h>
#include <asm/tlbflush.h>
#include <asm/suspend.h>
#include <linux/irqchip/arm-gic.h>

#include "rkpm_gicv2.h"
#include "rkpm_helpers.h"
#include "rkpm_uart.h"

#include "rv1106_pm.h"

#define RV1106_PM_REG_REGION_MEM_SIZE		SZ_4K

enum {
	RV1106_GPIO_PULL_NONE,
	RV1106_GPIO_PULL_UP,
	RV1106_GPIO_PULL_DOWN,
	RV1106_GPIO_PULL_UP_DOWN,
};

struct rockchip_pm_data {
	const struct platform_suspend_ops *ops;
	int (*init)(struct device_node *np);
};

struct rv1106_sleep_ddr_data {
	u32 cru_gate_con[RV1106_CRU_GATE_CON_NUM];
	u32 pmucru_gate_con[RV1106_PMUCRU_GATE_CON_NUM];
	u32 pericru_gate_con[RV1106_PERICRU_GATE_CON_NUM];
	u32 npucru_gate_con[RV1106_NPUCRU_GATE_CON_NUM];
	u32 venccru_gate_con[RV1106_VENCCRU_GATE_CON_NUM];
	u32 vicru_gate_con[RV1106_VICRU_GATE_CON_NUM];
	u32 vocru_gate_con[RV1106_VOCRU_GATE_CON_NUM];

	u32 ddrgrf_con1, ddrgrf_con2, ddrgrf_con3, ddrc_pwrctrl, ddrc_dfilpcfg0;
	u32 pmucru_sel_con7;
	u32 pmugrf_soc_con0, pmugrf_soc_con1, pmugrf_soc_con4, pmugrf_soc_con5;
	u32 ioc0_1a_iomux_l, ioc1_1a_iomux_l;
	u32 gpio0a_iomux_l, gpio0a_iomux_h, gpio0a0_pull;
	u32 gpio0_ddr_l, gpio0_ddr_h;
	u32 pmu_wkup_int_st, gpio0_int_st;
};

static struct rv1106_sleep_ddr_data ddr_data;

static void __iomem *pmucru_base;
static void __iomem *cru_base;
static void __iomem *pvtpllcru_base;
static void __iomem *pericru_base;
static void __iomem *vicru_base;
static void __iomem *npucru_base;
static void __iomem *corecru_base;
static void __iomem *venccru_base;
static void __iomem *vocru_base;

static void __iomem *perigrf_base;
static void __iomem *vencgrf_base;
static void __iomem *npugrf_base;
static void __iomem *pmugrf_base;
static void __iomem *ddrgrf_base;
static void __iomem *coregrf_base;
static void __iomem *vigrf_base;
static void __iomem *vogrf_base;
static void __iomem *perisgrf_base;
static void __iomem *visgrf_base;
static void __iomem *npusgrf_base;
static void __iomem *coresgrf_base;
static void __iomem *vencsgrf_base;
static void __iomem *vosgrf_base;
static void __iomem *pmusgrf_base;

static void __iomem *pmupvtm_base;
static void __iomem *uartdbg_base;
static void __iomem *pmu_base;
static void __iomem *gicd_base;
static void __iomem *gicc_base;
static void __iomem *firewall_ddr_base;
static void __iomem *firewall_syssram_base;
static void __iomem *pmu_base;
static void __iomem *nstimer_base;
static void __iomem *stimer_base;
static void __iomem *mbox_base;
static void __iomem *ddrc_base;
static void __iomem *ioc_base[5];
static void __iomem *gpio_base[5];
static void __iomem *rv1106_bootram_base;

#define WMSK_VAL		0xffff0000

static struct reg_region vd_core_reg_rgns[] = {
	/* core_cru */
	{ REG_REGION(0x300, 0x310, 4, &corecru_base, WMSK_VAL)},
	{ REG_REGION(0x800, 0x804, 4, &corecru_base, WMSK_VAL)},

	/* pvtpll_cru */
	{ REG_REGION(0x00, 0x24, 4, &pvtpllcru_base, WMSK_VAL)},
	{ REG_REGION(0x30, 0x54, 4, &pvtpllcru_base, WMSK_VAL)},

	/* core_sgrf */
	{ REG_REGION(0x004, 0x014, 4, &coresgrf_base, 0)},
	{ REG_REGION(0x000, 0x000, 4, &coresgrf_base, 0)},
	{ REG_REGION(0x020, 0x030, 4, &coresgrf_base, WMSK_VAL)},
	{ REG_REGION(0x040, 0x040, 4, &coresgrf_base, WMSK_VAL)},
	{ REG_REGION(0x044, 0x044, 4, &coresgrf_base, 0)},

	/* core grf */
	{ REG_REGION(0x004, 0x004, 4, &coregrf_base, WMSK_VAL)},
	{ REG_REGION(0x008, 0x010, 4, &coregrf_base, 0)},
	{ REG_REGION(0x024, 0x028, 4, &coregrf_base, 0)},
	{ REG_REGION(0x000, 0x000, 4, &coregrf_base, WMSK_VAL)},
	{ REG_REGION(0x02c, 0x02c, 4, &coregrf_base, WMSK_VAL)},
	{ REG_REGION(0x038, 0x03c, 4, &coregrf_base, WMSK_VAL)},
};

static struct reg_region vd_log_reg_rgns[] = {
	/* firewall_ddr */
	{ REG_REGION(0x000, 0x03c, 4, &firewall_ddr_base, 0)},
	{ REG_REGION(0x040, 0x06c, 4, &firewall_ddr_base, 0)},
	{ REG_REGION(0x0f0, 0x0f0, 4, &firewall_ddr_base, 0)},

	/* firewall_sram */
	{ REG_REGION(0x000, 0x01c, 4, &firewall_syssram_base, 0)},
	{ REG_REGION(0x040, 0x054, 4, &firewall_syssram_base, 0)},
	{ REG_REGION(0x0f0, 0x0f0, 4, &firewall_syssram_base, 0)},

	/* cru */
	{ REG_REGION(0x000, 0x004, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x008, 0x008, 4, &cru_base, 0)},
	{ REG_REGION(0x00c, 0x010, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x020, 0x024, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x028, 0x028, 4, &cru_base, 0)},
	{ REG_REGION(0x02c, 0x030, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x060, 0x064, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x068, 0x068, 4, &cru_base, 0)},
	{ REG_REGION(0x06c, 0x070, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x140, 0x1bc, 4, &cru_base, 0)},
	/* { REG_REGION(0x280, 0x280, 4, &cru_base, WMSK_VAL)}, */
	{ REG_REGION(0x300, 0x310, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x314, 0x34c, 8, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x318, 0x350, 8, &cru_base, 0)},
	{ REG_REGION(0x354, 0x360, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x364, 0x37c, 8, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x368, 0x380, 8, &cru_base, 0)},
	{ REG_REGION(0x384, 0x384, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0x800, 0x80c, 4, &cru_base, WMSK_VAL)},
	{ REG_REGION(0xc00, 0xc00, 4, &cru_base, 0)},
	{ REG_REGION(0xc10, 0xc10, 4, &cru_base, 0)},
	{ REG_REGION(0xc14, 0xc28, 4, &cru_base, WMSK_VAL)},

	/* npu_cru */
	{ REG_REGION(0x300, 0x300, 4, &npucru_base, WMSK_VAL)},
	{ REG_REGION(0x800, 0x804, 4, &npucru_base, WMSK_VAL)},

	/* npu_sgrf */
	{ REG_REGION(0x004, 0x014, 4, &npusgrf_base, 0)},
	{ REG_REGION(0x000, 0x000, 4, &npusgrf_base, 0)},

	/* peri_cru */
	{ REG_REGION(0x304, 0x32c, 4, &pericru_base, WMSK_VAL)},
	{ REG_REGION(0x800, 0x81c, 4, &pericru_base, WMSK_VAL)},

	/* peri_sgrf */
	{ REG_REGION(0x004, 0x014, 4, &perisgrf_base, 0)},
	{ REG_REGION(0x000, 0x000, 4, &perisgrf_base, 0)},
	{ REG_REGION(0x020, 0x030, 4, &perisgrf_base, WMSK_VAL)},
	{ REG_REGION(0x080, 0x0a4, 4, &perisgrf_base, WMSK_VAL)},
	{ REG_REGION(0x0b8, 0x0bc, 4, &perisgrf_base, WMSK_VAL)},

	/* vi_cru */
	{ REG_REGION(0x300, 0x30c, 4, &vicru_base, WMSK_VAL)},
	{ REG_REGION(0x800, 0x808, 4, &vicru_base, WMSK_VAL)},

	/* vi_sgrf */
	{ REG_REGION(0x004, 0x014, 4, &visgrf_base, 0)},
	{ REG_REGION(0x000, 0x000, 4, &visgrf_base, 0)},

	/* vo_cru */
	{ REG_REGION(0x300, 0x30c, 4, &vocru_base, WMSK_VAL)},
	{ REG_REGION(0x800, 0x80c, 4, &vocru_base, WMSK_VAL)},

	/* vo_sgrf */
	{ REG_REGION(0x004, 0x014, 4, &vosgrf_base, 0)},
	{ REG_REGION(0x000, 0x000, 4, &vosgrf_base, 0)},
	{ REG_REGION(0x018, 0x018, 4, &vosgrf_base, WMSK_VAL)},

	/* vepu_cru */
	{ REG_REGION(0x300, 0x304, 4, &venccru_base, WMSK_VAL)},
	{ REG_REGION(0x800, 0x808, 4, &venccru_base, WMSK_VAL)},

	/* vepu_sgrf */
	{ REG_REGION(0x004, 0x014, 4, &vencsgrf_base, 0)},
	{ REG_REGION(0x000, 0x000, 4, &vencsgrf_base, 0)},

	/* gpio1_ioc */
	{ REG_REGION(0x000, 0x018, 4, &ioc_base[1], WMSK_VAL)},
	{ REG_REGION(0x080, 0x0b4, 4, &ioc_base[1], WMSK_VAL)},
	{ REG_REGION(0x180, 0x18c, 4, &ioc_base[1], WMSK_VAL)},
	{ REG_REGION(0x1c0, 0x1cc, 4, &ioc_base[1], WMSK_VAL)},
	{ REG_REGION(0x200, 0x20c, 4, &ioc_base[1], WMSK_VAL)},
	{ REG_REGION(0x240, 0x24c, 4, &ioc_base[1], WMSK_VAL)},
	{ REG_REGION(0x280, 0x28c, 4, &ioc_base[1], WMSK_VAL)},
	{ REG_REGION(0x2c0, 0x2cc, 4, &ioc_base[1], WMSK_VAL)},
	{ REG_REGION(0x2f4, 0x2f4, 4, &ioc_base[1], WMSK_VAL)},

	/* gpio2_ioc */
	{ REG_REGION(0x020, 0x028, 4, &ioc_base[2], WMSK_VAL)},
	{ REG_REGION(0x0c0, 0x0d0, 4, &ioc_base[2], WMSK_VAL)},
	{ REG_REGION(0x190, 0x194, 4, &ioc_base[2], WMSK_VAL)},
	{ REG_REGION(0x1d0, 0x1d4, 4, &ioc_base[2], WMSK_VAL)},
	{ REG_REGION(0x210, 0x214, 4, &ioc_base[2], WMSK_VAL)},
	{ REG_REGION(0x250, 0x254, 4, &ioc_base[2], WMSK_VAL)},
	{ REG_REGION(0x290, 0x294, 4, &ioc_base[2], WMSK_VAL)},
	{ REG_REGION(0x2d0, 0x2d4, 4, &ioc_base[2], WMSK_VAL)},

	/* gpio3_ioc */
	{ REG_REGION(0x040, 0x058, 4, &ioc_base[3], WMSK_VAL)},
	{ REG_REGION(0x100, 0x10c, 4, &ioc_base[3], WMSK_VAL)},
	{ REG_REGION(0x128, 0x134, 4, &ioc_base[3], WMSK_VAL)},
	{ REG_REGION(0x1a0, 0x1ac, 4, &ioc_base[3], WMSK_VAL)},
	{ REG_REGION(0x1e0, 0x1ec, 4, &ioc_base[3], WMSK_VAL)},
	{ REG_REGION(0x220, 0x22c, 4, &ioc_base[3], WMSK_VAL)},
	{ REG_REGION(0x260, 0x26c, 4, &ioc_base[3], WMSK_VAL)},
	{ REG_REGION(0x2a0, 0x2ac, 4, &ioc_base[3], WMSK_VAL)},
	{ REG_REGION(0x2e0, 0x2ec, 4, &ioc_base[3], WMSK_VAL)},
	{ REG_REGION(0x2f4, 0x2f4, 4, &ioc_base[3], WMSK_VAL)},

	/* gpio1~3 */
	{ REG_REGION(0x000, 0x00c, 4, &gpio_base[1], WMSK_VAL)},
	{ REG_REGION(0x018, 0x044, 4, &gpio_base[1], WMSK_VAL)},
	{ REG_REGION(0x048, 0x048, 4, &gpio_base[1], 0)},
	{ REG_REGION(0x060, 0x064, 4, &gpio_base[1], WMSK_VAL)},
	{ REG_REGION(0x100, 0x108, 4, &gpio_base[1], WMSK_VAL)},
	{ REG_REGION(0x010, 0x014, 4, &gpio_base[1], WMSK_VAL)},

	{ REG_REGION(0x000, 0x00c, 4, &gpio_base[2], WMSK_VAL)},
	{ REG_REGION(0x018, 0x044, 4, &gpio_base[2], WMSK_VAL)},
	{ REG_REGION(0x048, 0x048, 4, &gpio_base[2], 0)},
	{ REG_REGION(0x060, 0x064, 4, &gpio_base[2], WMSK_VAL)},
	{ REG_REGION(0x100, 0x108, 4, &gpio_base[2], WMSK_VAL)},
	{ REG_REGION(0x010, 0x014, 4, &gpio_base[2], WMSK_VAL)},

	{ REG_REGION(0x000, 0x00c, 4, &gpio_base[3], WMSK_VAL)},
	{ REG_REGION(0x018, 0x044, 4, &gpio_base[3], WMSK_VAL)},
	{ REG_REGION(0x048, 0x048, 4, &gpio_base[3], 0)},
	{ REG_REGION(0x060, 0x064, 4, &gpio_base[3], WMSK_VAL)},
	{ REG_REGION(0x100, 0x108, 4, &gpio_base[3], WMSK_VAL)},
	{ REG_REGION(0x010, 0x014, 4, &gpio_base[3], WMSK_VAL)},

	/* NS TIMER 6 channel */
	{ REG_REGION(0x00, 0x04, 4, &nstimer_base, 0)},
	{ REG_REGION(0x10, 0x10, 4, &nstimer_base, 0)},
	{ REG_REGION(0x20, 0x24, 4, &nstimer_base, 0)},
	{ REG_REGION(0x30, 0x30, 4, &nstimer_base, 0)},
	{ REG_REGION(0x40, 0x44, 4, &nstimer_base, 0)},
	{ REG_REGION(0x50, 0x50, 4, &nstimer_base, 0)},
	{ REG_REGION(0x60, 0x64, 4, &nstimer_base, 0)},
	{ REG_REGION(0x70, 0x70, 4, &nstimer_base, 0)},
	{ REG_REGION(0x80, 0x84, 4, &nstimer_base, 0)},
	{ REG_REGION(0x90, 0x90, 4, &nstimer_base, 0)},
	{ REG_REGION(0xa0, 0xa4, 4, &nstimer_base, 0)},
	{ REG_REGION(0xb0, 0xb0, 4, &nstimer_base, 0)},

	/* S TIMER0 2 channel */
	{ REG_REGION(0x00, 0x04, 4, &stimer_base, 0)},
	{ REG_REGION(0x10, 0x10, 4, &stimer_base, 0)},
	{ REG_REGION(0x20, 0x24, 4, &stimer_base, 0)},
	{ REG_REGION(0x30, 0x30, 4, &stimer_base, 0)},
};

#define PLL_LOCKED_TIMEOUT		600000U

static void pm_pll_wait_lock(u32 pll_id)
{
	int delay = PLL_LOCKED_TIMEOUT;

	if (readl_relaxed(cru_base + RV1106_CRU_PLL_CON(pll_id, 1)) & CRU_PLLCON1_PWRDOWN)
		return;

	while (delay-- >= 0) {
		if (readl_relaxed(cru_base + RV1106_CRU_PLL_CON(pll_id, 1)) &
		    CRU_PLLCON1_LOCK_STATUS)
			break;

		rkpm_raw_udelay(1);
	}

	if (delay <= 0) {
		rkpm_printstr("Can't wait pll lock: ");
		rkpm_printhex(pll_id);
		rkpm_printch('\n');
	}
}

struct plat_gicv2_dist_ctx_t gicd_ctx_save;
struct plat_gicv2_cpu_ctx_t gicc_ctx_save;

static void gic400_save(void)
{
	rkpm_gicv2_cpu_save(gicd_base, gicc_base, &gicc_ctx_save);
	rkpm_gicv2_dist_save(gicd_base, &gicd_ctx_save);
}

static void gic400_restore(void)
{
	if (IS_ENABLED(CONFIG_RV1106_HPMCU_FAST_WAKEUP))
		writel_relaxed(0x3, gicd_base + GIC_DIST_CTRL);
	else
		rkpm_gicv2_dist_restore(gicd_base, &gicd_ctx_save);
	rkpm_gicv2_cpu_restore(gicd_base, gicc_base, &gicc_ctx_save);
}

static void uart_wrtie_byte(uint8_t byte)
{
	writel_relaxed(byte, uartdbg_base + 0x0);

	while (!(readl_relaxed(uartdbg_base + 0x14) & 0x40))
		;
}

void rkpm_printch(int c)
{
	if (c == '\n')
		uart_wrtie_byte('\r');

	uart_wrtie_byte(c);
}

#define RV1106_DUMP_GPIO_INTEN(id)							\
	do {										\
		rkpm_printstr("GPIO");							\
		rkpm_printdec(id);							\
		rkpm_printstr(": ");							\
		rkpm_printhex(readl_relaxed(gpio_base[id] + RV1106_GPIO_INT_EN_L));	\
		rkpm_printch(' ');							\
		rkpm_printhex(readl_relaxed(gpio_base[id] + RV1106_GPIO_INT_EN_H));	\
		rkpm_printch(' ');							\
		rkpm_printhex(readl_relaxed(gpio_base[id] + RV1106_GPIO_INT_MASK_L));	\
		rkpm_printch(' ');							\
		rkpm_printhex(readl_relaxed(gpio_base[id] + RV1106_GPIO_INT_MASK_H));	\
		rkpm_printch(' ');							\
		rkpm_printhex(readl_relaxed(gpio_base[id] + RV1106_GPIO_INT_STATUS));	\
		rkpm_printch(' ');							\
		rkpm_printhex(readl_relaxed(gpio_base[id] + RV1106_GPIO_INT_RAWSTATUS));\
		rkpm_printch('\n');							\
	} while (0)

static void rv1106_dbg_pmu_wkup_src(void)
{
	u32 pmu_int_st = ddr_data.pmu_wkup_int_st;

	rkpm_printstr("wake up status:");
	rkpm_printhex(pmu_int_st);
	rkpm_printch('\n');

	if (pmu_int_st)
		rkpm_printstr("wake up information:\n");

	if (pmu_int_st & BIT(RV1106_PMU_WAKEUP_GPIO_INT_EN)) {
		rkpm_printstr("GPIO0 interrupt wakeup:");
		rkpm_printhex(ddr_data.gpio0_int_st);
		rkpm_printch('\n');
	}

	if (pmu_int_st & BIT(RV1106_PMU_WAKEUP_SDMMC_EN))
		rkpm_printstr("PWM detect wakeup\n");

	if (pmu_int_st & BIT(RV1106_PMU_WAKEUP_SDIO_EN))
		rkpm_printstr("GMAC interrupt wakeup\n");

	if (pmu_int_st & BIT(RV1106_PMU_WAKEUP_TIMER_EN))
		rkpm_printstr("TIMER interrupt wakeup\n");

	if (pmu_int_st & BIT(RV1106_PMU_WAKEUP_USBDEV_EN))
		rkpm_printstr("USBDEV detect wakeup\n");

	if (pmu_int_st & BIT(RV1106_PMU_WAKEUP_TIMEROUT_EN))
		rkpm_printstr("TIMEOUT interrupt wakeup\n");

	rkpm_printch('\n');
}

static void rv1106_dbg_irq_prepare(void)
{
	RV1106_DUMP_GPIO_INTEN(0);
}

static void rv1106_dbg_irq_finish(void)
{
	rv1106_dbg_pmu_wkup_src();
}

static inline u32 rv1106_l2_config(void)
{
	u32 l2ctlr;

	asm("mrc p15, 1, %0, c9, c0, 2" : "=r" (l2ctlr));
	return l2ctlr;
}

static void __init rv1106_config_bootdata(void)
{
	rkpm_bootdata_cpusp = RV1106_PMUSRAM_BASE + (SZ_8K - 8);
	rkpm_bootdata_cpu_code = __pa_symbol(cpu_resume);

	rkpm_bootdata_l2ctlr_f = 1;
	rkpm_bootdata_l2ctlr = rv1106_l2_config();
}

static void writel_clrset_bits(u32 clr, u32 set, void __iomem *addr)
{
	u32 val = readl_relaxed(addr);

	val &= ~clr;
	val |= set;
	writel_relaxed(val, addr);
}

static void gic_irq_en(int irq)
{
	writel_clrset_bits(0xff << irq % 4 * 8, 0x1 << irq % 4 * 8,
			   gicd_base + GIC_DIST_TARGET + (irq >> 2 << 2));
	writel_clrset_bits(0xff << irq % 4 * 8, 0xa0 << irq % 4 * 8,
			   gicd_base + GIC_DIST_PRI + (irq >> 2 << 2));
	writel_clrset_bits(0x3 << irq % 16 * 2, 0x1 << irq % 16 * 2,
			   gicd_base + GIC_DIST_CONFIG + (irq >> 4 << 2));
	writel_clrset_bits(BIT(irq % 32), BIT(irq % 32),
			   gicd_base + GIC_DIST_IGROUP + (irq >> 5 << 2));

	dsb(sy);
	writel_relaxed(0x1 << irq % 32, gicd_base + GIC_DIST_ENABLE_SET + (irq >> 5 << 2));
	dsb(sy);
}

static int is_hpmcu_mbox_int(void)
{
	return !!(readl(mbox_base + RV1106_MBOX_B2A_STATUS) & BIT(0));
}

static void hpmcu_start(void)
{
	/* enable hpmcu mailbox AP irq */
	gic_irq_en(RV1106_HPMCU_MBOX_IRQ_AP);

	/* tell hpmcu that we are currently in system wake up. */
	writel(RV1106_SYS_IS_WKUP, pmu_base + RV1106_PMU_SYS_REG(0));

	/* set the mcu uncache area, usually set the devices address */
	writel(0xff000, coregrf_base + RV1106_COREGRF_CACHE_PERI_ADDR_START);
	writel(0xffc00, coregrf_base + RV1106_COREGRF_CACHE_PERI_ADDR_END);
	/* Reset the hp mcu */
	writel(0x1e001e, corecru_base + RV1106_COERCRU_SFTRST_CON(1));
	/* set the mcu addr */
	writel(RV1106_HPMCU_BOOT_ADDR,
	       coresgrf_base + RV1106_CORESGRF_HPMCU_BOOTADDR);
	dsb(sy);

	/* release the mcu */
	writel(0x1e0000, corecru_base + RV1106_COERCRU_SFTRST_CON(1));
	dsb(sy);
}

static int hpmcu_fast_wkup(void)
{
	u32 cmd;

	hpmcu_start();

	while (1) {
		rkpm_printstr("-s-\n");
		dsb(sy);
		wfi();
		rkpm_printstr("-w-\n");

		if (is_hpmcu_mbox_int()) {
			rkpm_printstr("-h-mbox-\n");
			/* clear system wake up state */
			writel(0, pmu_base + RV1106_PMU_SYS_REG(0));
			writel(BIT(0), mbox_base + RV1106_MBOX_B2A_STATUS);
			break;
		}
	}

	cmd = readl(mbox_base + RV1106_MBOX_B2A_CMD_0);
	if (cmd == RV1106_MBOX_CMD_AP_SUSPEND)
		return 1;
	else
		return 0;
}

static void clock_suspend(void)
{
	int i;

	for (i = 0; i < RV1106_CRU_GATE_CON_NUM; i++) {
		ddr_data.cru_gate_con[i] =
			readl_relaxed(cru_base + RV1106_CRU_GATE_CON(i));
		writel_relaxed(0xffff0000, cru_base + RV1106_CRU_GATE_CON(i));
	}

	for (i = 0; i < RV1106_PMUCRU_GATE_CON_NUM; i++) {
		ddr_data.pmucru_gate_con[i] =
			readl_relaxed(pmucru_base + RV1106_PMUCRU_GATE_CON(i));
		writel_relaxed(0xffff0000, pmucru_base + RV1106_PMUCRU_GATE_CON(i));
	}

	for (i = 0; i < RV1106_PERICRU_GATE_CON_NUM; i++) {
		ddr_data.pericru_gate_con[i] =
			readl_relaxed(pericru_base + RV1106_PERICRU_GATE_CON(i));
		writel_relaxed(0xffff0000, pericru_base + RV1106_PERICRU_GATE_CON(i));
	}

	for (i = 0; i < RV1106_NPUCRU_GATE_CON_NUM; i++) {
		ddr_data.npucru_gate_con[i] =
			readl_relaxed(npucru_base + RV1106_NPUCRU_GATE_CON(i));
		writel_relaxed(0xffff0000, npucru_base + RV1106_NPUCRU_GATE_CON(i));
	}

	for (i = 0; i < RV1106_VENCCRU_GATE_CON_NUM; i++) {
		ddr_data.venccru_gate_con[i] =
			readl_relaxed(venccru_base + RV1106_VENCCRU_GATE_CON(i));
		writel_relaxed(0xffff0000, venccru_base + RV1106_VENCCRU_GATE_CON(i));
	}

	for (i = 0; i < RV1106_VICRU_GATE_CON_NUM; i++) {
		ddr_data.vicru_gate_con[i] =
			readl_relaxed(vicru_base + RV1106_VICRU_GATE_CON(i));
		writel_relaxed(0xffff0000, vicru_base + RV1106_VICRU_GATE_CON(i));
	}

	for (i = 0; i < RV1106_VOCRU_GATE_CON_NUM; i++) {
		ddr_data.vocru_gate_con[i] =
			readl_relaxed(vocru_base + RV1106_VOCRU_GATE_CON(i));
		writel_relaxed(0xffff0000, vocru_base + RV1106_VOCRU_GATE_CON(i));
	}
}

static void clock_resume(void)
{
	int i;

	for (i = 0; i < RV1106_CRU_GATE_CON_NUM; i++)
		writel_relaxed(WITH_16BITS_WMSK(ddr_data.cru_gate_con[i]),
			       cru_base + RV1106_CRU_GATE_CON(i));

	for (i = 0; i < RV1106_PMUCRU_GATE_CON_NUM; i++)
		writel_relaxed(WITH_16BITS_WMSK(ddr_data.pmucru_gate_con[i]),
			       pmucru_base + RV1106_PMUCRU_GATE_CON(i));

	for (i = 0; i < RV1106_PERICRU_GATE_CON_NUM; i++)
		writel_relaxed(WITH_16BITS_WMSK(ddr_data.pericru_gate_con[i]),
			       pericru_base + RV1106_PERICRU_GATE_CON(i));

	for (i = 0; i < RV1106_NPUCRU_GATE_CON_NUM; i++)
		writel_relaxed(WITH_16BITS_WMSK(ddr_data.npucru_gate_con[i]),
			       npucru_base + RV1106_NPUCRU_GATE_CON(i));

	for (i = 0; i < RV1106_VENCCRU_GATE_CON_NUM; i++)
		writel_relaxed(WITH_16BITS_WMSK(ddr_data.venccru_gate_con[i]),
			       venccru_base + RV1106_VENCCRU_GATE_CON(i));

	for (i = 0; i < RV1106_VICRU_GATE_CON_NUM; i++)
		writel_relaxed(WITH_16BITS_WMSK(ddr_data.vicru_gate_con[i]),
			       vicru_base + RV1106_VICRU_GATE_CON(i));

	for (i = 0; i < RV1106_VOCRU_GATE_CON_NUM; i++)
		writel_relaxed(WITH_16BITS_WMSK(ddr_data.vocru_gate_con[i]),
			       vocru_base + RV1106_VOCRU_GATE_CON(i));
}

static void pvtm_32k_config(int flag)
{
	int value;
	int pvtm_freq_khz, pvtm_div;
	int sleep_clk_freq_khz;

	ddr_data.pmucru_sel_con7 =
		readl_relaxed(pmucru_base + RV1106_PMUCRU_CLKSEL_CON(7));

	if (flag) {
		writel_relaxed(BITS_WITH_WMASK(0x1, 0x1, 6), vigrf_base + 0x0);
		writel_relaxed(BITS_WITH_WMASK(0x4, 0xf, 0), ioc_base[0] + 0);
		writel_relaxed(BITS_WITH_WMASK(0x1, 0x1, 15),
			       pmugrf_base + RV1106_PMUGRF_SOC_CON(1));
		writel_relaxed(BITS_WITH_WMASK(0x1, 0x3, 0),
			       pmucru_base + RV1106_PMUCRU_CLKSEL_CON(7));
	} else {
		writel_relaxed(BITS_WITH_WMASK(0, 0x3, 0),
			       pmupvtm_base + RV1106_PVTM_CON(2));
		writel_relaxed(RV1106_PVTM_CALC_CNT,
			       pmupvtm_base + RV1106_PVTM_CON(1));
		writel_relaxed(BITS_WITH_WMASK(0, 0x3, PVTM_START),
			       pmupvtm_base + RV1106_PVTM_CON(0));
		dsb();

		writel_relaxed(BITS_WITH_WMASK(0, 0x7, PVTM_OSC_SEL),
			       pmupvtm_base + RV1106_PVTM_CON(0));
		writel_relaxed(BITS_WITH_WMASK(1, 0x1, PVTM_OSC_EN),
			       pmupvtm_base + RV1106_PVTM_CON(0));
		writel_relaxed(BITS_WITH_WMASK(1, 0x1, PVTM_RND_SEED_EN),
			       pmupvtm_base + RV1106_PVTM_CON(0));
		dsb();

		writel_relaxed(BITS_WITH_WMASK(1, 0x1, PVTM_START),
			       pmupvtm_base + RV1106_PVTM_CON(0));
		dsb();

		while (readl_relaxed(pmupvtm_base + RV1106_PVTM_STATUS(1)) < 30)
			;

		dsb();

		while (!readl_relaxed(pmupvtm_base + RV1106_PVTM_STATUS(0)) & 0x1)
			;

		value = (readl_relaxed(pmupvtm_base + RV1106_PVTM_STATUS(1)));
		pvtm_freq_khz = (value * 24000 + RV1106_PVTM_CALC_CNT / 2) / RV1106_PVTM_CALC_CNT;
		pvtm_div = (pvtm_freq_khz + 16) / 32 - 1;
		if (pvtm_div > 0xfff)
			pvtm_div = 0xfff;

		writel_relaxed(WITH_16BITS_WMSK(pvtm_div),
			       pmugrf_base + RV1106_PMUGRF_SOC_CON(3));

		/* select 32k source */
		writel_relaxed(BITS_WITH_WMASK(0x2, 0x3, 0),
			       pmucru_base + RV1106_PMUCRU_CLKSEL_CON(7));

		sleep_clk_freq_khz = pvtm_freq_khz / (pvtm_div + 1);

		rkpm_printstr("pvtm real_freq (khz):");
		rkpm_printhex(sleep_clk_freq_khz);
		rkpm_printch('\n');
	}
}

static void pvtm_32k_config_restore(void)
{
	writel_relaxed(WITH_16BITS_WMSK(ddr_data.pmucru_sel_con7),
		       pmucru_base + RV1106_PMUCRU_CLKSEL_CON(7));
}

static void ddr_sleep_config(void)
{
	u32 val;

	ddr_data.ddrc_pwrctrl = readl_relaxed(ddrc_base + 0x30);
	ddr_data.ddrgrf_con1 = readl_relaxed(ddrgrf_base + RV1106_DDRGRF_CON(1));
	ddr_data.ddrgrf_con2 = readl_relaxed(ddrgrf_base + RV1106_DDRGRF_CON(2));
	ddr_data.ddrgrf_con3 = readl_relaxed(ddrgrf_base + RV1106_DDRGRF_CON(3));
	ddr_data.ddrc_dfilpcfg0 = readl_relaxed(ddrc_base + 0x198);
	ddr_data.pmugrf_soc_con0 = readl_relaxed(pmugrf_base + RV1106_PMUGRF_SOC_CON(0));

	val = readl_relaxed(ddrc_base + 0x30);
	writel_relaxed(val & ~(BIT(0) | BIT(1)), ddrc_base + 0x30);

	/* disable ddr auto gt */
	writel_relaxed(BITS_WITH_WMASK(0x12, 0x1f, 0), ddrgrf_base + RV1106_DDRGRF_CON(1));

	writel_relaxed(BITS_WITH_WMASK(0x3ff, 0x3ff, 0), ddrgrf_base + RV1106_DDRGRF_CON(2));

	/* ddr low power request by pmu */
	writel_relaxed(BITS_WITH_WMASK(0x3, 0x3, 8), ddrgrf_base + RV1106_DDRGRF_CON(3));

	while ((readl_relaxed(ddrc_base + 0x4) & 0x7) != 0x1)
		continue;

	val = readl_relaxed(ddrc_base + 0x198) & ~(0xf << 12 | 0xf << 4);
	val |= (0xa << 12 | 0xa << 4);
	writel_relaxed(val, ddrc_base + 0x198);

	val = readl_relaxed(ddrc_base + 0x198) | BIT(8) | BIT(0);
	writel_relaxed(val, ddrc_base + 0x198);

	/* ddr io ret by pmu */
	writel_relaxed(BITS_WITH_WMASK(0x0, 0x7, 9), pmugrf_base + RV1106_PMUGRF_SOC_CON(0));
}

static void ddr_sleep_config_restore(void)
{
	writel_relaxed(WITH_16BITS_WMSK(ddr_data.ddrgrf_con3),
		       ddrgrf_base + RV1106_DDRGRF_CON(3));
}

static void pmu_sleep_config(void)
{
	u32 clk_freq_khz = 32;
	u32 pmu_wkup_con, pmu_pwr_con, pmu_scu_con, pmu_ddr_con;
	u32 pmu_bus_idle_con, pmu_cru_con[2], pmu_pll_con;

	ddr_data.pmugrf_soc_con1 = readl_relaxed(pmugrf_base + RV1106_PMUGRF_SOC_CON(1));
	ddr_data.pmugrf_soc_con4 = readl_relaxed(pmugrf_base + RV1106_PMUGRF_SOC_CON(4));
	ddr_data.pmugrf_soc_con5 = readl_relaxed(pmugrf_base + RV1106_PMUGRF_SOC_CON(5));
	ddr_data.ioc1_1a_iomux_l = readl_relaxed(ioc_base[1] + 0);

	pmu_wkup_con =
		/* BIT(RV1106_PMU_WAKEUP_CPU_INT_EN) | */
		BIT(RV1106_PMU_WAKEUP_GPIO_INT_EN) |
		0;
	if (IS_ENABLED(CONFIG_RV1106_HPMCU_FAST_WAKEUP))
		pmu_wkup_con |= BIT(RV1106_PMU_WAKEUP_TIMEROUT_EN);

	pmu_pwr_con =
		BIT(RV1106_PMU_PWRMODE_EN) |
		/* BIT(RV1106_PMU_BUS_BYPASS) | */
		/* BIT(RV1106_PMU_DDR_BYPASS) | */
		/* BIT(RV1106_PMU_CRU_BYPASS) | */
		0;

	pmu_scu_con =
		BIT(RV1106_PMU_SCU_INT_MASK_ENA) |
		BIT(RV1106_PMU_CPU_INT_MASK_ENA) |
		0;

	pmu_bus_idle_con =
		BIT(RV1106_PMU_IDLE_REQ_MSCH) |
		BIT(RV1106_PMU_IDLE_REQ_DDR) |
		BIT(RV1106_PMU_IDLE_REQ_NPU) |
		BIT(RV1106_PMU_IDLE_REQ_NPU_ACLK) |
		BIT(RV1106_PMU_IDLE_REQ_VI) |
		BIT(RV1106_PMU_IDLE_REQ_VO) |
		BIT(RV1106_PMU_IDLE_REQ_PERI) |
		BIT(RV1106_PMU_IDLE_REQ_CRU) |
		BIT(RV1106_PMU_IDLE_REQ_CPU) |
		BIT(RV1106_PMU_IDLE_REQ_VENC_COM) |
		BIT(RV1106_PMU_IDLE_REQ_VEPU) |
		0;

	pmu_cru_con[0] =
		BIT(RV1106_PMU_ALIVE_32K_ENA) |
		BIT(RV1106_PMU_OSC_DIS_ENA) |
		BIT(RV1106_PMU_WAKEUP_RST_ENA) |
		BIT(RV1106_PMU_INPUT_CLAMP_ENA) |
		/* BIT(RV1106_PMU_ALIVE_OSC_ENA) | */
		BIT(RV1106_PMU_POWER_OFF_ENA) |
		0;

	pmu_cru_con[1] =
		/* BIT(RV1106_PMU_VI_CLK_SRC_GATE_ENA) | */
		/* BIT(RV1106_PMU_VO_CLK_SRC_GATE_ENA) | */
		/* BIT(RV1106_PMU_VENC_CLK_SRC_GATE_ENA) | */
		/* BIT(RV1106_PMU_NPU_CLK_SRC_GATE_ENA) | */
		/* BIT(RV1106_PMU_DDR_CLK_SRC_CATE_ENA) | */
		/* BIT(RV1106_PMU_PERI_CLK_SRC_GATE_ENA) | */
		/* BIT(RV1106_PMU_CORE_CLK_SRC_GATE_ENA) | */
		/* BIT(RV1106_PMU_CRU_CLK_SRC_GATE_ENA) | */
		0;

	pmu_ddr_con =
		BIT(RV1106_PMU_DDR_SREF_C_ENA) |
#if !RV1106_WAKEUP_TO_SYSTEM_RESET
		BIT(RV1106_PMU_DDRIO_RET_ENA) |
#endif
		BIT(RV1106_PMU_DDRCTL_C_AUTO_GATING_ENA) |
		BIT(RV1106_PMU_MSCH_AUTO_GATING_ENA) |
		BIT(RV1106_PMU_DDR_SREF_A_ENA) |
		BIT(RV1106_PMU_DDRCTL_A_AUTO_GATING_ENA) |
		0;

	pmu_pll_con =
		BIT(RV1106_PMU_APLL_PD_ENA) |
		BIT(RV1106_PMU_DPLL_PD_ENA) |
		BIT(RV1106_PMU_CPLL_PD_ENA) |
		BIT(RV1106_PMU_GPLL_PD_ENA) |
		0;

	/* pmic_sleep */
	/* gpio0_a3 activelow, gpio0_a4 active high */
	writel_relaxed(BITS_WITH_WMASK(0x4, 0x7, 0), pmugrf_base + RV1106_PMUGRF_SOC_CON(1));
	/* select sleep func */
	writel_relaxed(BITS_WITH_WMASK(0x1, 0x1, 0), pmugrf_base + RV1106_PMUGRF_SOC_CON(1));
	/* gpio0_a3 iomux */
	writel_relaxed(BITS_WITH_WMASK(0x1, 0xf, 12), ioc_base[0] + 0);

	/* pmu_debug */
	writel_relaxed(0xffffff01, pmu_base + RV1106_PMU_INFO_TX_CON);
	writel_relaxed(BITS_WITH_WMASK(0x1, 0xf, 4), ioc_base[1] + 0);

	/* pmu count */
	writel_relaxed(clk_freq_khz * 10, pmu_base + RV1106_PMU_OSC_STABLE_CNT);
	writel_relaxed(clk_freq_khz * 5, pmu_base + RV1106_PMU_PMIC_STABLE_CNT);

	/* Pmu's clk has switched to 24M back When pmu FSM counts
	 * the follow counters, so we should use 24M to calculate
	 * these counters.
	 */
	writel_relaxed(12000, pmu_base + RV1106_PMU_WAKEUP_RSTCLR_CNT);
	writel_relaxed(12000, pmu_base + RV1106_PMU_PLL_LOCK_CNT);
	writel_relaxed(24000 * 2, pmu_base + RV1106_PMU_PWM_SWITCH_CNT);

	/* pmu reset hold */
	writel_relaxed(0xffffffff, pmugrf_base + RV1106_PMUGRF_SOC_CON(4));
	writel_relaxed(0xffffff47, pmugrf_base + RV1106_PMUGRF_SOC_CON(5));

	writel_relaxed(0x00010001, pmu_base + RV1106_PMU_INT_MASK_CON);
	writel_relaxed(WITH_16BITS_WMSK(pmu_scu_con), pmu_base + RV1106_PMU_SCU_PWR_CON);

	writel_relaxed(WITH_16BITS_WMSK(pmu_cru_con[0]), pmu_base + RV1106_PMU_CRU_PWR_CON0);
	writel_relaxed(WITH_16BITS_WMSK(pmu_cru_con[1]), pmu_base + RV1106_PMU_CRU_PWR_CON1);
	writel_relaxed(WITH_16BITS_WMSK(pmu_bus_idle_con), pmu_base + RV1106_PMU_BIU_IDLE_CON);

	writel_relaxed(WITH_16BITS_WMSK(pmu_ddr_con), pmu_base + RV1106_PMU_DDR_PWR_CON);
	writel_relaxed(WITH_16BITS_WMSK(pmu_pll_con), pmu_base + RV1106_PMU_PLLPD_CON);
	writel_relaxed(pmu_wkup_con, pmu_base + RV1106_PMU_WAKEUP_INT_CON);
	writel_relaxed(WITH_16BITS_WMSK(pmu_pwr_con), pmu_base + RV1106_PMU_PWR_CON);

#if RV1106_WAKEUP_TO_SYSTEM_RESET
	writel_relaxed(0, pmugrf_base + RV1106_PMUGRF_OS_REG(9));
	/* Use PMUGRF_OS_REG10 to save wakeup source */
	writel_relaxed(0, pmugrf_base + RV1106_PMUGRF_OS_REG(10));
#else
	writel_relaxed(PMU_SUSPEND_MAGIC, pmugrf_base + RV1106_PMUGRF_OS_REG(9));
#endif
}

static void pmu_sleep_restore(void)
{
	ddr_data.pmu_wkup_int_st = readl_relaxed(pmu_base + RV1106_PMU_WAKEUP_INT_ST);
	ddr_data.gpio0_int_st = readl_relaxed(gpio_base[0] + RV1106_GPIO_INT_STATUS);

	writel_relaxed(0xffff0000, pmu_base + RV1106_PMU_INFO_TX_CON);
	writel_relaxed(0x00010000, pmu_base + RV1106_PMU_INT_MASK_CON);
	writel_relaxed(0x00000000, pmu_base + RV1106_PMU_WAKEUP_INT_CON);
	writel_relaxed(0xffff0000, pmu_base + RV1106_PMU_PWR_CON);
	writel_relaxed(0xffff0000, pmu_base + RV1106_PMU_BIU_IDLE_CON);
	writel_relaxed(0xffff0000, pmu_base + RV1106_PMU_DDR_PWR_CON);
	writel_relaxed(0xffff0000, pmu_base + RV1106_PMU_SCU_PWR_CON);
	writel_relaxed(0xffff0000, pmu_base + RV1106_PMU_PLLPD_CON);
	writel_relaxed(0xffff0000, pmu_base + RV1106_PMU_CRU_PWR_CON0);
	writel_relaxed(0xffff0000, pmu_base + RV1106_PMU_CRU_PWR_CON1);

	writel_relaxed(WITH_16BITS_WMSK(ddr_data.ioc1_1a_iomux_l),
		       ioc_base[1] + 0);
	writel_relaxed(WITH_16BITS_WMSK(ddr_data.pmugrf_soc_con1),
		       pmugrf_base + RV1106_PMUGRF_SOC_CON(1));
	writel_relaxed(WITH_16BITS_WMSK(ddr_data.pmugrf_soc_con4),
		       pmugrf_base + RV1106_PMUGRF_SOC_CON(4));
	writel_relaxed(WITH_16BITS_WMSK(ddr_data.pmugrf_soc_con5),
		       pmugrf_base + RV1106_PMUGRF_SOC_CON(5));
}

static void soc_sleep_config(void)
{
	ddr_data.ioc0_1a_iomux_l = readl_relaxed(ioc_base[0] + 0);

	rkpm_printch('a');

	pvtm_32k_config(0);
	rkpm_printch('b');

	ddr_sleep_config();
	rkpm_printch('c');

	pmu_sleep_config();
	rkpm_printch('d');
}

static void soc_sleep_restore(void)
{
	rkpm_printch('d');

	pmu_sleep_restore();
	rkpm_printch('c');

	ddr_sleep_config_restore();
	rkpm_printch('b');

	pvtm_32k_config_restore();
	rkpm_printch('a');

	writel_relaxed(WITH_16BITS_WMSK(ddr_data.ioc0_1a_iomux_l),
		       ioc_base[0] + 0);
}

static void plls_suspend(void)
{
}

static void plls_resume(void)
{
}

static void gpio0_set_iomux(u32 pin_id, u32 func)
{
	u32 sft = (pin_id % 4) << 2;

	if (pin_id < 4)
		writel_relaxed(BITS_WITH_WMASK(func, 0xf, sft), ioc_base[0] + 0);
	else
		writel_relaxed(BITS_WITH_WMASK(func, 0xf, sft), ioc_base[0] + 4);
}

static void gpio0_set_pull(u32 pin_id, int pull)
{
	u32 sft = (pin_id % 8) << 1;

	writel_relaxed(BITS_WITH_WMASK(pull, 0x3, sft), ioc_base[0] + 0x38);
}

static void gpio0_set_direct(u32 pin_id, int out)
{
	u32 sft = (pin_id % 16);

	if (pin_id < 16)
		writel_relaxed(BITS_WITH_WMASK(out, 0x1, sft),
			       gpio_base[0] + RV1106_GPIO_SWPORT_DDR_L);
	else
		writel_relaxed(BITS_WITH_WMASK(out, 0x1, sft),
			       gpio_base[0] + RV1106_GPIO_SWPORT_DDR_H);
}

static void gpio_config(void)
{
	ddr_data.gpio0a_iomux_l = readl_relaxed(ioc_base[0] + 0);
	ddr_data.gpio0a_iomux_h = readl_relaxed(ioc_base[0] + 0x4);
	ddr_data.gpio0a0_pull = readl_relaxed(ioc_base[0] + 0x38);
	ddr_data.gpio0_ddr_l = readl_relaxed(gpio_base[0] + RV1106_GPIO_SWPORT_DDR_L);
	ddr_data.gpio0_ddr_h = readl_relaxed(gpio_base[0] + RV1106_GPIO_SWPORT_DDR_H);

	/* gpio0_a0, input, pulldown */
	gpio0_set_iomux(0, 0);
	gpio0_set_pull(0, RV1106_GPIO_PULL_DOWN);
	gpio0_set_direct(0, 0);

#ifdef RV1106_GPIO0_A1_LOWPOWER
	/* gpio0_a1, input, pulldown */
	gpio0_set_iomux(1, 0);
	gpio0_set_pull(1, RV1106_GPIO_PULL_DOWN);
	gpio0_set_direct(1, 0);
#endif
	/* gpio0_a2, input, pulldown */
	gpio0_set_iomux(2, 0);
	gpio0_set_pull(2, RV1106_GPIO_PULL_DOWN);
	gpio0_set_direct(2, 0);

	/* gpio0_a3, pullnone */
	gpio0_set_pull(3, RV1106_GPIO_PULL_NONE);

	/* gpio0_a4, input, pulldown */
	gpio0_set_iomux(4, 0);
	gpio0_set_pull(4, RV1106_GPIO_PULL_DOWN);
	gpio0_set_direct(4, 0);

	/* gpio0_a5, input, pullnone */
	gpio0_set_iomux(5, 0);
	gpio0_set_pull(5, RV1106_GPIO_PULL_NONE);
	gpio0_set_direct(5, 0);

	/* gpio0_a6, input, pullnone */
	gpio0_set_iomux(6, 0);
	gpio0_set_pull(6, RV1106_GPIO_PULL_NONE);
	gpio0_set_direct(6, 0);
}

static void gpio_restore(void)
{
	writel_relaxed(WITH_16BITS_WMSK(ddr_data.gpio0a_iomux_l), ioc_base[0] + 0);
	writel_relaxed(WITH_16BITS_WMSK(ddr_data.gpio0a_iomux_h), ioc_base[0] + 0x4);

	writel_relaxed(WITH_16BITS_WMSK(ddr_data.gpio0a0_pull), ioc_base[0] + 0x38);

	writel_relaxed(WITH_16BITS_WMSK(ddr_data.gpio0_ddr_l),
		       gpio_base[0] + RV1106_GPIO_SWPORT_DDR_L);
	writel_relaxed(WITH_16BITS_WMSK(ddr_data.gpio0_ddr_h),
		       gpio_base[0] + RV1106_GPIO_SWPORT_DDR_H);
}

static struct uart_debug_ctx debug_port_save;
static u32 cru_mode;

static void vd_log_regs_save(void)
{
	cru_mode = readl_relaxed(cru_base + 0x280);

	rkpm_printch('a');

	gic400_save();
	rkpm_printch('b');

	rkpm_reg_rgn_save(vd_core_reg_rgns, ARRAY_SIZE(vd_core_reg_rgns));
	rkpm_printch('c');
	rkpm_reg_rgn_save(vd_log_reg_rgns, ARRAY_SIZE(vd_log_reg_rgns));
	rkpm_printch('d');

	rkpm_uart_debug_save(uartdbg_base, &debug_port_save);
	rkpm_printch('e');
}

static void vd_log_regs_restore(void)
{
	rkpm_uart_debug_restore(uartdbg_base, &debug_port_save);

	/* slow mode */
	writel_relaxed(0x003f0000, cru_base + 0x280);

	rkpm_reg_rgn_restore(vd_core_reg_rgns, ARRAY_SIZE(vd_core_reg_rgns));
	rkpm_reg_rgn_restore(vd_log_reg_rgns, ARRAY_SIZE(vd_log_reg_rgns));

	/* wait lock */
	pm_pll_wait_lock(RV1106_APLL_ID);
	pm_pll_wait_lock(RV1106_CPLL_ID);
	pm_pll_wait_lock(RV1106_GPLL_ID);

	/* restore mode */
	writel_relaxed(WITH_16BITS_WMSK(cru_mode), cru_base + 0x280);

	gic400_restore();
}

static void rkpm_reg_rgns_init(void)
{
	rkpm_alloc_region_mem(vd_core_reg_rgns, ARRAY_SIZE(vd_core_reg_rgns));
	rkpm_alloc_region_mem(vd_log_reg_rgns, ARRAY_SIZE(vd_log_reg_rgns));
}

static void rkpm_regs_rgn_dump(void)
{
	return;

	rkpm_dump_reg_rgns(vd_core_reg_rgns, ARRAY_SIZE(vd_core_reg_rgns));
	rkpm_dump_reg_rgns(vd_log_reg_rgns, ARRAY_SIZE(vd_log_reg_rgns));
}

static int rockchip_lpmode_enter(unsigned long arg)
{
	flush_cache_all();

	cpu_do_idle();

#if RV1106_WAKEUP_TO_SYSTEM_RESET
	/* If reaches here, it means wakeup source cames before cpu enter wfi.
	 * So we should do system reset if RV1106_WAKEUP_TO_SYSTEM_RESET.
	 */
	writel_relaxed(0x000c000c, cru_base + RV1106_CRU_GLB_RST_CON);
	writel_relaxed(0xffff0000, pmugrf_base + RV1106_PMUGRF_SOC_CON(4));
	writel_relaxed(0xffff0000, pmugrf_base + RV1106_PMUGRF_SOC_CON(5));
	dsb(sy);
	writel_relaxed(0xfdb9, cru_base + RV1106_CRU_GLB_SRST_FST);
#endif

	rkpm_printstr("Failed to suspend\n");

	return 1;
}

static int rv1106_suspend_enter(suspend_state_t state)
{
	rkpm_printstr("rv1106 enter sleep\n");

	local_fiq_disable();

	rv1106_dbg_irq_prepare();

	rkpm_printch('-');

RE_ENTER_SLEEP:
	clock_suspend();
	rkpm_printch('0');

	soc_sleep_config();
	rkpm_printch('1');

	plls_suspend();
	rkpm_printch('2');

	gpio_config();
	rkpm_printch('3');

	vd_log_regs_save();
	rkpm_regs_rgn_dump();
	rkpm_printch('4');

	rkpm_printstr("-WFI-");
	cpu_suspend(0, rockchip_lpmode_enter);

	rkpm_printch('4');

	vd_log_regs_restore();
	rkpm_printch('3');
	rkpm_regs_rgn_dump();

	gpio_restore();
	rkpm_printch('2');

	plls_resume();
	rkpm_printch('1');

	soc_sleep_restore();
	rkpm_printch('0');

	clock_resume();
	rkpm_printch('-');

	/* Check whether it's time_out wakeup */
	if (IS_ENABLED(CONFIG_RV1106_HPMCU_FAST_WAKEUP)) {
		if (hpmcu_fast_wkup()) {
			rkpm_gicv2_dist_restore(gicd_base, &gicd_ctx_save);
			goto RE_ENTER_SLEEP;
		} else {
			rkpm_gicv2_dist_restore(gicd_base, &gicd_ctx_save);
			rkpm_gicv2_cpu_restore(gicd_base, gicc_base, &gicc_ctx_save);
		}
	}

	fiq_glue_resume();

	rv1106_dbg_irq_finish();

	local_fiq_enable();
	rkpm_printstr("rv1106 exit sleep\n");

	return 0;
}

static int __init rv1106_suspend_init(struct device_node *np)
{
	void __iomem *dev_reg_base;

	dev_reg_base = ioremap(RV1106_DEV_REG_BASE, RV1106_DEV_REG_SIZE);
	if (dev_reg_base)
		pr_info("%s map dev_reg 0x%x -> 0x%x\n",
			__func__, RV1106_DEV_REG_BASE, (u32)dev_reg_base);
	else
		pr_err("%s: can't map dev_reg(0x%x)\n", __func__, RV1106_DEV_REG_BASE);

	gicd_base = dev_reg_base + RV1106_GIC_OFFSET + 0x1000;
	gicc_base = dev_reg_base + RV1106_GIC_OFFSET + 0x2000;

	firewall_ddr_base = dev_reg_base + RV1106_FW_DDR_OFFSET;
	firewall_syssram_base = dev_reg_base + RV1106_FW_SRAM_OFFSET;

	nstimer_base = dev_reg_base + RV1106_NSTIMER_OFFSET;
	stimer_base = dev_reg_base + RV1106_STIMER_OFFSET;

	pmu_base = dev_reg_base + RV1106_PMU_OFFSET;
	uartdbg_base = dev_reg_base + RV1106_UART2_OFFSET;
	pmupvtm_base = dev_reg_base + RV1106_PMUPVTM_OFFSET;
	pmusgrf_base = dev_reg_base + RV1106_PMUSGRF_OFFSET;
	ddrc_base = dev_reg_base + RV1106_DDRC_OFFSET;

	perigrf_base = dev_reg_base + RV1106_PERIGRF_OFFSET;
	vencgrf_base = dev_reg_base + RV1106_VENCGRF_OFFSET;
	npugrf_base = dev_reg_base + RV1106_NPUGRF_OFFSET;
	pmugrf_base = dev_reg_base + RV1106_PMUGRF_OFFSET;
	ddrgrf_base = dev_reg_base + RV1106_DDRGRF_OFFSET;
	coregrf_base = dev_reg_base + RV1106_COREGRF_OFFSET;
	vigrf_base = dev_reg_base + RV1106_VIGRF_OFFSET;
	vogrf_base = dev_reg_base + RV1106_VOGRF_OFFSET;

	perisgrf_base = dev_reg_base + RV1106_PERISGRF_OFFSET;
	visgrf_base = dev_reg_base + RV1106_VIGRF_OFFSET;
	npusgrf_base = dev_reg_base + RV1106_NPUSGRF_OFFSET;
	coresgrf_base = dev_reg_base + RV1106_CORESGRF_OFFSET;
	vencsgrf_base = dev_reg_base + RV1106_VENCSGRF_OFFSET;
	vosgrf_base = dev_reg_base + RV1106_VOSGRF_OFFSET;
	pmusgrf_base = dev_reg_base + RV1106_PMUSGRF_OFFSET;

	pmucru_base = dev_reg_base + RV1106_PMUCRU_OFFSET;
	cru_base = dev_reg_base + RV1106_CRU_OFFSET;
	pvtpllcru_base = dev_reg_base + RV1106_PVTPLLCRU_OFFSET;
	pericru_base = dev_reg_base + RV1106_PERICRU_OFFSET;
	vicru_base = dev_reg_base + RV1106_VICRU_OFFSET;
	npucru_base = dev_reg_base + RV1106_NPUCRU_OFFSET;
	corecru_base = dev_reg_base + RV1106_CORECRU_OFFSET;
	venccru_base = dev_reg_base + RV1106_VENCCRU_OFFSET;
	vocru_base = dev_reg_base + RV1106_VOCRU_OFFSET;
	mbox_base = dev_reg_base + RV1106_MBOX_OFFSET;

	ioc_base[0] = dev_reg_base + RV1106_GPIO0IOC_OFFSET;
	ioc_base[1] = dev_reg_base + RV1106_GPIO1IOC_OFFSET;
	ioc_base[2] = dev_reg_base + RV1106_GPIO2IOC_OFFSET;
	ioc_base[3] = dev_reg_base + RV1106_GPIO3IOC_OFFSET;
	ioc_base[4] = dev_reg_base + RV1106_GPIO4IOC_OFFSET;

	gpio_base[0] = dev_reg_base + RV1106_GPIO0_OFFSET;
	gpio_base[1] = dev_reg_base + RV1106_GPIO1_OFFSET;
	gpio_base[2] = dev_reg_base + RV1106_GPIO2_OFFSET;
	gpio_base[3] = dev_reg_base + RV1106_GPIO3_OFFSET;
	gpio_base[4] = dev_reg_base + RV1106_GPIO4_OFFSET;

	rv1106_bootram_base = dev_reg_base + RV1106_PMUSRAM_OFFSET;

	rv1106_config_bootdata();

	/* copy resume code and data to bootsram */
	memcpy(rv1106_bootram_base, rockchip_slp_cpu_resume,
	       rv1106_bootram_sz + 0x50);

	/* remap */
#if RV1106_WAKEUP_TO_SYSTEM_RESET
	writel_relaxed(BITS_WITH_WMASK(1, 0x1, 10), pmusgrf_base + RV1106_PMUSGRF_SOC_CON(1));
#else
	writel_relaxed(BITS_WITH_WMASK(0, 0x1, 10), pmusgrf_base + RV1106_PMUSGRF_SOC_CON(1));
#endif
	/* biu auto con */
	writel_relaxed(0x07ff07ff, pmu_base + RV1106_PMU_BIU_AUTO_CON);

	rkpm_region_mem_init(RV1106_PM_REG_REGION_MEM_SIZE);
	rkpm_reg_rgns_init();

	return 0;
}

static const struct platform_suspend_ops rv1106_suspend_ops = {
	.enter   = rv1106_suspend_enter,
	.valid   = suspend_valid_only_mem,
};

static const struct rockchip_pm_data rv1106_pm_data __initconst = {
	.ops = &rv1106_suspend_ops,
	.init = rv1106_suspend_init,
};

/****************************************************************************/
static const struct of_device_id rockchip_pmu_of_device_ids[] __initconst = {
	{
		.compatible = "rockchip,rv1106-pmu",
		.data = &rv1106_pm_data,
	},
	{ /* sentinel */ },
};

void __init rockchip_suspend_init(void)
{
	const struct rockchip_pm_data *pm_data;
	const struct of_device_id *match;
	struct device_node *np;
	int ret;

	np = of_find_matching_node_and_match(NULL, rockchip_pmu_of_device_ids,
					     &match);
	if (!match) {
		pr_err("Failed to find PMU node\n");
		return;
	}
	pm_data = (struct rockchip_pm_data *)match->data;

	if (pm_data->init) {
		ret = pm_data->init(np);

		if (ret) {
			pr_err("%s: matches init error %d\n", __func__, ret);
			return;
		}
	}

	suspend_set_ops(pm_data->ops);
}
