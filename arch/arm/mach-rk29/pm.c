#define DEBUG

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#ifdef CONFIG_RK29_PWM_REGULATOR
#include <linux/regulator/rk29-pwm-regulator.h>
#endif
#include <linux/io.h>
#include <linux/wakelock.h>
#include <asm/tlbflush.h>

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

#define LOOPS_PER_USEC	13
#define LOOP(loops) do { int i = loops; barrier(); while (i--) barrier(); } while (0)

static inline void delay_500ns(void)
{
	LOOP(LOOPS_PER_USEC);
}

static inline void delay_300us(void)
{
	LOOP(300 * LOOPS_PER_USEC);
}

extern void ddr_suspend(void);
extern void ddr_resume(void);

#ifdef DEBUG
static void inline printch(char byte)
{
	unsigned long flags;
	unsigned int delay;
	u32 gate1, gate2;

	local_irq_save(flags);
	gate1 = cru_readl(CRU_CLKGATE1_CON);
	gate2 = cru_readl(CRU_CLKGATE2_CON);
	cru_writel(gate1 & ~((1 << CLK_GATE_PCLK_PEIRPH % 32) | (1 << CLK_GATE_ACLK_PEIRPH % 32) | (1 << CLK_GATE_ACLK_CPU_PERI % 32)), CRU_CLKGATE1_CON);
	cru_writel(gate2 & ~(1 << CLK_GATE_UART1 % 32), CRU_CLKGATE2_CON);
	delay_500ns();

	writel(byte, RK29_UART1_BASE);

	delay = (cru_readl(CRU_MODE_CON) & CRU_CPU_MODE_MASK) ? 10 : 1;
	while (delay--)
		delay_300us();

	cru_writel(gate2, CRU_CLKGATE2_CON);
	cru_writel(gate1, CRU_CLKGATE1_CON);
	local_irq_restore(flags);
	if (byte == '\n')
		printch('\r');
}

static void inline printascii(const char *s)
{
	while (*s) {
		printch(*s);
		s++;
	}
}

static void inline printhex(unsigned int hex)
{
	int i = 8;
	printch('0');
	printch('x');
	while (i--) {
		unsigned char c = (hex & 0xF0000000) >> 28;
		printch(c < 0xa ? c + '0' : c - 0xa + 'a');
		hex <<= 4;
	}
}
#else
static void inline printch(char byte) {}
static void inline printascii(const char *s) {}
static void inline printhex(unsigned int hex) {}
#endif /* DEBUG */

#ifdef CONFIG_RK29_PWM_REGULATOR
#define pwm_write_reg(addr, val)	__raw_writel(val, addr + (RK29_PWM_BASE + 2*0x10))
#define pwm_read_reg(addr)		__raw_readl(addr + (RK29_PWM_BASE + 2*0x10))

static u32 __sramdata pwm_lrc, pwm_hrc;
static void __sramfunc rk29_set_core_voltage(int uV)
{
	u32 gate1;

	gate1 = cru_readl(CRU_CLKGATE1_CON);
	cru_writel(gate1 & ~((1 << CLK_GATE_PCLK_PEIRPH % 32) | (1 << CLK_GATE_ACLK_PEIRPH % 32) | (1 << CLK_GATE_ACLK_CPU_PERI % 32)), CRU_CLKGATE1_CON);

	/* iomux pwm2 */
	writel((readl(RK29_GRF_BASE + 0x58) & ~(0x3<<6)) | (0x2<<6), RK29_GRF_BASE + 0x58);

	if (uV) {
		pwm_lrc = pwm_read_reg(PWM_REG_LRC);
		pwm_hrc = pwm_read_reg(PWM_REG_HRC);
	}

	pwm_write_reg(PWM_REG_CTRL, PWM_DIV|PWM_RESET);
	if (uV == 1000000) {
		pwm_write_reg(PWM_REG_LRC, 12);
		pwm_write_reg(PWM_REG_HRC, 10);
	} else {
		pwm_write_reg(PWM_REG_LRC, pwm_lrc);
		pwm_write_reg(PWM_REG_HRC, pwm_hrc);
	}
	pwm_write_reg(PWM_REG_CNTR, 0);
	pwm_write_reg(PWM_REG_CTRL, PWM_DIV|PWM_ENABLE|PWM_TimeEN);

	LOOP(5 * 1000 * LOOPS_PER_USEC); /* delay 5ms */

	cru_writel(gate1, CRU_CLKGATE1_CON);
}
#endif /* CONFIG_RK29_PWM_REGULATOR */

