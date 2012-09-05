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
#include <mach/ddr.h>

#define cru_readl(offset)	readl_relaxed(RK30_CRU_BASE + offset)
#define cru_writel(v, offset)	do { writel_relaxed(v, RK30_CRU_BASE + offset); dsb(); } while (0)

#define pmu_readl(offset)	readl_relaxed(RK30_PMU_BASE + offset)
#define pmu_writel(v,offset)	do { writel_relaxed(v, RK30_PMU_BASE + offset); dsb(); } while (0)

#define grf_readl(offset)	readl_relaxed(RK30_GRF_BASE + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK30_GRF_BASE + offset); dsb(); } while (0)

#define gate_save_soc_clk(val,_save,cons,w_msk) \
	(_save)=cru_readl(cons);\
	cru_writel((((~(val)|(_save))&(w_msk))|((w_msk)<<16)),cons)

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

#ifdef CONFIG_DDR_TEST
static int ddr_debug=0;
module_param(ddr_debug, int, 0644);

static int inline calc_crc32(u32 addr, size_t len)
{
	return crc32_le(~0, (const unsigned char *)addr, len);
}

extern __sramdata uint32_t mem_type;
static void ddr_testmode(void)
{
	int32_t g_crc1, g_crc2;
	uint32_t nMHz;
	uint32_t n = 0;
	uint32_t min,max;
	extern char _stext[], _etext[];


	if (ddr_debug == 1) {
	    switch(mem_type)
	    {
	        case 0:  //LPDDR
	        case 1:  //DDR
	            max = 210;
	            min = 100;
	            break;
	        case 2:  //DDR2
	        case 4:  //LPDDR2
	            max=410;
	            min=100;
	            break;
	        case 3:  //DDR3
	        default:
	            max=500;
	            min=100;
	            break;
	    }
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

static noinline void rk30_pm_dump_irq(void)
{
	u32 irq_gpio = (readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 8) >> 22) & 0x7F;
	printk("wakeup irq: %08x %08x %08x %08x\n",
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 4),
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 8),
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 12),
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 16));
	if (irq_gpio & 1)
		printk("wakeup gpio0: %08x\n", readl_relaxed(RK30_GPIO0_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 2)
		printk("wakeup gpio1: %08x\n", readl_relaxed(RK30_GPIO1_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 4)
		printk("wakeup gpio2: %08x\n", readl_relaxed(RK30_GPIO2_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 8)
		printk("wakeup gpio3: %08x\n", readl_relaxed(RK30_GPIO3_BASE + GPIO_INT_STATUS));
#if !defined(CONFIG_ARCH_RK3066B)
	if (irq_gpio & 0x10)
		printk("wakeup gpio4: %08x\n", readl_relaxed(RK30_GPIO4_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 0x40)
		printk("wakeup gpio6: %08x\n", readl_relaxed(RK30_GPIO6_BASE + GPIO_INT_STATUS));
#endif
}

#define DUMP_GPIO_INTEN(ID) \
do { \
	u32 en = readl_relaxed(RK30_GPIO##ID##_BASE + GPIO_INTEN); \
	if (en) { \
		sram_printascii("GPIO" #ID "_INTEN: "); \
		sram_printhex(en); \
		sram_printch('\n'); \
	} \
} while (0)

static noinline void rk30_pm_dump_inten(void)
{
	DUMP_GPIO_INTEN(0);
	DUMP_GPIO_INTEN(1);
	DUMP_GPIO_INTEN(2);
	DUMP_GPIO_INTEN(3);
#if !defined(CONFIG_ARCH_RK3066B)
	DUMP_GPIO_INTEN(4);
	DUMP_GPIO_INTEN(6);
#endif
}

static void pm_pll_wait_lock(int pll_idx)
{
	u32 pll_state[4] = { 1, 0, 2, 3 };
#if defined(CONFIG_ARCH_RK3066B)
	u32 bit = 0x20u << pll_state[pll_idx];
#else
	u32 bit = 0x10u << pll_state[pll_idx];
#endif
	u32 delay = 2400000U;
	while (delay > 0) {
		if (grf_readl(GRF_SOC_STATUS0) & bit)
			break;
		delay--;
	}
	if (delay == 0) {
		//CRU_PRINTK_ERR("wait pll bit 0x%x time out!\n", bit); 
		sram_printch('p');
		sram_printch('l');
		sram_printch('l');
		sram_printhex(pll_idx);
		sram_printch('\n');
	}
}

#define power_on_pll(id) \
	cru_writel(PLL_PWR_DN_W_MSK|PLL_PWR_ON,PLL_CONS((id),3));\
	pm_pll_wait_lock((id))

#define DDR_SAVE_SP(save_sp)		do { save_sp = ddr_save_sp(((unsigned long)SRAM_DATA_END & (~7))); } while (0)
#define DDR_RESTORE_SP(save_sp)		do { ddr_save_sp(save_sp); } while (0)

static unsigned long save_sp;

static noinline void interface_ctr_reg_pread(void)
{
	u32 addr;

	flush_cache_all();
	outer_flush_all();
	local_flush_tlb_all();

	for (addr = (u32)SRAM_CODE_OFFSET; addr < (u32)SRAM_CODE_END; addr += PAGE_SIZE)
		readl_relaxed(addr);
	for (addr = (u32)SRAM_DATA_OFFSET; addr < (u32)SRAM_DATA_END; addr += PAGE_SIZE)
		readl_relaxed(addr);
	readl_relaxed(RK30_PMU_BASE);
	readl_relaxed(RK30_GRF_BASE);
	readl_relaxed(RK30_DDR_PCTL_BASE);
	readl_relaxed(RK30_DDR_PUBL_BASE);
	readl_relaxed(RK30_I2C1_BASE);
}

static inline bool pm_pmu_power_domain_is_on(enum pmu_power_domain pd, u32 pmu_pwrdn_st)
{
	return !(pmu_pwrdn_st & (1 << pd));
}

static void rk30_pm_set_power_domain(u32 pmu_pwrdn_st, bool state)
{
#if !defined(CONFIG_ARCH_RK3066B)
	if (pm_pmu_power_domain_is_on(PD_DBG, pmu_pwrdn_st))
		pmu_set_power_domain(PD_DBG, state);
#endif

	if (pm_pmu_power_domain_is_on(PD_GPU, pmu_pwrdn_st)) {
#if defined(CONFIG_ARCH_RK3066B)
		u32 gate[3];
		gate[0] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU_MST));
		gate[1] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU_SLV));
		gate[2] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_CLK_GPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_CLK_GPU), CLK_GATE_CLKID_CONS(CLK_GATE_CLK_GPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU_MST), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU_MST));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU_SLV), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU_SLV));
		pmu_set_power_domain(PD_GPU, state);
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU_MST) | gate[0], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU_MST));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU_SLV) | gate[1], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU_SLV));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_CLK_GPU) | gate[2], CLK_GATE_CLKID_CONS(CLK_GATE_CLK_GPU));
#else
		u32 gate[2];
		gate[0] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_GPU_SRC));
		gate[1] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_GPU_SRC), CLK_GATE_CLKID_CONS(CLK_GATE_GPU_SRC));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU));
		pmu_set_power_domain(PD_GPU, state);
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_GPU_SRC) | gate[0], CLK_GATE_CLKID_CONS(CLK_GATE_GPU_SRC));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU) | gate[1], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU));
#endif
	}

	if (pm_pmu_power_domain_is_on(PD_VIDEO, pmu_pwrdn_st)) {
		u32 gate[3];
		gate[0] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VEPU));
		gate[1] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VDPU));
		gate[2] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VCODEC));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VEPU), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VEPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VDPU), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VDPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VCODEC), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VCODEC));
		pmu_set_power_domain(PD_VIDEO, state);
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VEPU) | gate[0], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VEPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VDPU) | gate[1], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VDPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VCODEC) | gate[2], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VCODEC));
	}

	if (pm_pmu_power_domain_is_on(PD_VIO, pmu_pwrdn_st)) {
		u32 gate[10];
		gate[0] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC0_SRC));
		gate[1] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC1_SRC));
		gate[2] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC0));
		gate[3] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC1));
		gate[4] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_CIF0));
		gate[5] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_CIF1));
		gate[6] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VIO0));
		gate[7] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VIO1));
		gate[8] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_IPP));
		gate[9] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_RGA));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_LCDC0_SRC), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC0_SRC));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_LCDC1_SRC), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC1_SRC));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_LCDC0), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC0));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_LCDC1), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC1));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_CIF0), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_CIF0));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_CIF1), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_CIF1));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VIO0), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VIO0));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VIO1), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VIO1));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_IPP), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_IPP));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_RGA), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_RGA));
		pmu_set_power_domain(PD_VIO, state);
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_LCDC0_SRC) | gate[0], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC0_SRC));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_LCDC1_SRC) | gate[1], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC1_SRC));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_LCDC0) | gate[2], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC0));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_LCDC1) | gate[3], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_LCDC1));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_CIF0) | gate[4], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_CIF0));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_CIF1) | gate[5], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_CIF1));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VIO0) | gate[6], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VIO0));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VIO1) | gate[7], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VIO1));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_IPP) | gate[8], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_IPP));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_RGA) | gate[9], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_RGA));
	}
}

