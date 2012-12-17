#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/random.h>
#include <linux/crc32.h>
#include <linux/io.h>
#include <linux/wakelock.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>

#include <mach/pmu.h>
#include <mach/board.h>
#include <mach/system.h>
#include <mach/sram.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/cru.h>

#define cru_readl(offset)	readl_relaxed(RK2928_CRU_BASE + offset)
#define cru_writel(v, offset)	do { writel_relaxed(v, RK2928_CRU_BASE + offset); dsb(); } while (0)

#define grf_readl(offset)	readl_relaxed(RK2928_GRF_BASE + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK2928_GRF_BASE + offset); dsb(); } while (0)

#define gate_save_soc_clk(val, _save, cons, w_msk) \
	do { \
		(_save) = cru_readl(cons); \
		cru_writel(((~(val) | (_save)) & (w_msk)) | ((w_msk) << 16), cons); \
	} while (0)

void __sramfunc sram_printch(char byte)
{
#ifdef DEBUG_UART_BASE
	u32 clk_gate2, clk_gate4, clk_gate8;

	gate_save_soc_clk(0
			  | (1 << CLK_GATE_ACLK_PERIPH % 16)
			  | (1 << CLK_GATE_HCLK_PERIPH % 16)
			  | (1 << CLK_GATE_PCLK_PERIPH % 16)
			  , clk_gate2, CRU_CLKGATES_CON(2), 0
			  | (1 << ((CLK_GATE_ACLK_PERIPH % 16) + 16))
			  | (1 << ((CLK_GATE_HCLK_PERIPH % 16) + 16))
			  | (1 << ((CLK_GATE_PCLK_PERIPH % 16) + 16)));
	gate_save_soc_clk((1 << CLK_GATE_ACLK_CPU_PERI % 16)
			  , clk_gate4, CRU_CLKGATES_CON(4),
			  (1 << ((CLK_GATE_ACLK_CPU_PERI % 16) + 16)));
	gate_save_soc_clk((1 << ((CLK_GATE_PCLK_UART0 + CONFIG_RK_DEBUG_UART) % 16)),
			  clk_gate8, CRU_CLKGATES_CON(8),
			  (1 << (((CLK_GATE_PCLK_UART0 + CONFIG_RK_DEBUG_UART) % 16) + 16)));
	sram_udelay(1);

	writel_relaxed(byte, DEBUG_UART_BASE);
	dsb();

	/* loop check LSR[6], Transmitter Empty bit */
	while (!(readl_relaxed(DEBUG_UART_BASE + 0x14) & 0x40))
		barrier();

	cru_writel(0xffff0000 | clk_gate2, CRU_CLKGATES_CON(2));
	cru_writel(0xffff0000 | clk_gate4, CRU_CLKGATES_CON(4));
	cru_writel(0xffff0000 | clk_gate8, CRU_CLKGATES_CON(8));

	if (byte == '\n')
		sram_printch('\r');
#endif
}

__weak void __sramfunc ddr_suspend(void) {}
__weak void __sramfunc ddr_resume(void) {}
__weak uint32_t __sramfunc ddr_change_freq(uint32_t nMHz) { return nMHz; }

#ifdef CONFIG_DDR_TEST
static int ddr_debug=0;
module_param(ddr_debug, int, 0644);

static int inline calc_crc32(u32 addr, size_t len)
{
	return crc32_le(~0, (const unsigned char *)addr, len);
}

