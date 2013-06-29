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
#include <mach/debug_uart.h>
#include <plat/efuse.h>
#include <plat/cpu.h>

#define cru_readl(offset)	readl_relaxed(RK30_CRU_BASE + offset)
#define cru_writel(v, offset)	do { writel_relaxed(v, RK30_CRU_BASE + offset); dsb(); } while (0)

#define pmu_readl(offset)	readl_relaxed(RK30_PMU_BASE + offset)
#define pmu_writel(v,offset)	do { writel_relaxed(v, RK30_PMU_BASE + offset); dsb(); } while (0)

#define grf_readl(offset)	readl_relaxed(RK30_GRF_BASE + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK30_GRF_BASE + offset); dsb(); } while (0)

#define gate_save_soc_clk(val,_save,cons,w_msk) \
	(_save)=cru_readl(cons);\
	cru_writel((((~(val)|(_save))&(w_msk))|((w_msk)<<16)),cons)


__weak void board_gpio_suspend(void) {}
__weak void board_gpio_resume(void) {}
__weak void __sramfunc board_pmu_suspend(void) {}
__weak void __sramfunc board_pmu_resume(void) {}
__weak void __sramfunc rk30_suspend_voltage_set(unsigned int vol){}
__weak void __sramfunc rk30_suspend_voltage_resume(unsigned int vol){}

__weak void  rk30_pwm_suspend_voltage_set(void){}
__weak void  rk30_pwm_resume_voltage_set(void){}
__weak void board_act8846_set_suspend_vol(void){}
__weak void board_act8846_set_resume_vol(void){}

__weak void __sramfunc rk30_pwm_logic_suspend_voltage(void){}
__weak void __sramfunc rk30_pwm_logic_resume_voltage(void){}

static int rk3188plus_soc = 0;

/********************************sram_printch**************************************************/
static bool __sramdata pm_log;
extern void pm_emit_log_char(char c);

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
#endif

	sram_log_char(byte);
	if (pm_log)
		pm_emit_log_char(byte);

	if (byte == '\n')
		sram_printch('\r');
}
/********************************ddr test**************************************************/

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
	if (ddr_debug == 0)
		return;
	
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


#define DUMP_GPIO_INT_STATUS(ID) \
do { \
	if (irq_gpio & (1 << ID)) \
		printk("wakeup gpio" #ID ": %08x\n", readl_relaxed(RK30_GPIO##ID##_BASE + GPIO_INT_STATUS)); \
} while (0)

static noinline void rk30_pm_dump_irq(void)
{
	u32 irq_gpio = (readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 8) >> 22) & 0x7F;
	printk("wakeup irq: %08x %08x %08x %08x\n",
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 4),
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 8),
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 12),
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 16));
	DUMP_GPIO_INT_STATUS(0);
	DUMP_GPIO_INT_STATUS(1);
	DUMP_GPIO_INT_STATUS(2);
	DUMP_GPIO_INT_STATUS(3);
#if !(defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188))
	DUMP_GPIO_INT_STATUS(4);
	DUMP_GPIO_INT_STATUS(6);
#endif
}