__weak void board_gpio_suspend(void) {}
__weak void board_gpio_resume(void) {}
__weak void __sramfunc board_pmu_suspend(void) {}
__weak void __sramfunc board_pmu_resume(void) {}
__weak void __sramfunc rk30_suspend_voltage_set(unsigned int vol){}
__weak void __sramfunc rk30_suspend_voltage_resume(unsigned int vol){}

__weak void  rk30_pwm_suspend_voltage_set(void){}
__weak void  rk30_pwm_resume_voltage_set(void){}

__weak void __sramfunc rk30_pwm_logic_suspend_voltage(void){}
__weak void __sramfunc rk30_pwm_logic_resume_voltage(void){}

static void __sramfunc rk30_sram_suspend(void)
{
	u32 cru_clksel0_con;
	u32 clkgt_regs[CRU_CLKGATES_CON_CNT];
	u32 cru_mode_con;
	int i;
	
	sram_printch('5');
	ddr_suspend();
	sram_printch('6');
	rk30_suspend_voltage_set(1000000);
	rk30_pwm_logic_suspend_voltage();
	sram_printch('7');
	

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		clkgt_regs[i] = cru_readl(CRU_CLKGATES_CON(i));
	}
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CORE_PERIPH)
			  | (1 << CLK_GATE_ACLK_CPU)
			  | (1 << CLK_GATE_HCLK_CPU)
			  | (1 << CLK_GATE_PCLK_CPU)
			  | (1 << CLK_GATE_ACLK_CORE)
			  , clkgt_regs[0], CRU_CLKGATES_CON(0), 0xffff);
	gate_save_soc_clk(0, clkgt_regs[1], CRU_CLKGATES_CON(1), 0xffff);
