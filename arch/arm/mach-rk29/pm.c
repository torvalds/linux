#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/wakelock.h>
#include <linux/console.h>

#include <mach/rk29_iomap.h>
#include <mach/cru.h>
#include <mach/pmu.h>
#include <mach/board.h>
#include <mach/system.h>
#include <mach/sram.h>

#define cru_readl(offset)	readl(RK29_CRU_BASE + offset)
#define cru_writel(v, offset)	do { writel(v, RK29_CRU_BASE + offset); readl(RK29_CRU_BASE + offset); } while (0)
#define pmu_readl(offset)	readl(RK29_PMU_BASE + offset)
#define pmu_writel(v, offset)	do { writel(v, RK29_PMU_BASE + offset); readl(RK29_PMU_BASE + offset); } while (0)
static unsigned long save_sp;

static inline void delay_500ns(void)
{
	int delay = 12;
	while (delay--)
		barrier();
}

static inline void delay_300us(void)
{
	int i;
	for (i = 0; i < 600; i++)
		delay_500ns();
}

static u32 apll;
static u32 lpj;
static struct regulator *vcore;
static int vcore_uV;

extern void ddr_suspend(void);
extern void ddr_resume(void);

static void __sramfunc rk29_pm_enter_ddr(void)
{
	u32 clksel0, dpll, mode, clkgate0;

	asm("dsb");
	ddr_suspend();

	/* suspend ddr pll */
	mode = cru_readl(CRU_MODE_CON);
	dpll = cru_readl(CRU_DPLL_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_DDR_MODE_MASK) | CRU_DDR_MODE_SLOW, CRU_MODE_CON);
	cru_writel(dpll | PLL_BYPASS, CRU_DPLL_CON);
	cru_writel(dpll | PLL_PD | PLL_BYPASS, CRU_DPLL_CON);
	delay_500ns();

	/* disable ddr clock */
	clkgate0 = cru_readl(CRU_CLKGATE0_CON);
	cru_writel(clkgate0 | (1 << CLK_GATE_DDR_PHY) | (1 << CLK_GATE_DDR_REG) | (1 << CLK_GATE_DDR_CPU), CRU_CLKGATE0_CON);

	/* set arm clk 24MHz/32 = 750KHz */
	clksel0 = cru_readl(CRU_CLKSEL0_CON);
	cru_writel(clksel0 | 0x1F, CRU_CLKSEL0_CON);

	asm("wfi");

	/* resume arm clk */
	cru_writel(clksel0, CRU_CLKSEL0_CON);

	/* enable ddr clock */
	cru_writel(clkgate0, CRU_CLKGATE0_CON);

	/* resume ddr pll */
	cru_writel(dpll, CRU_DPLL_CON);
	delay_300us();
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_DDR_MODE_MASK) | (mode & CRU_DDR_MODE_MASK), CRU_MODE_CON);

	ddr_resume();
}