#define DUMP_GPIO_INTEN(ID) \
do { \
	u32 en = readl_relaxed(RK30_GPIO##ID##_BASE + GPIO_INTEN); \
	if (en) { \
		sram_printascii("GPIO" #ID "_INTEN: "); \
		sram_printhex(en); \
		sram_printch('\n'); \
		printk(KERN_DEBUG "GPIO%d_INTEN: %08x\n", ID, en); \
	} \
} while (0)

static noinline void rk30_pm_dump_inten(void)
{
	DUMP_GPIO_INTEN(0);
	DUMP_GPIO_INTEN(1);
	DUMP_GPIO_INTEN(2);
	DUMP_GPIO_INTEN(3);
#if !(defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188))
	DUMP_GPIO_INTEN(4);
	DUMP_GPIO_INTEN(6);
#endif
}

static void pm_pll_wait_lock(int pll_idx)
{
	u32 pll_state[4] = { 1, 0, 2, 3 };
#if defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188)

	u32 bit = 0x20u << pll_state[pll_idx];
#else
	u32 bit = 0x10u << pll_state[pll_idx];
#endif
	u32 delay = pll_idx == APLL_ID ? 600000U : 30000000U;
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
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

static void power_on_pll(enum rk_plls_id pll_id)
{
#if defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188)
	if (!rk3188plus_soc) {
		cru_writel(PLL_PWR_DN_W_MSK | PLL_PWR_ON, PLL_CONS((pll_id), 3));
		pm_pll_wait_lock((pll_id));
	} else {
		u32 pllcon0, pllcon1, pllcon2;
		cru_writel(PLL_PWR_DN_W_MSK | PLL_PWR_ON, PLL_CONS((pll_id),3));
		pllcon0 = cru_readl(PLL_CONS((pll_id),0));
		pllcon1 = cru_readl(PLL_CONS((pll_id),1));
		pllcon2 = cru_readl(PLL_CONS((pll_id),2));

		//enter slowmode
		cru_writel(PLL_MODE_SLOW(pll_id), CRU_MODE_CON);

		//enter rest
		cru_writel(PLL_RESET_W_MSK | PLL_RESET, PLL_CONS(pll_id,3));
		cru_writel(pllcon0, PLL_CONS(pll_id,0));
		cru_writel(pllcon1, PLL_CONS(pll_id,1));
		cru_writel(pllcon2, PLL_CONS(pll_id,2));
		if (pll_id == APLL_ID)
			sram_udelay(5);
		else
			udelay(5);

		//return form rest
		cru_writel(PLL_RESET_W_MSK | PLL_RESET_RESUME, PLL_CONS(pll_id,3));

		//wating lock state
		if (pll_id == APLL_ID)
			sram_udelay(168);
		else
			udelay(168);
		pm_pll_wait_lock(pll_id);

		//return form slow
		cru_writel(PLL_MODE_NORM(pll_id), CRU_MODE_CON);
	}
#else
	u32 pllcon0, pllcon1, pllcon2;

	cru_writel(PLL_PWR_DN_W_MSK | PLL_PWR_ON, PLL_CONS((pll_id),3));
	pllcon0 = cru_readl(PLL_CONS((pll_id),0));
	pllcon1 = cru_readl(PLL_CONS((pll_id),1));
	pllcon2 = cru_readl(PLL_CONS((pll_id),2));

	//enter slowmode
	cru_writel(PLL_MODE_SLOW(pll_id), CRU_MODE_CON);

	//enter rest
	cru_writel(PLL_REST_W_MSK | PLL_REST, PLL_CONS(pll_id,3));
	cru_writel(pllcon0, PLL_CONS(pll_id,0));
	cru_writel(pllcon1, PLL_CONS(pll_id,1));
	cru_writel(pllcon2, PLL_CONS(pll_id,2));
	if (pll_id == APLL_ID)
		sram_udelay(5);
	else
		udelay(5);

	//return form rest
	cru_writel(PLL_REST_W_MSK | PLL_REST_RESM, PLL_CONS(pll_id,3));

	//wating lock state
	if (pll_id == APLL_ID)
		sram_udelay(168);
	else
		udelay(168);
	pm_pll_wait_lock(pll_id);

	//return form slow
	cru_writel(PLL_MODE_NORM(pll_id), CRU_MODE_CON);
#endif
}

#define power_off_pll(id) \
	cru_writel(PLL_PWR_DN_W_MSK | PLL_PWR_DN, PLL_CONS((id), 3))


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
	readl_relaxed(RK30_I2C1_BASE+SZ_4K);
	readl_relaxed(RK30_GPIO0_BASE);
	readl_relaxed(RK30_GPIO3_BASE);
#if defined(RK30_GPIO6_BASE)
	readl_relaxed(RK30_GPIO6_BASE);
#endif
}


static inline bool pm_pmu_power_domain_is_on(enum pmu_power_domain pd, u32 pmu_pwrdn_st)
{
	return !(pmu_pwrdn_st & (1 << pd));
}
static void rk30_pm_set_power_domain(u32 pmu_pwrdn_st, bool state)
{
#if !(defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188))
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
#elif defined(CONFIG_ARCH_RK3188)
		u32 gate[2];
		gate[0] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU_SRC));
		gate[1] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU_SRC), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU_SRC));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU));
		pmu_set_power_domain(PD_GPU, state);
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU_SRC) | gate[0],
				CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU_SRC));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_GPU) | gate[1], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_GPU));
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
#if !defined(CONFIG_ARCH_RK3188)
		gate[2] = cru_readl(CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VCODEC));