#if defined(CONFIG_ARCH_RK3066B)
	if(((clkgt_regs[8] >> CLK_GATE_PCLK_GPIO3% 16) & 0x01) == 0x01){
#else
	if(((clkgt_regs[8] >> CLK_GATE_PCLK_GPIO3% 16) & 0x03) == 0x03){
#endif
		gate_save_soc_clk(0
				, clkgt_regs[2], CRU_CLKGATES_CON(2), 0xffff);

	}else{
		gate_save_soc_clk(0
				  | (1 << CLK_GATE_PERIPH_SRC % 16)
				  | (1 << CLK_GATE_PCLK_PERIPH % 16)
				, clkgt_regs[2], CRU_CLKGATES_CON(2), 0xffff);
	}
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_ACLK_STRC_SYS % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM % 16)
			  | (1 << CLK_GATE_HCLK_L2MEM % 16)
			  , clkgt_regs[4], CRU_CLKGATES_CON(4), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PCLK_GRF % 16)
			  | (1 << CLK_GATE_PCLK_PMU % 16)
			  , clkgt_regs[5], CRU_CLKGATES_CON(5), 0xffff);
	gate_save_soc_clk(0, clkgt_regs[7], CRU_CLKGATES_CON(7), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CLK_L2C % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM0 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM1 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM2 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM3 % 16)
			  , clkgt_regs[9], CRU_CLKGATES_CON(9), 0x07ff);
	