static void ddr_testmode(void)
{
	int32_t g_crc1, g_crc2;
	uint32_t nMHz;
	uint32_t n = 0;
	uint32_t min,max;
	extern char _stext[], _etext[];


	if (ddr_debug == 1) {
		max=500;
		min=100;
		for (;;) {
			sram_printascii("\n change freq:");
			g_crc1 = calc_crc32((u32)_stext, (size_t)(_etext-_stext));
			do
			{
			    nMHz = min + random32();
			    nMHz %= max;
			}while(nMHz < min);
			sram_printhex(nMHz);
			sram_printch(' ');
			nMHz = ddr_change_freq(nMHz);
			sram_printhex(n++);
			sram_printch(' ');
			g_crc2 = calc_crc32((u32)_stext, (size_t)(_etext-_stext));
			if (g_crc1!=g_crc2) {
				sram_printascii("fail\n");
			}
			//ddr_print("check image crc32 success--crc value = 0x%x!, count:%d\n",g_crc1, n++);
			//     sram_printascii("change freq success\n");
		}
	} else if(ddr_debug == 2) {
		for (;;) {
			sram_printch(' ');
			sram_printch('9');
			sram_printch('9');
			sram_printch('9');
			sram_printch(' ');
			g_crc1 = calc_crc32((u32)_stext, (size_t)(_etext-_stext));
			nMHz = (random32()>>13);// 16.7s max
			ddr_suspend();
			sram_udelay(nMHz);
			ddr_resume();
			sram_printhex(nMHz);
			sram_printch(' ');
			sram_printhex(n++);
			g_crc2 = calc_crc32((u32)_stext, (size_t)(_etext-_stext));
			if (g_crc1 != g_crc2) {
				sram_printch(' ');
				sram_printch('f');
				sram_printch('a');
				sram_printch('i');
				sram_printch('l');
			}
			// ddr_print("check image crc32 fail!, count:%d\n", n++);
			//    sram_printascii("self refresh fail\n");
			//else
			//ddr_print("check image crc32 success--crc value = 0x%x!, count:%d\n",g_crc1, n++);
			//    sram_printascii("self refresh success\n");
		}
	} else if (ddr_debug == 3) {
		extern int memtester(void);
		memtester();
	}
	else
	{
	    ddr_change_freq(ddr_debug);
	    ddr_debug=0;
	}
}
#else
static void ddr_testmode(void) {}
#endif