#endif
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VEPU), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VEPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VDPU), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VDPU));
#if !defined(CONFIG_ARCH_RK3188)
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VCODEC), CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VCODEC));
#endif
		pmu_set_power_domain(PD_VIDEO, state);
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VEPU) | gate[0], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VEPU));
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VDPU) | gate[1], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VDPU));
#if !defined(CONFIG_ARCH_RK3188)
		cru_writel(CLK_GATE_W_MSK(CLK_GATE_ACLK_VCODEC) | gate[2], CLK_GATE_CLKID_CONS(CLK_GATE_ACLK_VCODEC));
#endif
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
static void __sramfunc rk_pm_soc_sram_volt_suspend(void)
{
	rk30_suspend_voltage_set(1000000);
	rk30_pwm_logic_suspend_voltage();
#ifdef CONFIG_ACT8846_SUPPORT_RESET
	board_act8846_set_suspend_vol();
#endif
}	

static void __sramfunc rk_pm_soc_sram_volt_resume(void)
{

	#ifdef CONFIG_ACT8846_SUPPORT_RESET
		board_act8846_set_resume_vol();
	#endif

	rk30_pwm_logic_resume_voltage();
	rk30_suspend_voltage_resume(1100000);

}

#define CLK_GATE_W_MSK0 (0xffff)
#define CLK_GATE_W_MSK1 (0xff9f)	//defult:(0xffff); ignore usb:(0xff9f) G1_[6:5]
#define CLK_GATE_W_MSK2 (0xffff)
#define CLK_GATE_W_MSK3 (0xff9f)	//defult:(0xff9f); ignore use:(0xff9f) G3_[6]
#define CLK_GATE_W_MSK4 (0xffff)
#define CLK_GATE_W_MSK5 (0xdfff)	//defult:(0xffff); ignore usb:(0xdfff) G5_[13]
#define CLK_GATE_W_MSK6 (0xffff)
#define CLK_GATE_W_MSK7 (0xffe7)	//defult:(0xffff); ignore usb:(0xffe7) G7_[4:3]
#define CLK_GATE_W_MSK8 (0x01ff)
#define CLK_GATE_W_MSK9 (0x07ff)
static u32 __sramdata clkgt_regs_sram[CRU_CLKGATES_CON_CNT];
static u32 __sramdata sram_grf_uoc0_con0_status;

static void __sramfunc rk_pm_soc_sram_clk_gating(void)
{
	int i;

	#if defined(CONFIG_ARCH_RK3188) && (CONFIG_RK_DEBUG_UART == 2)
	#ifdef CONFIG_RK_USB_UART
		sram_grf_uoc0_con0_status = grf_readl(GRF_UOC0_CON0);
		grf_writel(0x03000000, GRF_UOC0_CON0);
	#endif
	#endif

	
	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		clkgt_regs_sram[i] = cru_readl(CRU_CLKGATES_CON(i));
	}
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CORE_PERIPH)
			  | (1 << CLK_GATE_ACLK_CPU)
			  | (1 << CLK_GATE_HCLK_CPU)
			  | (1 << CLK_GATE_PCLK_CPU)
			  | (1 << CLK_GATE_ACLK_CORE)
			  , clkgt_regs_sram[0], CRU_CLKGATES_CON(0), CLK_GATE_W_MSK0);
	
	gate_save_soc_clk(0, clkgt_regs_sram[1], CRU_CLKGATES_CON(1), CLK_GATE_W_MSK1);
	