#ifdef CONFIG_CLK_SWITCH_TO_32K
	cru_mode_con = cru_readl(CRU_MODE_CON);
	cru_writel(0|
		PLL_MODE_DEEP(APLL_ID)|
		PLL_MODE_DEEP(DPLL_ID)|
		PLL_MODE_DEEP(CPLL_ID)|PLL_MODE_DEEP(GPLL_ID),CRU_MODE_CON);
	board_pmu_suspend();
#else
	board_pmu_suspend();
	cru_clksel0_con = cru_readl(CRU_CLKSELS_CON(0));
	cru_writel(CORE_CLK_DIV_W_MSK | CORE_CLK_DIV_MSK | CPU_CLK_DIV_W_MSK | CPU_CLK_DIV_MSK, CRU_CLKSELS_CON(0));
#endif

	dsb();
	wfi();

#ifdef CONFIG_CLK_SWITCH_TO_32K
	board_pmu_resume();
	cru_writel((0xffff<<16) | cru_mode_con, CRU_MODE_CON);
#else
	cru_writel(CORE_CLK_DIV_W_MSK | CPU_CLK_DIV_W_MSK | cru_clksel0_con, CRU_CLKSELS_CON(0));
	board_pmu_resume();
#endif
	
	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		cru_writel(clkgt_regs[i] | 0xffff0000, CRU_CLKGATES_CON(i));
	}

	sram_printch('7');
	rk30_pwm_logic_resume_voltage();
	rk30_suspend_voltage_resume(1100000);
	
	sram_printch('6');
	ddr_resume();
	sram_printch('5');
	
}

static void noinline rk30_suspend(void)
{
	DDR_SAVE_SP(save_sp);
	rk30_sram_suspend();
	DDR_RESTORE_SP(save_sp);
}

