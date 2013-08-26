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
#include <mach/debug_uart.h>
#include <linux/regulator/machine.h>


#define cru_readl(offset)	readl_relaxed(RK2928_CRU_BASE + offset)
#define cru_writel(v, offset)	do { writel_relaxed(v, RK2928_CRU_BASE + offset); dsb(); } while (0)

#define grf_readl(offset)	readl_relaxed(RK2928_GRF_BASE + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK2928_GRF_BASE + offset); dsb(); } while (0)

#define gate_save_soc_clk(val, _save, cons, w_msk) \
	do { \
		(_save) = cru_readl(cons); \
		cru_writel(((~(val) | (_save)) & (w_msk)) | ((w_msk) << 16), cons); \
	} while (0)

#define  RK_SOC_PM_CTR_FUN(ctr,fun) \
	if(!(rk_soc_pm_ctr_bits_check((1<<RK_PM_CTR_##ctr))))\
(fun)()

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
/*********************************pm control******************************************/
enum rk_soc_pm_ctr_flags_offset {

	RK_PM_CTR_NO_PD = 0,
	RK_PM_CTR_NO_CLK_GATING,
	RK_PM_CTR_NO_PLL,
	RK_PM_CTR_NO_VOLT,
	RK_PM_CTR_NO_GPIO,
	RK_PM_CTR_NO_SRAM,
	RK_PM_CTR_NO_DDR,
	RK_PM_CTR_NO_SYS_CLK,
	RK_PM_CTR_NO_PMIC,

	RK_PM_CTR_RET_DIRT = 24,
	RK_PM_CTR_SRAM_NO_WFI,
	RK_PM_CTR_WAKE_UP_KEY,
	RK_PM_CTR_ALL = 31,
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

struct rk_soc_pm_info_st rk_soc_pm_helps[] = {
	RK_SOC_PM_HELP_(NO_PD, "pd is not power dn"),
	RK_SOC_PM_HELP_(NO_CLK_GATING, "clk is not gating"),
	RK_SOC_PM_HELP_(NO_PLL, "pll is not power dn"),
	RK_SOC_PM_HELP_(NO_VOLT, "volt is not set suspend"),
	RK_SOC_PM_HELP_(NO_GPIO, "gpio is not control "),
	//RK_SOC_PM_HELP_(NO_SRAM,"not enter sram code"),
	RK_SOC_PM_HELP_(NO_DDR, "ddr is not reflash"),
	RK_SOC_PM_HELP_(NO_PMIC, "pmic is not suspend"),
	RK_SOC_PM_HELP_(RET_DIRT, "sys return from pm_enter directly"),
	RK_SOC_PM_HELP_(SRAM_NO_WFI, "sys is not runing wfi in sram"),
	RK_SOC_PM_HELP_(WAKE_UP_KEY, "send a power key to wake up lcd"),
};

ssize_t rk_soc_pm_helps_sprintf(char *buf)
{
	char *s = buf;
	int i;

	for(i=0;i<ARRAY_SIZE(rk_soc_pm_helps);i++)
	{
		s += sprintf(s, "bit(%d): %s\n", rk_soc_pm_helps[i].offset,rk_soc_pm_helps[i].name);
	}

	return (s-buf);
}	

void rk_soc_pm_helps_printk(void)
{
	int i;
	printk("**************rk_suspend_ctr_bits bits help***********:\n");
	for(i=0;i<ARRAY_SIZE(rk_soc_pm_helps);i++)
	{
		printk("bit(%d): %s\n", rk_soc_pm_helps[i].offset,rk_soc_pm_helps[i].name);
	}
}	


// pm enter return directly
#define RK_SUSPEND_RET_DIRT_BITS ((1<<RK_PM_CTR_RET_DIRT))
// not enter rk30_suspend
#define RK_NO_SUSPEND_CTR_BITS ((1<<RK_PM_CTR_NO_PLL))
//not running wfi in sram code
#define RK_SUSPEND_NO_SRAM_WFI_BITS ((1<<RK_PM_CTR_SRAM_NO_WFI))


static u32  __sramdata rk_soc_pm_ctr_flags_sram = 0;
static u32  rk_soc_pm_ctr_flags = 0;

static int arm_suspend_volt = 0;
static int logic_suspend_volt = 0;


static int __init early_param_rk_soc_pm_ctr(char *str)
{
	get_option(&str, &rk_soc_pm_ctr_flags);
	
	printk("********rk_suspend_ctr_bits information is following:*********\n");
	printk("rk_suspend_ctr_bits=%x\n",rk_soc_pm_ctr_flags);
	if(rk_soc_pm_ctr_flags)
	{
		rk_soc_pm_helps_printk();
	}
	printk("********rk_suspend_ctr_bits information end*********\n");
	return 0;
}

early_param("rk_suspend_ctr_bits", early_param_rk_soc_pm_ctr);



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
	rk_soc_pm_ctr_flags |= (1 << offset);
}
void inline rk_soc_pm_ctr_bit_clear(int offset)
{
	rk_soc_pm_ctr_flags &= ~(1 << offset);
}

u32 inline rk_soc_pm_ctr_bits_check(int bits)
{
	return (rk_soc_pm_ctr_flags_sram & (bits));
}

#define  RK_SOC_PM_CTR_FUN(ctr,fun) \
	if(!(rk_soc_pm_ctr_bits_check((1<<RK_PM_CTR_##ctr))))\
(fun)()

void rk_soc_pm_ctr_bits_prepare(void)
{
	rk_soc_pm_ctr_flags_sram = rk_soc_pm_ctr_flags;

	if (rk_soc_pm_ctr_flags_sram & (1 << RK_PM_CTR_NO_PLL)) {
		rk_soc_pm_ctr_flags_sram |= (1 << RK_PM_CTR_NO_VOLT);
	}
}



	
static int __init set_arm_suspend_volt(char *str)
{
	get_option(&str, &arm_suspend_volt);
	printk("rk_suspend_arm_volt=%dmV\n", arm_suspend_volt);
	return 0;
}
early_param("rk_suspend_arm_volt", set_arm_suspend_volt);

static int __init set_logic_suspend_volt(char *str)
{
	get_option(&str, &logic_suspend_volt);
	printk("rk_suspend_logic_volt=%dmV\n", logic_suspend_volt);
	return 0;
}
early_param("rk_suspend_logic_volt", set_logic_suspend_volt);



static int __init pm_suspend_volt_seting(void)
{
	struct regulator *regulator;	

	printk("pmic set pm_suspend_volt:\n");
	if (arm_suspend_volt){
		regulator = regulator_get(NULL, "vdd_cpu");
		if (IS_ERR(regulator)){
			printk("%s:get vdd_cpu regulator err\n", __func__);
			return 0;
		}
		regulator_set_suspend_voltage(regulator, arm_suspend_volt);
		regulator_put(regulator);
	}

	if (logic_suspend_volt){
		regulator = regulator_get(NULL, "vdd_core");
		if (IS_ERR(regulator)){
			printk("%s:get vdd_core regulator err\n", __func__);
			return 0;
		}
		regulator_set_suspend_voltage(regulator, logic_suspend_volt);	
		regulator_put(regulator);
	}
	return 0;
}

device_initcall_sync(pm_suspend_volt_seting);


/*********************************pm main function******************************************/
__weak void __sramfunc ddr_suspend(void) {}
__weak void __sramfunc ddr_resume(void) {}
__weak uint32_t __sramfunc ddr_change_freq(uint32_t nMHz)
{
	return nMHz;
}

#ifdef CONFIG_DDR_TEST
static int ddr_debug = 0;
module_param(ddr_debug, int, 0644);

static int inline calc_crc32(u32 addr, size_t len)
{
	return crc32_le(~0, (const unsigned char *) addr, len);
}

static void ddr_testmode(void)
{
	int32_t g_crc1, g_crc2;
	uint32_t nMHz;
	uint32_t n = 0;
	uint32_t min, max;
	extern char _stext[], _etext[];


	if (ddr_debug == 1) {
		max = 500;
		min = 100;

		for (;;) {
			sram_printascii("\n change freq:");
			g_crc1 = calc_crc32((u32) _stext, (size_t)(_etext - _stext));

			do {
				nMHz = min + random32();
				nMHz %= max;
			} while (nMHz < min);

			sram_printhex(nMHz);
			sram_printch(' ');
			nMHz = ddr_change_freq(nMHz);
			sram_printhex(n++);
			sram_printch(' ');
			g_crc2 = calc_crc32((u32) _stext, (size_t)(_etext - _stext));

			if (g_crc1 != g_crc2) {
				sram_printascii("fail\n");
			}

			//ddr_print("check image crc32 success--crc value = 0x%x!, count:%d\n",g_crc1, n++);
			//     sram_printascii("change freq success\n");
		}
	} else if (ddr_debug == 2) {
		for (;;) {
			sram_printch(' ');
			sram_printch('9');
			sram_printch('9');
			sram_printch('9');
			sram_printch(' ');
			g_crc1 = calc_crc32((u32) _stext, (size_t)(_etext - _stext));
			nMHz = (random32() >> 13); // 16.7s max
			ddr_suspend();
			sram_udelay(nMHz);
			ddr_resume();
			sram_printhex(nMHz);
			sram_printch(' ');
			sram_printhex(n++);
			g_crc2 = calc_crc32((u32) _stext, (size_t)(_etext - _stext));

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
	} else {
		ddr_change_freq(ddr_debug);
		ddr_debug = 0;
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

		while (1);
	}
}

#define power_on_pll(id) \
	cru_writel(PLL_PWR_DN_W_MSK|PLL_PWR_ON,PLL_CONS((id),3));\
pm_pll_wait_lock((id))

static int pm_pll_pwr_up(u8 pll_id)
{
	//enter slowmode
	cru_writel(PLL_MODE_SLOW(pll_id), CRU_MODE_CON);
	cru_writel(CRU_W_MSK(PLL_PWR_DN_SHIFT, 0x01), PLL_CONS(pll_id, 1));

	sram_udelay(100);

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

	for (addr = (u32) SRAM_CODE_OFFSET; addr < (u32) SRAM_DATA_END; addr += PAGE_SIZE)
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

#define CLK_GATEALL_W_MSK	(0xffff)
#define PM_HOLDGATE(ID)		(1 << (CLK_GATE_##ID % 16))
#define PM_GATING(msk, con)	do {cru_writel((msk << 16) | 0xffff, con); } while(0)
static u16 clkgt_first_w_msk[CRU_CLKGATES_CON_CNT] = {
	~(0 | PM_HOLDGATE(CORE_PERIPH)
			| PM_HOLDGATE(CPU_GPLL)
			| PM_HOLDGATE(DDRPHY_SRC)
			| PM_HOLDGATE(ACLK_CPU)
			| PM_HOLDGATE(HCLK_CPU)
			| PM_HOLDGATE(PCLK_CPU)
			| PM_HOLDGATE(ACLK_CORE)),
	~(0),
	~(0 | PM_HOLDGATE(PERIPH_SRC)
			| PM_HOLDGATE(PCLK_PERIPH)
			| PM_HOLDGATE(ACLK_PERIPH)),
	~(0),
	~(0 | PM_HOLDGATE(HCLK_PERI_AXI_MATRIX)
			| PM_HOLDGATE(PCLK_PERI_AXI_MATRIX)
			| PM_HOLDGATE(ACLK_PERI_AXI_MATRIX)
			| PM_HOLDGATE(ACLK_CPU_PERI)
			| PM_HOLDGATE(ACLK_STRC_SYS)
			| PM_HOLDGATE(ACLK_INTMEM)),
	~(0 | PM_HOLDGATE(PCLK_GRF)
			| PM_HOLDGATE(PCLK_DDRUPCTL)),
	~(0),
	~(0 | PM_HOLDGATE(PCLK_PWM01)),
	~(0 | PM_HOLDGATE(PCLK_GPIO0)
			| PM_HOLDGATE(PCLK_GPIO1)
			| PM_HOLDGATE(PCLK_GPIO2)
			| PM_HOLDGATE(PCLK_GPIO3)),
	~(0 | PM_HOLDGATE(CLK_L2C)
			| PM_HOLDGATE(HCLK_PERI_ARBI)
			| PM_HOLDGATE(ACLK_PERI_NIU)),
};
static u16 __sramdata clkgt_sram_w_msk[CRU_CLKGATES_CON_CNT] = {
	~(0 | PM_HOLDGATE(CORE_PERIPH)
			| PM_HOLDGATE(DDRPHY_SRC)
			| PM_HOLDGATE(ACLK_CPU)
			| PM_HOLDGATE(HCLK_CPU)
			| PM_HOLDGATE(PCLK_CPU)
			| PM_HOLDGATE(ACLK_CORE)),
	~(0),
	~(0 | PM_HOLDGATE(PERIPH_SRC)
			| PM_HOLDGATE(PCLK_PERIPH)),
	~(0),
	~(0 | PM_HOLDGATE(ACLK_STRC_SYS)
			| PM_HOLDGATE(ACLK_INTMEM)),
	~(0 | PM_HOLDGATE(PCLK_GRF)
			| PM_HOLDGATE(PCLK_DDRUPCTL)),
	~(0),
	~(0),
	~(0 | PM_HOLDGATE(PCLK_GPIO0)
			| PM_HOLDGATE(PCLK_GPIO1)
			| PM_HOLDGATE(PCLK_GPIO2)
			| PM_HOLDGATE(PCLK_GPIO3)),
	~(0 | PM_HOLDGATE(CLK_L2C)),
};
static u32 __sramdata clkgt_regs_sram[CRU_CLKGATES_CON_CNT];
static u32 __sramdata grf_uoc1_con;
static void __sramfunc rk_pm_soc_sram_volt_suspend(void)
{
	rk30_suspend_voltage_set(1000000);
	rk30_pwm_logic_suspend_voltage();
	board_pmu_suspend();
}

static void __sramfunc rk_pm_soc_sram_clk_gating(void)
{
	int i;
#ifdef CONFIG_ARCH_RK2928
	grf_uoc1_con = grf_readl(GRF_UOC1_CON4);
	grf_writel(0x30000000, GRF_UOC1_CON4);
#endif

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		clkgt_regs_sram[i] = cru_readl(CRU_CLKGATES_CON(i));
		PM_GATING(clkgt_sram_w_msk[i], CRU_CLKGATES_CON(i));
	}

	if (((clkgt_regs_sram[8] >> (CLK_GATE_PCLK_GPIO0 % 16)) & 0xf) == 0xf) {
		PM_GATING(CLK_GATEALL_W_MSK, CRU_CLKGATES_CON(2));
	}
}

static u32 __sramdata sram_cru_clksel0_con;
static void __sramfunc rk_pm_soc_sram_sys_clk_suspend(void)
{
	sram_cru_clksel0_con = cru_readl(CRU_CLKSELS_CON(0));
	cru_writel(CLK_CORE_DIV(32), CRU_CLKSELS_CON(0));

}
static void __sramfunc rk_pm_soc_sram_sys_clk_resume(void)
{

	cru_writel((0x1f << 16) | sram_cru_clksel0_con, CRU_CLKSELS_CON(0));
	//cru_writel((A9_CORE_DIV_MASK << (A9_CORE_DIV_SHIFT + 16)) | sram_cru_clksel0_con, CRU_CLKSELS_CON(0));

}
static void __sramfunc rk_pm_soc_sram_clk_ungating(void)
{
	int i;

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		cru_writel(clkgt_regs_sram[i] | 0xffff0000, CRU_CLKGATES_CON(i));
	}

#ifdef CONFIG_ARCH_RK2928
	grf_writel(0x30000000 | grf_uoc1_con, GRF_UOC1_CON4);
#endif
}

static void __sramfunc rk_pm_soc_sram_volt_resume(void)
{
	board_pmu_resume();
	rk30_pwm_logic_resume_voltage();
	rk30_suspend_voltage_resume(1100000);
}
static void __sramfunc rk2928_sram_suspend(void)
{
	int grf_uoc1_con0;
	sram_printch('5');
	RK_SOC_PM_CTR_FUN(NO_DDR, ddr_suspend);
	sram_printch('6');
	RK_SOC_PM_CTR_FUN(NO_VOLT, rk_pm_soc_sram_volt_suspend);
	sram_printch('7');
#ifdef CONFIG_ARCH_RK3026
       grf_uoc1_con0 = grf_readl(GRF_UOC1_CON0);
       grf_writel(0x30000000, GRF_UOC1_CON0);
#endif
	
	RK_SOC_PM_CTR_FUN(NO_CLK_GATING, rk_pm_soc_sram_clk_gating);
	RK_SOC_PM_CTR_FUN(NO_SYS_CLK, rk_pm_soc_sram_sys_clk_suspend);

	//RK_SOC_PM_CTR_FUN(NO_PMIC,board_pmu_suspend);
	if (!rk_soc_pm_ctr_bits_check(RK_SUSPEND_NO_SRAM_WFI_BITS)) {
		dsb();
		wfi();
	}

	//RK_SOC_PM_CTR_FUN(NO_PMIC,board_pmu_resume);
	RK_SOC_PM_CTR_FUN(NO_SYS_CLK, rk_pm_soc_sram_sys_clk_resume);
	RK_SOC_PM_CTR_FUN(NO_CLK_GATING, rk_pm_soc_sram_clk_ungating);
#ifdef CONFIG_ARCH_RK3026
       grf_writel(0x30000000 | grf_uoc1_con0, GRF_UOC1_CON0);
#endif	
	sram_printch('7');
	RK_SOC_PM_CTR_FUN(NO_VOLT, rk_pm_soc_sram_volt_resume);
	sram_printch('6');
	RK_SOC_PM_CTR_FUN(NO_DDR, ddr_resume);
	sram_printch('5');
}
static void noinline rk2928_suspend(void)
{
	DDR_SAVE_SP(save_sp);
	rk2928_sram_suspend();
	DDR_RESTORE_SP(save_sp);
}
static u32 clk_sel0, clk_sel1, clk_sel10;
static u32 cru_mode_con;
static u32 apll_con1, cpll_con1, gpll_con1;
static u32 clkgt_regs_first[CRU_CLKGATES_CON_CNT];
static void rk_pm_soc_clk_gating_first(void)
{
	int i;

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		clkgt_regs_first[i] = cru_readl(CRU_CLKGATES_CON(i));
		PM_GATING(clkgt_first_w_msk[i], CRU_CLKGATES_CON(i));
	}

	cru_writel(((1 << CLK_GATE_PCLK_GPIO0 % 16) << 16),  CRU_CLKGATES_CON(8));
}

static void rk_pm_soc_pll_suspend(void)
{
	cru_mode_con = cru_readl(CRU_MODE_CON);

	//apll
	clk_sel0 = cru_readl(CRU_CLKSELS_CON(0));
	clk_sel1 = cru_readl(CRU_CLKSELS_CON(1));
	apll_con1 = cru_readl(PLL_CONS(APLL_ID, 1));

	cru_writel(PLL_MODE_SLOW(APLL_ID), CRU_MODE_CON);
	cru_writel(CLK_CORE_DIV(1) | ACLK_CPU_DIV(1) | CPU_SEL_PLL(SEL_APLL), CRU_CLKSELS_CON(0));
	cru_writel(CLK_CORE_PERI_DIV(1) | ACLK_CORE_DIV(1) | HCLK_CPU_DIV(1) | PCLK_CPU_DIV(1), CRU_CLKSELS_CON(1));
	//cru_writel((0x01 <<(PLL_PWR_DN_SHIFT + 16))|(1<<PLL_PWR_DN_SHIFT), PLL_CONS(APLL_ID, 1));
	cru_writel(CRU_W_MSK_SETBIT(0x01, PLL_PWR_DN_SHIFT) , PLL_CONS(APLL_ID, 1));     //power down apll

	//cpll
	cpll_con1 = cru_readl(PLL_CONS(CPLL_ID, 1));

	cru_writel(PLL_MODE_SLOW(CPLL_ID), CRU_MODE_CON);
	cru_writel(CRU_W_MSK_SETBIT(0x01, PLL_PWR_DN_SHIFT), PLL_CONS(CPLL_ID, 1));     //power down cpll

	//gpll
	clk_sel10 = cru_readl(CRU_CLKSELS_CON(10));
	gpll_con1 = cru_readl(PLL_CONS(GPLL_ID, 1));

	cru_writel(PLL_MODE_SLOW(GPLL_ID), CRU_MODE_CON);
	cru_writel(PERI_SET_ACLK_DIV(1)
			| CRU_W_MSK_SETBITS(0, PERI_PCLK_DIV_SHIFT, PERI_PCLK_DIV_MASK)
			| CRU_W_MSK_SETBITS(0, PERI_HCLK_DIV_SHIFT, PERI_HCLK_DIV_MASK)
			, CRU_CLKSELS_CON(10));
	cru_writel(CRU_W_MSK_SETBIT(0x01, PLL_PWR_DN_SHIFT), PLL_CONS(GPLL_ID, 1));     //power down gpll

}

static void rk_pm_soc_pll_resume(void)
{

	//gpll
	pm_pll_pwr_up(GPLL_ID);
	cru_writel(0xffff0000 | clk_sel10, CRU_CLKSELS_CON(10));
	cru_writel(clk_sel10, CRU_CLKSELS_CON(10));
	cru_writel((PLL_MODE_MSK(GPLL_ID) << 16) | (PLL_MODE_MSK(GPLL_ID) & cru_mode_con), CRU_MODE_CON);

	//cpll
	if (!(cpll_con1 & (0x1 << PLL_PWR_DN_SHIFT)))
		pm_pll_pwr_up(CPLL_ID);

	cru_writel((PLL_MODE_MSK(CPLL_ID) << 16) | (PLL_MODE_MSK(CPLL_ID) & cru_mode_con), CRU_MODE_CON);

	//apll
	pm_pll_pwr_up(APLL_ID);
	cru_writel(0xffff0000 | clk_sel1, CRU_CLKSELS_CON(1));
	cru_writel(0xffff0000 | clk_sel0, CRU_CLKSELS_CON(0));
	cru_writel((PLL_MODE_MSK(APLL_ID) << 16) | (PLL_MODE_MSK(APLL_ID) & cru_mode_con), CRU_MODE_CON);


}
static void rk_pm_soc_clk_ungating_first(void)
{
	int i;

	for (i = 0; i < CRU_CLKGATES_CON_CNT; i++) {
		cru_writel(clkgt_regs_first[i] | 0xffff0000, CRU_CLKGATES_CON(i));
	}
}
static int rk2928_pm_enter(suspend_state_t state)
{
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
	RK_SOC_PM_CTR_FUN(NO_CLK_GATING, rk_pm_soc_clk_gating_first);

	sram_printch('2');
	RK_SOC_PM_CTR_FUN(NO_PLL, rk_pm_soc_pll_suspend);

	sram_printch('3');
	//RK_SOC_PM_CTR_FUN(NO_VOLT,rk30_pwm_suspend_voltage_set);
	RK_SOC_PM_CTR_FUN(NO_GPIO, board_gpio_suspend);

	interface_ctr_reg_pread();

	sram_printch('4');

	if (!rk_soc_pm_ctr_bits_check(RK_NO_SUSPEND_CTR_BITS))
		rk2928_suspend();

	sram_printch('4');
	RK_SOC_PM_CTR_FUN(NO_GPIO, board_gpio_resume);
	//RK_SOC_PM_CTR_FUN(NO_VOLT,rk30_pwm_resume_voltage_set);

	sram_printch('3');
	RK_SOC_PM_CTR_FUN(NO_PLL, rk_pm_soc_pll_resume);

	sram_printch('2');
	RK_SOC_PM_CTR_FUN(NO_CLK_GATING, rk_pm_soc_clk_ungating_first);

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
	pm_set_vt_switch(0);  /* disable vt switch while suspend */
#endif

	return 0;
}
__initcall(rk2928_pm_init);