#if defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188)
	if(((clkgt_regs_sram[8] >> CLK_GATE_PCLK_GPIO3% 16) & 0x01) == 0x01){
#else
	if(((clkgt_regs_sram[8] >> CLK_GATE_PCLK_GPIO3% 16) & 0x03) == 0x03){
#endif
		gate_save_soc_clk(0
				, clkgt_regs_sram[2], CRU_CLKGATES_CON(2), CLK_GATE_W_MSK2);

	}else{
		gate_save_soc_clk(0
				  | (1 << CLK_GATE_PERIPH_SRC % 16)
				  | (1 << CLK_GATE_PCLK_PERIPH % 16)
				, clkgt_regs_sram[2], CRU_CLKGATES_CON(2), CLK_GATE_W_MSK2);
	}
	gate_save_soc_clk(0
		#if 1  //for uart befor wfi 
		| (1 << CLK_GATE_PCLK_PERI_AXI_MATRIX % 16)
		| (1 << CLK_GATE_ACLK_PERI_AXI_MATRIX % 16)
		| (1 << CLK_GATE_ACLK_PEI_NIU % 16)
		#endif	
		
		| (1 << CLK_GATE_ACLK_STRC_SYS % 16)
		| (1 << CLK_GATE_ACLK_INTMEM % 16)
		#if !defined(CONFIG_ARCH_RK3188)
		| (1 << CLK_GATE_HCLK_L2MEM % 16)
		#else
		| (1 << CLK_GATE_HCLK_IMEM1 % 16)
		| (1 << CLK_GATE_HCLK_IMEM0 % 16)
		#endif
		, clkgt_regs_sram[4], CRU_CLKGATES_CON(4), CLK_GATE_W_MSK4);

	gate_save_soc_clk(0
		  | (1 << CLK_GATE_PCLK_GRF % 16)
		  | (1 << CLK_GATE_PCLK_PMU % 16)
		  , clkgt_regs_sram[5], CRU_CLKGATES_CON(5), CLK_GATE_W_MSK5);
	
	gate_save_soc_clk(0, clkgt_regs_sram[7], CRU_CLKGATES_CON(7), CLK_GATE_W_MSK7);
	
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CLK_L2C % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM0 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM1 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM2 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM3 % 16)
			  , clkgt_regs_sram[9], CRU_CLKGATES_CON(9), CLK_GATE_W_MSK9);

}

static void __sramfunc rk_pm_soc_sram_clk_ungating(void)
{
	int i;
	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		cru_writel(clkgt_regs_sram[i] | 0xffff0000, CRU_CLKGATES_CON(i));
	}

	#if defined(CONFIG_ARCH_RK3188) && (CONFIG_RK_DEBUG_UART == 2)
	#ifdef CONFIG_RK_USB_UART
		grf_writel(0x03000000 | sram_grf_uoc0_con0_status, GRF_UOC0_CON0);
	#endif
	#endif
}

static u32 __sramdata sram_cru_clksel0_con, sram_cru_clksel10_con,sram_cru_mode_con;
static void __sramfunc rk_pm_soc_sram_sys_clk_suspend(void)
{
	sram_cru_clksel0_con = cru_readl(CRU_CLKSELS_CON(0));
#ifdef CONFIG_CLK_SWITCH_TO_32K
		sram_cru_mode_con = cru_readl(CRU_MODE_CON);
		sram_cru_clksel10_con = cru_readl(CRU_CLKSELS_CON(10));
		cru_writel(PERI_ACLK_DIV_W_MSK | PERI_ACLK_DIV(4), CRU_CLKSELS_CON(10));
		cru_writel(CORE_CLK_DIV_W_MSK | CORE_CLK_DIV(4) | CPU_CLK_DIV_W_MSK | CPU_CLK_DIV(4), CRU_CLKSELS_CON(0));
		cru_writel(0
			   | PLL_MODE_DEEP(APLL_ID)
			   | PLL_MODE_DEEP(DPLL_ID)
			   | PLL_MODE_DEEP(CPLL_ID)
			   | PLL_MODE_DEEP(GPLL_ID)
			   , CRU_MODE_CON);
		//board_pmu_suspend();
#else
		cru_writel(CORE_CLK_DIV_W_MSK | CORE_CLK_DIV_MSK | CPU_CLK_DIV_W_MSK | CPU_CLK_DIV_MSK, CRU_CLKSELS_CON(0));
#endif

}

static void __sramfunc  rk_pm_soc_sram_sys_clk_resume(void)
{

#ifdef CONFIG_CLK_SWITCH_TO_32K
	cru_writel((0xffff<<16) | sram_cru_mode_con, CRU_MODE_CON);
	cru_writel(CORE_CLK_DIV_W_MSK | CPU_CLK_DIV_W_MSK | sram_cru_clksel0_con, CRU_CLKSELS_CON(0));
	cru_writel(PERI_ACLK_DIV_W_MSK | sram_cru_clksel10_con, CRU_CLKSELS_CON(10));
#else
	cru_writel(CORE_CLK_DIV_W_MSK | CPU_CLK_DIV_W_MSK | sram_cru_clksel0_con, CRU_CLKSELS_CON(0));
#endif

}
/*********************************code is in ddr ******************************************/