static noinline void rk2928_pm_dump_irq(void)
{
	u32 irq_gpio = (readl_relaxed(RK2928_GICD_BASE + GIC_DIST_PENDING_SET + (IRQ_GPIO0 / 32) * 4) >> (IRQ_GPIO0 % 32)) & 0xF;
	printk("wakeup irq: %08x %08x %08x\n",
		readl_relaxed(RK2928_GICD_BASE + GIC_DIST_PENDING_SET + 4),
		readl_relaxed(RK2928_GICD_BASE + GIC_DIST_PENDING_SET + 8),
		readl_relaxed(RK2928_GICD_BASE + GIC_DIST_PENDING_SET + 12));
	if (irq_gpio & 1)
		printk("wakeup gpio0: %08x\n", readl_relaxed(RK2928_GPIO0_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 2)
		printk("wakeup gpio1: %08x\n", readl_relaxed(RK2928_GPIO1_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 4)
		printk("wakeup gpio2: %08x\n", readl_relaxed(RK2928_GPIO2_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 8)
		printk("wakeup gpio3: %08x\n", readl_relaxed(RK2928_GPIO3_BASE + GPIO_INT_STATUS));
}

#define DUMP_GPIO_INTEN(ID) \
do { \
	u32 en = readl_relaxed(RK2928_GPIO##ID##_BASE + GPIO_INTEN); \
	if (en) { \
		sram_printascii("GPIO" #ID "_INTEN: "); \
		sram_printhex(en); \
		sram_printch('\n'); \
	} \
} while (0)

static noinline void rk2928_pm_dump_inten(void)
{
	DUMP_GPIO_INTEN(0);
	DUMP_GPIO_INTEN(1);
	DUMP_GPIO_INTEN(2);
	DUMP_GPIO_INTEN(3);
}

static void pm_pll_wait_lock(int pll_idx)
{
	u32 pll_state[4] = {1, 0, 2, 3};
	u32 bit = 0x10u << pll_state[pll_idx];
	int delay = 24000000;
	while (delay > 0) {
		if ((cru_readl(PLL_CONS(pll_idx, 1)) & (0x1 << PLL_LOCK_SHIFT))) {
			break;
		}
		delay--;
	}
	if (delay == 0) {
		sram_printch('p');
		sram_printch('l');
		sram_printch('l');
		sram_printhex(pll_idx);
		sram_printch('\n');
		while(1);
	}
}

#define power_on_pll(id) \
	cru_writel(PLL_PWR_DN_W_MSK|PLL_PWR_ON,PLL_CONS((id),3));\
	pm_pll_wait_lock((id))

static int pm_pll_pwr_up(u8 pll_id)
{
	u32 pllcon0,pllcon1,pllcon2;
	//enter slowmode
	cru_writel(PLL_MODE_SLOW(pll_id), CRU_MODE_CON);
	cru_writel( CRU_W_MSK(PLL_PWR_DN_SHIFT, 0x01), PLL_CONS(pll_id, 1));

	if(pll_id==0) {
		sram_udelay(100);
	} else {
		udelay(100);
	}

	pm_pll_wait_lock(pll_id);
	//return form slow
	//cru_writel(PLL_MODE_NORM(pll_id), CRU_MODE_CON);
	return 0;
}

#define DDR_SAVE_SP(save_sp)		do { save_sp = ddr_save_sp(((unsigned long)SRAM_DATA_END & (~7))); } while (0)
#define DDR_RESTORE_SP(save_sp)		do { ddr_save_sp(save_sp); } while (0)

static unsigned long save_sp;

static noinline void interface_ctr_reg_pread(void)
{
	u32 addr;

	flush_cache_all();
	outer_flush_all();
	local_flush_tlb_all();

	for (addr = (u32)SRAM_CODE_OFFSET; addr < (u32)SRAM_DATA_END; addr += PAGE_SIZE)
		readl_relaxed(addr);
	readl_relaxed(RK2928_GRF_BASE);
	readl_relaxed(RK2928_DDR_PCTL_BASE);
	readl_relaxed(RK2928_DDR_PHY_BASE);
	readl_relaxed(RK2928_GPIO0_BASE);
	readl_relaxed(RK2928_GPIO1_BASE);
	readl_relaxed(RK2928_GPIO2_BASE);
	readl_relaxed(RK2928_GPIO3_BASE);
	#if defined (CONFIG_MACH_RK2928_SDK)
	readl_relaxed(RK2928_RKI2C1_BASE);
	#else
	readl_relaxed(RK2928_RKI2C0_BASE);
	#endif
}

__weak void board_gpio_suspend(void) {}
__weak void board_gpio_resume(void) {}
__weak void __sramfunc board_pmu_suspend(void) {}
__weak void __sramfunc board_pmu_resume(void) {}
__weak void __sramfunc rk30_suspend_voltage_set(unsigned int vol) {}
__weak void __sramfunc rk30_suspend_voltage_resume(unsigned int vol) {}

__weak void rk30_pwm_suspend_voltage_set(void) {}
__weak void rk30_pwm_resume_voltage_set(void) {}

__weak void __sramfunc rk30_pwm_logic_suspend_voltage(void) {}
__weak void __sramfunc rk30_pwm_logic_resume_voltage(void) {}

static void __sramfunc rk2928_sram_suspend(void)
{
	u32 cru_clksel0_con;
	u32 clkgt_regs[CRU_CLKGATES_CON_CNT];
	int i;

	sram_printch('5');
	ddr_suspend();
	sram_printch('6');
	rk30_suspend_voltage_set(1000000);
	rk30_pwm_logic_suspend_voltage();
	board_pmu_suspend();
	sram_printch('7');


	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		clkgt_regs[i] = cru_readl(CRU_CLKGATES_CON(i));
	}
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CORE_PERIPH)
			  | (1 << CLK_GATE_DDRPHY_SRC)
			  | (1 << CLK_GATE_ACLK_CPU)
			  | (1 << CLK_GATE_HCLK_CPU)
			  | (1 << CLK_GATE_PCLK_CPU)
			  | (1 << CLK_GATE_ACLK_CORE)
			  , clkgt_regs[0], CRU_CLKGATES_CON(0), 0xffff);
	if (((clkgt_regs[8] >> (CLK_GATE_PCLK_GPIO0 % 16)) & 0xf) != 0xf) {
		gate_save_soc_clk(0
				  | (1 << CLK_GATE_PERIPH_SRC % 16)
				  | (1 << CLK_GATE_PCLK_PERIPH % 16)
				  , clkgt_regs[2], CRU_CLKGATES_CON(2), 0xffff);
	} else {
		gate_save_soc_clk(0, clkgt_regs[2], CRU_CLKGATES_CON(2), 0xffff);
	}
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_ACLK_STRC_SYS % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM % 16)
			  , clkgt_regs[4], CRU_CLKGATES_CON(4), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PCLK_GRF % 16)
			  | (1 << CLK_GATE_PCLK_DDRUPCTL % 16)
			  , clkgt_regs[5], CRU_CLKGATES_CON(5), 0xffff);
	gate_save_soc_clk(0, clkgt_regs[7], CRU_CLKGATES_CON(7), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CLK_L2C % 16)
			  , clkgt_regs[9], CRU_CLKGATES_CON(9), 0xffff);

//	board_pmu_suspend();
	cru_clksel0_con = cru_readl(CRU_CLKSELS_CON(0));
	cru_writel((0x1f << 16) | 0x1f, CRU_CLKSELS_CON(0));

	dsb();
	wfi();

	cru_writel((0x1f << 16) | cru_clksel0_con, CRU_CLKSELS_CON(0));