static int rk29_pm_enter(suspend_state_t state)
{
	u32 cpll, gpll, mode;
	u32 clkgate[4];

	/* disable clock */
	clkgate[0] = cru_readl(CRU_CLKGATE0_CON);
	clkgate[1] = cru_readl(CRU_CLKGATE1_CON);
	clkgate[2] = cru_readl(CRU_CLKGATE2_CON);
	clkgate[3] = cru_clkgate3_con_mirror;
	cru_writel(~((1 << CLK_GATE_CORE)
		   | (1 << CLK_GATE_ACLK_CPU)
		   | (1 << CLK_GATE_ACLK_CPU2)
		   | (1 << CLK_GATE_PCLK_CPU)
		   | (1 << CLK_GATE_GIC)
		   | (1 << CLK_GATE_INTMEM)
		   | (1 << CLK_GATE_DDR_PHY)
		   | (1 << CLK_GATE_DDR_REG)
		   | (1 << CLK_GATE_DDR_CPU)
		   | (1 << CLK_GATE_GPIO0)
		   | (1 << CLK_GATE_RTC)
		   | (1 << CLK_GATE_GRF)
		   ) | clkgate[0], CRU_CLKGATE0_CON);
	cru_writel(~0, CRU_CLKGATE1_CON);
	cru_writel(~((1 << CLK_GATE_GPIO1 % 32)
		   | (1 << CLK_GATE_GPIO2 % 32)
		   | (1 << CLK_GATE_GPIO3 % 32)
		   | (1 << CLK_GATE_GPIO4 % 32)
		   | (1 << CLK_GATE_GPIO5 % 32)
		   | (1 << CLK_GATE_GPIO6 % 32)
		   | (1 << CLK_GATE_PWM % 32)
		   ) | clkgate[2], CRU_CLKGATE2_CON);
	cru_writel(~0, CRU_CLKGATE3_CON);

	mode = cru_readl(CRU_MODE_CON);

	/* suspend codec pll */
	cpll = cru_readl(CRU_CPLL_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | CRU_CODEC_MODE_SLOW, CRU_MODE_CON);
	cru_writel(cpll | PLL_BYPASS, CRU_CPLL_CON);
	cru_writel(cpll | PLL_PD | PLL_BYPASS, CRU_CPLL_CON);
	delay_500ns();

	/* suspend general pll */
	gpll = cru_readl(CRU_GPLL_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | CRU_GENERAL_MODE_SLOW, CRU_MODE_CON);
	cru_writel(gpll | PLL_BYPASS, CRU_GPLL_CON);
	cru_writel(gpll | PLL_PD | PLL_BYPASS, CRU_GPLL_CON);
	delay_500ns();

	DDR_SAVE_SP(save_sp);
	rk29_pm_enter_ddr();
	DDR_RESTORE_SP(save_sp);

	/* resume general pll */
	cru_writel(gpll, CRU_GPLL_CON);
	delay_300us();
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | (mode & CRU_GENERAL_MODE_MASK), CRU_MODE_CON);

	/* resume codec pll */
	cru_writel(cpll, CRU_CPLL_CON);
	delay_300us();
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | (mode & CRU_CODEC_MODE_MASK), CRU_MODE_CON);

	/* enable clock */
	cru_writel(clkgate[0], CRU_CLKGATE0_CON);
	cru_writel(clkgate[1], CRU_CLKGATE1_CON);
	cru_writel(clkgate[2], CRU_CLKGATE2_CON);
	cru_writel(clkgate[3], CRU_CLKGATE3_CON);

	return 0;
}

static int rk29_pm_prepare(void)
{
	printk("+%s\n", __func__);
	disable_hlt(); // disable entering rk29_idle() by disable_hlt()

	/* suspend arm pll */
	apll = cru_readl(CRU_APLL_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CPU_MODE_MASK) | CRU_CPU_MODE_SLOW, CRU_MODE_CON);
	cru_writel(apll | PLL_BYPASS, CRU_APLL_CON);
	cru_writel(apll | PLL_PD | PLL_BYPASS, CRU_APLL_CON);
	delay_500ns();
	lpj = loops_per_jiffy;
	loops_per_jiffy = 120000; // make udelay/mdelay correct

	if (vcore) {
		vcore_uV = regulator_get_voltage(vcore);
		regulator_set_voltage(vcore, 1000000, 1000000);
	}
	printk("-%s\n", __func__);
	return 0;
}

static void rk29_pm_finish(void)
{
	printk("+%s\n", __func__);
	if (vcore) {
		regulator_set_voltage(vcore, vcore_uV, vcore_uV);
	}

	/* resume arm pll */
	loops_per_jiffy = lpj;
	cru_writel(apll, CRU_APLL_CON);
	delay_300us();
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CPU_MODE_MASK) | CRU_CPU_MODE_NORMAL, CRU_MODE_CON);

	enable_hlt();
	printk("-%s\n", __func__);
}

static struct platform_suspend_ops rk29_pm_ops = {
	.enter		= rk29_pm_enter,
	.valid		= suspend_valid_only_mem,
	.prepare 	= rk29_pm_prepare,
	.finish		= rk29_pm_finish,
};

static void rk29_idle(void)
{
	if (!need_resched()) {
		int allow_sleep = 1;
#ifdef CONFIG_HAS_WAKELOCK
		allow_sleep = !has_wake_lock(WAKE_LOCK_IDLE);
#endif
		if (allow_sleep) {
			u32 mode_con = cru_readl(CRU_MODE_CON);
			cru_writel((mode_con & ~CRU_CPU_MODE_MASK) | CRU_CPU_MODE_SLOW, CRU_MODE_CON);
			arch_idle();
			cru_writel((mode_con & ~CRU_CPU_MODE_MASK) | CRU_CPU_MODE_NORMAL, CRU_MODE_CON);
		} else {
			arch_idle();
		}
	}
	local_irq_enable();
}

static int __init rk29_pm_init(void)
{
	vcore = regulator_get(NULL, "vcore");
	if (IS_ERR(vcore)) {
		pr_err("pm: fail to get regulator vcore: %ld\n", PTR_ERR(vcore));
		vcore = NULL;
	}

	suspend_set_ops(&rk29_pm_ops);

	/* set idle function */
	pm_idle = rk29_idle;

	/* for debug */
	console_suspend_enabled = 0;

	return 0;
}
__initcall(rk29_pm_init);