static inline void rk_pm_soc_prepare(void)
{
	// dump GPIO INTEN for debug
	rk30_pm_dump_inten();
#if !(defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188))
	//gpio6_b7
	grf_writel(0xc0004000, 0x10c);
	cru_writel(0x07000000, CRU_MISC_CON);
#endif

#ifdef CONFIG_DDR_TEST
	// memory tester
		ddr_testmode();
#endif
}

static inline void rk_pm_soc_finsh(void)
{
	rk30_pm_dump_irq();
}


static u32 pmu_pwrdn_st;

static inline void rk_pm_soc_pd_suspend(void)
{
	pmu_pwrdn_st = pmu_readl(PMU_PWRDN_ST);
	rk30_pm_set_power_domain(pmu_pwrdn_st, false);
}

static inline void rk_pm_soc_pd_resume(void)
{
	rk30_pm_set_power_domain(pmu_pwrdn_st, true);
}


static u32 clkgt_regs_first[CRU_CLKGATES_CON_CNT];
static void rk_pm_soc_clk_gating_first(void)
{
	int i;

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		clkgt_regs_first[i] = cru_readl(CRU_CLKGATES_CON(i));
	}

	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CORE_PERIPH)
#if defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188)
			  | (1 << CLK_GATE_CPU_GPLL_PATH)
			  | (1 << CLK_GATE_ACLK_CORE)
#endif
			  | (1 << CLK_GATE_DDRPHY)
			  | (1 << CLK_GATE_ACLK_CPU)
			  | (1 << CLK_GATE_HCLK_CPU)
			  | (1 << CLK_GATE_PCLK_CPU)
			  , clkgt_regs_first[0], CRU_CLKGATES_CON(0), CLK_GATE_W_MSK0);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_DDR_GPLL % 16)
			  , clkgt_regs_first[1], CRU_CLKGATES_CON(1), CLK_GATE_W_MSK1);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PERIPH_SRC % 16)
			  | (1 << CLK_GATE_PCLK_PERIPH % 16)
			  | (1 << CLK_GATE_ACLK_PERIPH % 16)
			  , clkgt_regs_first[2], CRU_CLKGATES_CON(2),CLK_GATE_W_MSK2 );
	gate_save_soc_clk(0, clkgt_regs_first[3], CRU_CLKGATES_CON(3),CLK_GATE_W_MSK3);
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
#if !defined(CONFIG_ARCH_RK3188)
			  | (1 << CLK_GATE_HCLK_L2MEM % 16)
#else
			  | (1 << CLK_GATE_HCLK_IMEM1 % 16)
			  | (1 << CLK_GATE_HCLK_IMEM0 % 16)
#endif
			  , clkgt_regs_first[4], CRU_CLKGATES_CON(4), CLK_GATE_W_MSK4);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PCLK_GRF % 16)
			  | (1 << CLK_GATE_PCLK_PMU % 16)
			  | (1 << CLK_GATE_PCLK_DDRUPCTL % 16)
			  , clkgt_regs_first[5], CRU_CLKGATES_CON(5), CLK_GATE_W_MSK5);
	gate_save_soc_clk(0, clkgt_regs_first[6], CRU_CLKGATES_CON(6), CLK_GATE_W_MSK6);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_PCLK_PWM01 % 16)
			  | (1 << CLK_GATE_PCLK_PWM23 % 16)
			  , clkgt_regs_first[7], CRU_CLKGATES_CON(7),CLK_GATE_W_MSK7);
	gate_save_soc_clk(0 , clkgt_regs_first[8], CRU_CLKGATES_CON(8), CLK_GATE_W_MSK8);
	gate_save_soc_clk(0
			  | (1 << CLK_GATE_CLK_L2C % 16)
			  | (1 << CLK_GATE_PCLK_PUBL % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM0 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM1 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM2 % 16)
			  | (1 << CLK_GATE_ACLK_INTMEM3 % 16)
			  , clkgt_regs_first[9], CRU_CLKGATES_CON(9), CLK_GATE_W_MSK9);
}

static void rk_pm_soc_clk_ungating_first(void)
{
	int i;
	
	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		cru_writel(clkgt_regs_first[i] | 0xffff0000, CRU_CLKGATES_CON(i));
	}
}