//	board_pmu_resume();

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		cru_writel(clkgt_regs[i] | 0xffff0000, CRU_CLKGATES_CON(i));
	}

	sram_printch('7');
	board_pmu_resume();
	rk30_pwm_logic_resume_voltage();
	rk30_suspend_voltage_resume(1100000);

	sram_printch('6');
	ddr_resume();
	sram_printch('5');
}
static void noinline rk2928_suspend(void)
{
	DDR_SAVE_SP(save_sp);
	rk2928_sram_suspend();
	DDR_RESTORE_SP(save_sp);
}

static int rk2928_pm_enter(suspend_state_t state)
{
	u32 i;
	u32 clkgt_regs[CRU_CLKGATES_CON_CNT];
	u32 clk_sel0, clk_sel1, clk_sel10;
	u32 cru_mode_con;
	u32 apll_con1,cpll_con1,gpll_con1;
	// dump GPIO INTEN for debug
	rk2928_pm_dump_inten();

	sram_printch('0');

#ifdef CONFIG_DDR_TEST
	// memory tester
	if (ddr_debug != 0)
		ddr_testmode();
#endif

	sram_printch('1');
	local_fiq_disable();

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		clkgt_regs[i] = cru_readl(CRU_CLKGATES_CON(i));
	}

	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CORE_PERIPH)
			  | (1 << CLK_GATE_CPU_GPLL)
			  | (1 << CLK_GATE_DDRPHY_SRC)
			  | (1 << CLK_GATE_ACLK_CPU)
			  | (1 << CLK_GATE_HCLK_CPU)
			  | (1 << CLK_GATE_PCLK_CPU)
			  | (1 << CLK_GATE_ACLK_CORE)
			  , clkgt_regs[0], CRU_CLKGATES_CON(0), 0xffff);
	gate_save_soc_clk(0, clkgt_regs[1], CRU_CLKGATES_CON(1), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PERIPH_SRC % 16)
			  | (1 << CLK_GATE_PCLK_PERIPH % 16)
			  | (1 << CLK_GATE_ACLK_PERIPH % 16)
			  , clkgt_regs[2], CRU_CLKGATES_CON(2), 0xffff);
	gate_save_soc_clk(0, clkgt_regs[3], CRU_CLKGATES_CON(3), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_HCLK_PERI_AXI_MATRIX % 16)
			  | (1 << CLK_GATE_PCLK_PERI_AXI_MATRIX % 16)
			  | (1 << CLK_GATE_ACLK_CPU_PERI % 16)
			  | (1 << CLK_GATE_ACLK_PERI_AXI_MATRIX % 16)
			  | (1 << CLK_GATE_ACLK_STRC_SYS % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM % 16)
			  , clkgt_regs[4], CRU_CLKGATES_CON(4), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PCLK_GRF % 16)
			  | (1 << CLK_GATE_PCLK_DDRUPCTL % 16)
			  , clkgt_regs[5], CRU_CLKGATES_CON(5), 0xffff);
	gate_save_soc_clk(0, clkgt_regs[6], CRU_CLKGATES_CON(6), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PCLK_PWM01 % 16)
			  , clkgt_regs[7], CRU_CLKGATES_CON(7), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PCLK_GPIO0 % 16)
			  | (1 << CLK_GATE_PCLK_GPIO1 % 16)
			  | (1 << CLK_GATE_PCLK_GPIO2 % 16)
			  | (1 << CLK_GATE_PCLK_GPIO3 % 16)
			  , clkgt_regs[8], CRU_CLKGATES_CON(8), 0xffff);
	cru_writel( ((1 << CLK_GATE_PCLK_GPIO0 % 16)<<16),  CRU_CLKGATES_CON(8));
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CLK_L2C % 16)
			  | (1 << CLK_GATE_HCLK_PERI_ARBI % 16)
			  | (1 << CLK_GATE_ACLK_PERI_NIU % 16)
			  , clkgt_regs[9], CRU_CLKGATES_CON(9), 0xffff);

	sram_printch('2');

	cru_mode_con = cru_readl(CRU_MODE_CON);

	//apll
	clk_sel0 = cru_readl(CRU_CLKSELS_CON(0));
	clk_sel1 = cru_readl(CRU_CLKSELS_CON(1));
	apll_con1 = cru_readl(PLL_CONS(APLL_ID, 1));
	
	cru_writel(PLL_MODE_SLOW(APLL_ID), CRU_MODE_CON);
	cru_writel(CLK_CORE_DIV(1) | ACLK_CPU_DIV(1) | CPU_SEL_PLL(SEL_APLL), CRU_CLKSELS_CON(0));
	cru_writel(CLK_CORE_PERI_DIV(1) | ACLK_CORE_DIV(1) | HCLK_CPU_DIV(1) | PCLK_CPU_DIV(1), CRU_CLKSELS_CON(1));
	//cru_writel((0x01 <<(PLL_PWR_DN_SHIFT + 16))|(1<<PLL_PWR_DN_SHIFT), PLL_CONS(APLL_ID, 1)); 
 	cru_writel(CRU_W_MSK_SETBIT(0x01, PLL_PWR_DN_SHIFT) , PLL_CONS(APLL_ID, 1)); //power down apll

	//cpll
	cpll_con1 =   cru_readl(PLL_CONS(CPLL_ID, 1));
	
	cru_writel(PLL_MODE_SLOW(CPLL_ID), CRU_MODE_CON);
	cru_writel(CRU_W_MSK_SETBIT(0x01, PLL_PWR_DN_SHIFT), PLL_CONS(CPLL_ID, 1));//power down cpll

	//gpll
	clk_sel10 = cru_readl(CRU_CLKSELS_CON(10));
	gpll_con1 = cru_readl(PLL_CONS(GPLL_ID, 1));
	
	cru_writel(PLL_MODE_SLOW(GPLL_ID), CRU_MODE_CON);
	cru_writel(PERI_SET_ACLK_DIV(1)
		   | PERI_SET_A2H_RATIO(RATIO_11)
		   | PERI_SET_A2P_RATIO(RATIO_11)
		   , CRU_CLKSELS_CON(10));
	cru_writel(CRU_W_MSK_SETBIT(0x01, PLL_PWR_DN_SHIFT), PLL_CONS(GPLL_ID, 1));//power down gpll

	sram_printch('3');
	rk30_pwm_suspend_voltage_set();

	board_gpio_suspend();

	interface_ctr_reg_pread();

	sram_printch('4');
	rk2928_suspend();
	sram_printch('4');

	board_gpio_resume();
	rk30_pwm_resume_voltage_set();
	sram_printch('3');

	//gpll
	pm_pll_pwr_up(GPLL_ID);
	cru_writel(0xffff0000 | clk_sel10, CRU_CLKSELS_CON(10));
	cru_writel(clk_sel10, CRU_CLKSELS_CON(10));
	cru_writel((PLL_MODE_MSK(GPLL_ID) << 16) | (PLL_MODE_MSK(GPLL_ID) & cru_mode_con), CRU_MODE_CON);

	//cpll
	pm_pll_pwr_up(CPLL_ID);
	cru_writel((PLL_MODE_MSK(CPLL_ID) << 16) | (PLL_MODE_MSK(CPLL_ID) & cru_mode_con), CRU_MODE_CON);

	//apll
	pm_pll_pwr_up(APLL_ID);
	cru_writel(0xffff0000 | clk_sel1, CRU_CLKSELS_CON(1));
	cru_writel(0xffff0000 | clk_sel0, CRU_CLKSELS_CON(0));
	cru_writel((PLL_MODE_MSK(APLL_ID) << 16) | (PLL_MODE_MSK(APLL_ID) & cru_mode_con), CRU_MODE_CON);

	sram_printch('2');

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		cru_writel(clkgt_regs[i] | 0xffff0000, CRU_CLKGATES_CON(i));
	}

	local_fiq_enable();
	sram_printch('1');

	sram_printascii("0\n");

	rk2928_pm_dump_irq();

	return 0;
}

static int rk2928_pm_prepare(void)
{
	/* disable entering idle by disable_hlt() */
	disable_hlt();
	return 0;
}

static void rk2928_pm_finish(void)
{
	enable_hlt();
}

static struct platform_suspend_ops rk2928_pm_ops = {
	.enter		= rk2928_pm_enter,
	.valid		= suspend_valid_only_mem,
	.prepare 	= rk2928_pm_prepare,
	.finish		= rk2928_pm_finish,
};

static int __init rk2928_pm_init(void)
{
	suspend_set_ops(&rk2928_pm_ops);

#ifdef CONFIG_EARLYSUSPEND
	pm_set_vt_switch(0); /* disable vt switch while suspend */
#endif

	return 0;
}
__initcall(rk2928_pm_init);