static void __sramfunc rk29_sram_suspend(void)
{
	u32 clksel0;

	printch('5');
	ddr_suspend();

	printch('6');
#ifdef CONFIG_RK29_PWM_REGULATOR
	rk29_set_core_voltage(1000000);
#endif
	printch('7');
	clksel0 = cru_readl(CRU_CLKSEL0_CON);
	/* set arm clk 24MHz/32 = 750KHz */
	cru_writel(clksel0 | 0x1F, CRU_CLKSEL0_CON);

	asm("wfi");

	/* resume arm clk */
	cru_writel(clksel0, CRU_CLKSEL0_CON);
	printch('7');

#ifdef CONFIG_RK29_PWM_REGULATOR
	rk29_set_core_voltage(0);
#endif
	printch('6');

	ddr_resume();
	printch('5');
}

static void noinline rk29_suspend(void)
{
	DDR_SAVE_SP(save_sp);
	rk29_sram_suspend();
	DDR_RESTORE_SP(save_sp);
}

static int rk29_pm_enter(suspend_state_t state)
{
	u32 apll, cpll, gpll, mode, clksel0;
	u32 clkgate[4];
	printch('0');

#ifdef CONFIG_RK29_PWM_REGULATOR
	/* touch TLB */
	flush_tlb_all();
	readl(RK29_PWM_BASE);
	readl(RK29_GRF_BASE);
#endif

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
	printch('1');

	mode = cru_readl(CRU_MODE_CON);
	clksel0 = cru_readl(CRU_CLKSEL0_CON);

	/* suspend arm pll */
	apll = cru_readl(CRU_APLL_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CPU_MODE_MASK) | CRU_CPU_MODE_SLOW, CRU_MODE_CON);
	cru_writel(apll | PLL_BYPASS, CRU_APLL_CON);
	cru_writel(apll | PLL_PD | PLL_BYPASS, CRU_APLL_CON);
	delay_500ns();
	/* set core = aclk_cpu = hclk_cpu = pclk_cpu = 24MHz */
	cru_writel(clksel0 & 0xFFFFF000, CRU_CLKSEL0_CON);
	printch('2');

	/* suspend codec pll */
	cpll = cru_readl(CRU_CPLL_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | CRU_CODEC_MODE_SLOW, CRU_MODE_CON);
	cru_writel(cpll | PLL_BYPASS, CRU_CPLL_CON);
	cru_writel(cpll | PLL_PD | PLL_BYPASS, CRU_CPLL_CON);
	delay_500ns();
	printch('3');

	/* suspend general pll */
	gpll = cru_readl(CRU_GPLL_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | CRU_GENERAL_MODE_SLOW, CRU_MODE_CON);
	cru_writel(gpll | PLL_BYPASS, CRU_GPLL_CON);
	cru_writel(gpll | PLL_PD | PLL_BYPASS, CRU_GPLL_CON);
	delay_500ns();
	/* set aclk_periph = hclk_periph = pclk_periph = 24MHz */
	cru_writel(clksel0 & ~0x7FC000, CRU_CLKSEL0_CON);

	printch('4');
	rk29_suspend();
	printch('4');

	/* resume general pll */
	cru_writel(gpll, CRU_GPLL_CON);
	delay_300us();
	/* restore aclk_periph/hclk_periph/pclk_periph */
	cru_writel(cru_readl(CRU_CLKSEL0_CON) | (clksel0 & 0x7FC000), CRU_CLKSEL0_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | (mode & CRU_GENERAL_MODE_MASK), CRU_MODE_CON);
	printch('3');

	/* resume codec pll */
	cru_writel(cpll, CRU_CPLL_CON);
	delay_300us();
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | (mode & CRU_CODEC_MODE_MASK), CRU_MODE_CON);
	printch('2');

	/* resume arm pll */
	cru_writel(apll, CRU_APLL_CON);
	delay_300us();
	/* restore core/aclk_cpu/hclk_cpu/pclk_cpu */
	cru_writel(cru_readl(CRU_CLKSEL0_CON) | (clksel0 & 0xFFF), CRU_CLKSEL0_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CPU_MODE_MASK) | (mode & CRU_CPU_MODE_MASK), CRU_MODE_CON);
	printch('1');

	/* enable clock */
	cru_writel(clkgate[0], CRU_CLKGATE0_CON);
	cru_writel(clkgate[1], CRU_CLKGATE1_CON);
	cru_writel(clkgate[2], CRU_CLKGATE2_CON);
	cru_writel(clkgate[3], CRU_CLKGATE3_CON);
	printascii("0\n");

	return 0;
}

static int rk29_pm_prepare(void)
{
	/* disable entering rk29_idle() by disable_hlt() */
	disable_hlt();
	return 0;
}

static void rk29_pm_finish(void)
{
	enable_hlt();
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
	suspend_set_ops(&rk29_pm_ops);

	/* set idle function */
	pm_idle = rk29_idle;

	return 0;
}
__initcall(rk29_pm_init);