static u32 clk_sel0, clk_sel1, clk_sel10;
static u32 cpll_con3;
static u32 cru_mode_con;
static void rk_pm_soc_pll_suspend(void)
{
	cru_mode_con = cru_readl(CRU_MODE_CON);
	
		//cpll
		if(rk_pll_flag()==0)
		{	
		cru_writel(PLL_MODE_SLOW(CPLL_ID), CRU_MODE_CON);
		cpll_con3 = cru_readl(PLL_CONS(CPLL_ID, 3));
		power_off_pll(CPLL_ID);
		}
	
		//apll
		clk_sel0 = cru_readl(CRU_CLKSELS_CON(0));
		clk_sel1 = cru_readl(CRU_CLKSELS_CON(1));
	
		cru_writel(PLL_MODE_SLOW(APLL_ID), CRU_MODE_CON);
		/* To make sure aclk_cpu select apll before div effect */
		cru_writel(CPU_SEL_PLL_W_MSK | CPU_SEL_APLL
                           | CORE_SEL_PLL_W_MSK | CORE_SEL_APLL
                           , CRU_CLKSELS_CON(0));
		cru_writel(CORE_PERIPH_W_MSK | CORE_PERIPH_2
			   | CORE_CLK_DIV_W_MSK | CORE_CLK_DIV(1)
			   | CPU_CLK_DIV_W_MSK | CPU_CLK_DIV(1)
			   , CRU_CLKSELS_CON(0));
		cru_writel(CORE_ACLK_W_MSK | CORE_ACLK_11
#if !defined(CONFIG_ARCH_RK3188)
			   | CPU_ACLK_W_MSK | CPU_ACLK_11
#endif
			   | ACLK_HCLK_W_MSK | ACLK_HCLK_11
			   | ACLK_PCLK_W_MSK | ACLK_PCLK_11
			   | AHB2APB_W_MSK | AHB2APB_11
			   , CRU_CLKSELS_CON(1));
		power_off_pll(APLL_ID);
	
		//gpll
		if(rk_pll_flag()==0)
		{
		cru_writel(PLL_MODE_SLOW(GPLL_ID), CRU_MODE_CON);
		clk_sel10 = cru_readl(CRU_CLKSELS_CON(10));
		cru_writel(CRU_W_MSK_SETBITS(0, PERI_ACLK_DIV_OFF, PERI_ACLK_DIV_MASK)
			   | CRU_W_MSK_SETBITS(0, PERI_HCLK_DIV_OFF, PERI_HCLK_DIV_MASK)
			   | CRU_W_MSK_SETBITS(0, PERI_PCLK_DIV_OFF, PERI_PCLK_DIV_MASK)
			   , CRU_CLKSELS_CON(10));
		power_off_pll(GPLL_ID);
		}

}

static void rk_pm_soc_pll_resume(void)
{
	
	//gpll
	if(rk_pll_flag()==0)
	{
	cru_writel(0xffff0000 | clk_sel10, CRU_CLKSELS_CON(10));
	power_on_pll(GPLL_ID);
	cru_writel((PLL_MODE_MSK(GPLL_ID) << 16) | (PLL_MODE_MSK(GPLL_ID) & cru_mode_con), CRU_MODE_CON);
	}
	//apll
	cru_writel(0xffff0000 | clk_sel1, CRU_CLKSELS_CON(1));
	/* To make sure aclk_cpu select gpll after div effect */
	cru_writel((0xffff0000 & ~CPU_SEL_PLL_W_MSK & ~CORE_SEL_PLL_W_MSK) | clk_sel0, CRU_CLKSELS_CON(0));
	cru_writel(CPU_SEL_PLL_W_MSK | CORE_SEL_PLL_W_MSK | clk_sel0, CRU_CLKSELS_CON(0));
	power_on_pll(APLL_ID);
	cru_writel((PLL_MODE_MSK(APLL_ID) << 16) | (PLL_MODE_MSK(APLL_ID) & cru_mode_con), CRU_MODE_CON);

	//cpll
	if(rk_pll_flag()==0)
	{	
	if (((cpll_con3 & PLL_PWR_DN_MSK) == PLL_PWR_ON) &&
		((PLL_MODE_NORM(CPLL_ID) & PLL_MODE_MSK(CPLL_ID)) == (cru_mode_con & PLL_MODE_MSK(CPLL_ID)))) {
		power_on_pll(CPLL_ID);
	}
	cru_writel((PLL_MODE_MSK(CPLL_ID) << 16) | (PLL_MODE_MSK(CPLL_ID) & cru_mode_con), CRU_MODE_CON);
	}


}

/*********************************pm control******************************************/

enum rk_soc_pm_ctr_flags_offset {