static int rk30_pm_enter(suspend_state_t state)
{
	u32 i;
	u32 clkgt_regs[CRU_CLKGATES_CON_CNT];
	u32 clk_sel0, clk_sel1, clk_sel10;
	u32 cpll_con3;
	u32 cru_mode_con;
	u32 pmu_pwrdn_st;

	// dump GPIO INTEN for debug
	rk30_pm_dump_inten();
#if !defined(CONFIG_ARCH_RK3066B)
	//gpio6_b7
	grf_writel(0xc0004000, 0x10c);
	cru_writel(0x07000000, CRU_MISC_CON);
#endif

	sram_printch('0');

	pmu_pwrdn_st = pmu_readl(PMU_PWRDN_ST);
	rk30_pm_set_power_domain(pmu_pwrdn_st, false);

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
#if defined(CONFIG_ARCH_RK3066B)
			  | (1 << CLK_GATE_CPU_GPLL_PATH)
			  | (1 << CLK_GATE_ACLK_CORE)
#endif
			  | (1 << CLK_GATE_DDRPHY)
			  | (1 << CLK_GATE_ACLK_CPU)
			  | (1 << CLK_GATE_HCLK_CPU)
			  | (1 << CLK_GATE_PCLK_CPU)
			  , clkgt_regs[0], CRU_CLKGATES_CON(0), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_DDR_GPLL % 16)
			  , clkgt_regs[1], CRU_CLKGATES_CON(1), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PERIPH_SRC % 16)
			  | (1 << CLK_GATE_PCLK_PERIPH % 16)
			  | (1 << CLK_GATE_ACLK_PERIPH % 16)
			  , clkgt_regs[2], CRU_CLKGATES_CON(2), 0xffff);
	gate_save_soc_clk(0, clkgt_regs[3], CRU_CLKGATES_CON(3), 0xff9f);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_HCLK_PERI_AXI_MATRIX % 16)
			  | (1 << CLK_GATE_PCLK_PERI_AXI_MATRIX % 16)
			  | (1 << CLK_GATE_ACLK_CPU_PERI % 16)
			  | (1 << CLK_GATE_ACLK_PERI_AXI_MATRIX % 16)
			  | (1 << CLK_GATE_ACLK_PEI_NIU % 16)
			  | (1 << CLK_GATE_HCLK_PERI_AHB_ARBI % 16)
			  | (1 << CLK_GATE_HCLK_CPUBUS % 16)
			  | (1 << CLK_GATE_ACLK_STRC_SYS % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM % 16)
			  | (1 << CLK_GATE_HCLK_L2MEM % 16)
			  , clkgt_regs[4], CRU_CLKGATES_CON(4), 0xffff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PCLK_GRF % 16)
			  | (1 << CLK_GATE_PCLK_PMU % 16)
			  | (1 << CLK_GATE_PCLK_DDRUPCTL % 16)
			  , clkgt_regs[5], CRU_CLKGATES_CON(5), 0xffff);
	gate_save_soc_clk(0, clkgt_regs[6], CRU_CLKGATES_CON(6), 0xffff);
	gate_save_soc_clk(0
			|(1 << CLK_GATE_PCLK_PWM23%16)
			, clkgt_regs[7], CRU_CLKGATES_CON(7), 0xffff);
	gate_save_soc_clk(0 , clkgt_regs[8], CRU_CLKGATES_CON(8), 0x01ff);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CLK_L2C % 16)
			  | (1 << CLK_GATE_PCLK_PUBL % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM0 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM1 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM2 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM3 % 16)
			  , clkgt_regs[9], CRU_CLKGATES_CON(9), 0x07ff);

	sram_printch('2');

	cru_mode_con = cru_readl(CRU_MODE_CON);

	//cpll
	cru_writel(PLL_MODE_SLOW(CPLL_ID), CRU_MODE_CON);
	cpll_con3 = cru_readl(PLL_CONS(CPLL_ID, 3));
	cru_writel(PLL_PWR_DN_MSK | PLL_PWR_DN, PLL_CONS(CPLL_ID, 3));

	//gpll
	cru_writel(PLL_MODE_SLOW(GPLL_ID), CRU_MODE_CON);
	clk_sel10 = cru_readl(CRU_CLKSELS_CON(10));
	cru_writel(CRU_W_MSK_SETBITS(0, PERI_ACLK_DIV_OFF, PERI_ACLK_DIV_MASK)
		   | CRU_W_MSK_SETBITS(0, PERI_HCLK_DIV_OFF, PERI_HCLK_DIV_MASK)
		   | CRU_W_MSK_SETBITS(0, PERI_PCLK_DIV_OFF, PERI_PCLK_DIV_MASK)
		   , CRU_CLKSELS_CON(10));
	cru_writel(PLL_PWR_DN_MSK | PLL_PWR_DN, PLL_CONS(GPLL_ID, 3));

	//apll
	clk_sel0 = cru_readl(CRU_CLKSELS_CON(0));
	clk_sel1 = cru_readl(CRU_CLKSELS_CON(1));

	cru_writel(PLL_MODE_SLOW(APLL_ID), CRU_MODE_CON);
	cru_writel(CORE_PERIPH_MSK | CORE_PERIPH_2
		   | CORE_CLK_DIV_W_MSK | CORE_CLK_DIV(1)
		   | CPU_CLK_DIV_W_MSK | CPU_CLK_DIV(1)
		   | CORE_SEL_PLL_W_MSK | CORE_SEL_APLL
		   | CPU_SEL_PLL_W_MSK | CPU_SEL_APLL
		   , CRU_CLKSELS_CON(0));
	cru_writel(CORE_ACLK_W_MSK | CORE_ACLK_11
		   | CPU_ACLK_W_MSK | CPU_ACLK_11
		   | ACLK_HCLK_W_MSK | ACLK_HCLK_11
		   | ACLK_PCLK_W_MSK | ACLK_PCLK_11
		   | AHB2APB_W_MSK | AHB2APB_11
		   , CRU_CLKSELS_CON(1));
	cru_writel(PLL_PWR_DN_W_MSK | PLL_PWR_DN, PLL_CONS(APLL_ID, 3));

	sram_printch('3');
	rk30_pwm_suspend_voltage_set();

	board_gpio_suspend();

	interface_ctr_reg_pread();

	sram_printch('4');
	rk30_suspend();
	sram_printch('4');

	board_gpio_resume();
	rk30_pwm_resume_voltage_set();
	sram_printch('3');

	//apll
	cru_writel(0xffff0000 | clk_sel1, CRU_CLKSELS_CON(1));
	cru_writel(0xffff0000 | clk_sel0, CRU_CLKSELS_CON(0));
	power_on_pll(APLL_ID);
	cru_writel((PLL_MODE_MSK(APLL_ID) << 16) | (PLL_MODE_MSK(APLL_ID) & cru_mode_con), CRU_MODE_CON);

	//gpll
	cru_writel(0xffff0000 | clk_sel10, CRU_CLKSELS_CON(10));
	power_on_pll(GPLL_ID);
	cru_writel((PLL_MODE_MSK(GPLL_ID) << 16) | (PLL_MODE_MSK(GPLL_ID) & cru_mode_con), CRU_MODE_CON);

	//cpll
	if (((cpll_con3 & PLL_PWR_DN_MSK) == PLL_PWR_ON) &&
	    ((PLL_MODE_NORM(CPLL_ID) & PLL_MODE_MSK(CPLL_ID)) == (cru_mode_con & PLL_MODE_MSK(CPLL_ID)))) {
		power_on_pll(CPLL_ID);
	}
	cru_writel((PLL_MODE_MSK(CPLL_ID) << 16) | (PLL_MODE_MSK(CPLL_ID) & cru_mode_con), CRU_MODE_CON);

	sram_printch('2');

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		cru_writel(clkgt_regs[i] | 0xffff0000, CRU_CLKGATES_CON(i));
	}

	local_fiq_enable();
	sram_printch('1');

	rk30_pm_set_power_domain(pmu_pwrdn_st, true);

	sram_printascii("0\n");

	rk30_pm_dump_irq();

	return 0;
}

static int rk30_pm_prepare(void)
{
	/* disable entering idle by disable_hlt() */
	disable_hlt();
	return 0;
}

static void rk30_pm_finish(void)
{
	enable_hlt();
}

static struct platform_suspend_ops rk30_pm_ops = {
	.enter		= rk30_pm_enter,
	.valid		= suspend_valid_only_mem,
	.prepare 	= rk30_pm_prepare,
	.finish		= rk30_pm_finish,
};

static int __init rk30_pm_init(void)
{
	suspend_set_ops(&rk30_pm_ops);

#ifdef CONFIG_EARLYSUSPEND
	pm_set_vt_switch(0); /* disable vt switch while suspend */
#endif

	return 0;
}
__initcall(rk30_pm_init);