	RK_PM_CTR_NO_PD=0,
	RK_PM_CTR_NO_CLK_GATING,
	RK_PM_CTR_NO_PLL,
	RK_PM_CTR_NO_VOLT,
	RK_PM_CTR_NO_GPIO,
	RK_PM_CTR_NO_SRAM,
	RK_PM_CTR_NO_DDR,
	RK_PM_CTR_NO_SYS_CLK,
	RK_PM_CTR_NO_PMIC,

	RK_PM_CTR_RET_DIRT=24,
	RK_PM_CTR_SRAM_NO_WFI,
	RK_PM_CTR_WAKE_UP_KEY,
	RK_PM_CTR_ALL=31,
};

struct rk_soc_pm_info_st {
	int offset;
	char *name;
};

#define RK_SOC_PM_HELP_(id,NAME)\
	{\
	.offset= RK_PM_CTR_##id,\
	.name= NAME,\
	}

struct rk_soc_pm_info_st rk_soc_pm_helps[]={
	RK_SOC_PM_HELP_(NO_PD,"pd is not power dn"),
	RK_SOC_PM_HELP_(NO_CLK_GATING,"clk is not gating"),
	RK_SOC_PM_HELP_(NO_PLL,"pll is not power dn"),
	RK_SOC_PM_HELP_(NO_VOLT,"volt is not set suspend"),
	RK_SOC_PM_HELP_(NO_GPIO,"gpio is not control "),
	//RK_SOC_PM_HELP_(NO_SRAM,"not enter sram code"),
	RK_SOC_PM_HELP_(NO_DDR,"ddr is not reflash"),
	RK_SOC_PM_HELP_(NO_PMIC,"pmic is not suspend"),
	RK_SOC_PM_HELP_(RET_DIRT,"sys return from pm_enter directly"),
	RK_SOC_PM_HELP_(SRAM_NO_WFI,"sys is not runing wfi in sram"),
	RK_SOC_PM_HELP_(WAKE_UP_KEY,"send a power key to wake up lcd"),
};

ssize_t rk_soc_pm_helps_print(char *buf)
{
	char *s = buf;
	int i;

	for(i=0;i<ARRAY_SIZE(rk_soc_pm_helps);i++)
	{
		s += sprintf(s, "bit(%d): %s\n", rk_soc_pm_helps[i].offset,rk_soc_pm_helps[i].name);
	}

	return (s-buf);
}	

// pm enter return directly
#define RK_SUSPEND_RET_DIRT_BITS ((1<<RK_PM_CTR_RET_DIRT))
// not enter rk30_suspend
#define RK_NO_SUSPEND_CTR_BITS ((1<<RK_PM_CTR_NO_PLL))
//not running wfi in sram code
#define RK_SUSPEND_NO_SRAM_WFI_BITS ((1<<RK_PM_CTR_SRAM_NO_WFI))


static u32  __sramdata rk_soc_pm_ctr_flags_sram=0;
static u32   rk_soc_pm_ctr_flags=0;

static int __init early_param_rk_soc_pm_ctr(char *str)
{
	get_option(&str, &rk_soc_pm_ctr_flags);
	printk("early_param_rk_soc_pm_ctr=%x\n",rk_soc_pm_ctr_flags);
	return 0;
}

early_param("rk_soc_pm_ctr", early_param_rk_soc_pm_ctr);

void  rk_soc_pm_ctr_bits_set(u32 flags)
{	
	rk_soc_pm_ctr_flags = flags;
	
}
u32  rk_soc_pm_ctr_bits_get(void)
{	
	return rk_soc_pm_ctr_flags;
}

void  rk_soc_pm_ctr_bit_set(int offset)
{	
	rk_soc_pm_ctr_flags|=(1<<offset);
}
void inline rk_soc_pm_ctr_bit_clear(int offset)
{
	rk_soc_pm_ctr_flags&=~(1<<offset);
}

u32 inline rk_soc_pm_ctr_bits_check(int bits)
{
	return (rk_soc_pm_ctr_flags_sram&(bits));
}	

#define  RK_SOC_PM_CTR_FUN(ctr,fun) \
	if(!(rk_soc_pm_ctr_bits_check((1<<RK_PM_CTR_##ctr))))\
		(fun)()

void rk_soc_pm_ctr_bits_prepare(void)
{
	
	rk_soc_pm_ctr_flags_sram=rk_soc_pm_ctr_flags;
	if(rk_soc_pm_ctr_flags_sram&(1<<RK_PM_CTR_NO_PLL))
	{
		rk_soc_pm_ctr_flags_sram|=(1<<RK_PM_CTR_NO_VOLT);
	}
		
}	

static void __sramfunc rk30_sram_suspend(void)
{

	sram_printch('5');
	
	RK_SOC_PM_CTR_FUN(NO_DDR,ddr_suspend);
	sram_printch('6');
	RK_SOC_PM_CTR_FUN(NO_VOLT,rk_pm_soc_sram_volt_suspend);
	sram_printch('7');
	RK_SOC_PM_CTR_FUN(NO_CLK_GATING,rk_pm_soc_sram_clk_gating);
	RK_SOC_PM_CTR_FUN(NO_SYS_CLK,rk_pm_soc_sram_sys_clk_suspend);
	RK_SOC_PM_CTR_FUN(NO_PMIC,board_pmu_suspend);
	if(!rk_soc_pm_ctr_bits_check(RK_SUSPEND_NO_SRAM_WFI_BITS))
	{
		dsb();
		wfi();
	}

	RK_SOC_PM_CTR_FUN(NO_PMIC,board_pmu_resume);
	RK_SOC_PM_CTR_FUN(NO_SYS_CLK,rk_pm_soc_sram_sys_clk_resume);
	RK_SOC_PM_CTR_FUN(NO_CLK_GATING,rk_pm_soc_sram_clk_ungating);
	sram_printch('7');
	RK_SOC_PM_CTR_FUN(NO_VOLT,rk_pm_soc_sram_volt_resume);
	sram_printch('6');	
	RK_SOC_PM_CTR_FUN(NO_DDR,ddr_resume);
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
	rk_soc_pm_ctr_bits_prepare();

	if(rk_soc_pm_ctr_bits_check(RK_SUSPEND_RET_DIRT_BITS))
	{
		printk("%s return directly\n",__FUNCTION__);
		return 0;
	}
	
	rk_pm_soc_prepare();
	
	printk(KERN_DEBUG "pm: ");
	pm_log = true;
	sram_log_reset();
	sram_printch('0');
	
	RK_SOC_PM_CTR_FUN(NO_PD,rk_pm_soc_pd_suspend);

	sram_printch('1');
	local_fiq_disable();
	
	RK_SOC_PM_CTR_FUN(NO_CLK_GATING,rk_pm_soc_clk_gating_first);

	sram_printch('2');
	RK_SOC_PM_CTR_FUN(NO_PLL,rk_pm_soc_pll_suspend);

	sram_printch('3');

	RK_SOC_PM_CTR_FUN(NO_VOLT,rk30_pwm_suspend_voltage_set);
	RK_SOC_PM_CTR_FUN(NO_GPIO,board_gpio_suspend);


	interface_ctr_reg_pread();

	sram_printch('4');
	pm_log = false;
	
	if(!rk_soc_pm_ctr_bits_check(RK_NO_SUSPEND_CTR_BITS))
		rk30_suspend();
	
	
	pm_log = true;
	sram_printch('4');

	RK_SOC_PM_CTR_FUN(NO_GPIO,board_gpio_resume);
	RK_SOC_PM_CTR_FUN(NO_VOLT,rk30_pwm_resume_voltage_set);

	sram_printch('3');
	
	RK_SOC_PM_CTR_FUN(NO_PLL,rk_pm_soc_pll_resume);

	sram_printch('2');
	
	RK_SOC_PM_CTR_FUN(NO_CLK_GATING,rk_pm_soc_clk_ungating_first);
	
	local_fiq_enable();
	sram_printch('1');

	RK_SOC_PM_CTR_FUN(NO_PD,rk_pm_soc_pd_resume);
	
	sram_printch('0');
	pm_log = false;
	printk(KERN_CONT "\n");
	sram_printch('\n');
	rk_pm_soc_finsh();
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
#ifdef CONFIG_KEYS_RK29
	if(rk_soc_pm_ctr_bits_check(1<<RK_PM_CTR_WAKE_UP_KEY))
	{
		rk28_send_wakeup_key();
		printk("rk30_pm_finish rk28_send_wakeup_key\n");
	}
#endif
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
	rk3188plus_soc = soc_is_rk3188plus();
	return 0;
}
__initcall(rk30_pm_init);
