#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <asm/tcm.h>
#include <mach/rk2818_iomap.h>
#include <mach/scu.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/rk2818_pm.h>
#include <linux/regulator/driver.h>

extern void rockchip_timer_clocksource_suspend_resume(int suspend);
extern int rockchip_timer_clocksource_irq_checkandclear(void);

#ifdef CONFIG_DWC_OTG_HOST_ONLY
static int rk28_usb_suspend(int exitsuspend) { return 0; }
static int rk28_usb_check_vbus_change(void) { return 0; }
#else
extern int rk28_usb_suspend(int exitsuspend);
extern int rk28_usb_check_vbus_change(void);
#endif
#ifdef CONFIG_DWC_OTG_BOTH_HOST_SLAVE
extern int rk28_usb_check_connectid_change(void);
#else
static int rk28_usb_check_connectid_change(void) { return 0; }
#endif

#define regfile_readl(offset)	readl(RK2818_REGFILE_BASE + offset)

#define scu_readl(offset)	readl(RK2818_SCU_BASE + offset)
#define scu_writel(v, offset)	writel(v, RK2818_SCU_BASE + offset)

#define msdr_readl(offset)	readl(RK2818_SDRAMC_BASE + offset)
#define msdr_writel(v, offset)	writel(v, RK2818_SDRAMC_BASE + offset)

#define ddr_readl(offset)	readl(RK2818_SDRAMC_BASE + offset)
#define ddr_writel(v, offset)	writel(v, RK2818_SDRAMC_BASE + offset)

#define irq_readl(offset)	readl(RK2818_INTC_BASE + offset)
#define gpio0_readl(offset)	readl(RK2818_GPIO0_BASE + offset)
#define gpio1_readl(offset)	readl(RK2818_GPIO1_BASE + offset)

#define PLL_PD		(0x01u<<22)

/* CPU_APB_REG0 */
#define MEMTYPESHIFT	11
#define MEMTYPEMASK	(0x3 << MEMTYPESHIFT)

#define SDRAM		0
#define MOBILE_SDRAM	1
#define DDRII		2
#define MOBILE_DDR	3

static inline u32 sdram_get_mem_type(void)
{
	return (regfile_readl(CPU_APB_REG0) & MEMTYPEMASK) >> MEMTYPESHIFT;
}

#define ASM_LOOP_INSTRUCTION_NUM     4

/*static noinline void __tcmlocalfunc tcm_udelay(unsigned long usecs, u32 arm_freq_mhz)
{
	volatile unsigned int cycle;

	cycle = usecs * arm_freq_mhz / ASM_LOOP_INSTRUCTION_NUM;

	while (cycle--) {}
}*/

#define CLK_GATE_SDRAM_MASK		((1 << (CLK_GATE_SDRAM_COMMON & 31)) | (1 << (CLK_GATE_SDRAM_CONTROLLER & 31)))
#define CLK_GATE_MOBILESDRAM_MASK	((1 << (CLK_GATE_SDRAM_COMMON & 31)) | (1 << (CLK_GATE_MOBILE_SDRAM_CONTROLLER & 31)))

/* SDRAM Control Regitster */
#define SR_MODE            (1 << 11)
#define ENTER_SELF_REFRESH (1 << 1)
#define MSDR_SCTLR         0x0C // SDRAM control register

/* CTR_REG_62 */
#define MODE5_MASK         (0xFFFF << 16)
#define MODE5_CNT(n)       (((n) & 0xFFFF) << 16)
#define CTRL_REG_62        0xf8  // LOWPOWER_INTERNAL_CNT/LOWPOWER_EXTERNAL_CNT.

/****************************************************************/
//函数匿sdram_enter_self_refresh
//描述: SDRAM进入自刷新模帿
//参数说明:
//返回伿对于DDR就是CTRL_REG_62的值，供sdram_exit_self_refresh使用
//相关全局变量:
//注意:(1)系统完全idle后才能进入自刷新模式，进入自刷新后不能再访问SDRAM
//     (2)要进入自刷新模式，必须保证运行时这个函数所调用到的所有代码不在SDRAM
/****************************************************************/
u32 __tcmfunc sdram_enter_self_refresh(void)
{
	u32 r;
	u32 mem_type = sdram_get_mem_type();

	switch (mem_type) {
	case SDRAM:
	case MOBILE_SDRAM:
		msdr_writel(msdr_readl(MSDR_SCTLR) | ENTER_SELF_REFRESH, MSDR_SCTLR);

		while (!(msdr_readl(MSDR_SCTLR) & SR_MODE));  //确定已经进入self-refresh

		/* Disable Mobile SDRAM/SDRAM Common/Controller hclk clock */
		r = scu_readl(SCU_CLKGATE1_CON);
		if (SDRAM == mem_type) {
			r |= CLK_GATE_SDRAM_MASK;
		} else {
			r |= CLK_GATE_MOBILESDRAM_MASK;
		}
		scu_writel(r, SCU_CLKGATE1_CON);
		break;

	case DDRII:
	case MOBILE_DDR:
		r = ddr_readl(CTRL_REG_62);
		ddr_writel((r & ~MODE5_MASK) | MODE5_CNT(1), CTRL_REG_62);
		//FIXME: 等待进入self-refresh
		break;
	}

	return r;
}

/****************************************************************/
//函数匿sdram_exit_self_refresh
//描述: SDRAM退出自刷新模式
//参数说明:
//返回伿
//相关全局变量:
//注意:(1)SDRAM在自刷新模式后不能被访问，必须先退出自刷新模式
//     (2)必须保证运行时这个函数的代码不在SDRAM
/****************************************************************/
void __tcmfunc sdram_exit_self_refresh(u32 ctrl_reg_62)
{
	u32 r;
	u32 mem_type = sdram_get_mem_type();

	switch (mem_type) {
	case SDRAM:
	case MOBILE_SDRAM:
		/* Enable Mobile SDRAM/SDRAM Common/Controller hclk clock */
		r = scu_readl(SCU_CLKGATE1_CON);
		if (SDRAM == mem_type) {
			r &= ~CLK_GATE_SDRAM_MASK;
		} else {
			r &= ~CLK_GATE_MOBILESDRAM_MASK;
		}
		scu_writel(r, SCU_CLKGATE1_CON);
		tcm_udelay(1, 24); // DRVDelayUs(1);

		msdr_writel(msdr_readl(MSDR_SCTLR) & ~ENTER_SELF_REFRESH, MSDR_SCTLR);

		while (msdr_readl(MSDR_SCTLR) & SR_MODE);  //确定退出进入self-refresh
		break;

	case DDRII:
	case MOBILE_DDR:
		ddr_writel(ctrl_reg_62, CTRL_REG_62);
		break;
	}

	tcm_udelay(100, 24); //DRVDelayUs(100); 延时一下比较安全，保证退出后稳定
}

static void __tcmlocalfunc noinline tcm_printch(char byte)
{
	unsigned int timeout;

	timeout = 0xffffffff;
	while (!(readl(RK2818_UART1_BASE + 0x7c) & (1<<1))) {
		if (!timeout--) 
			return;
	}
	writel(byte, RK2818_UART1_BASE);
	if (byte == '\n')
		tcm_printch('\r');
}

static void __tcmlocalfunc noinline tcm_printascii(const char *s)
{
	while (*s) {
		tcm_printch(*s);
		s++;
	}
}

static void __tcmlocalfunc noinline tcm_printhex(unsigned int hex)
{
	int i = 8;
	tcm_printch('0');
	tcm_printch('x');
	while (i--) {
		unsigned char c = (hex & 0xF0000000) >> 28;
		tcm_printch(c < 0xa ? c + '0' : c - 0xa + 'a');
		hex <<= 4;
	}
}

//#define RK2818_MOBLIE_PM_CON
#ifdef RK2818_MOBLIE_PM_CON

#define RK2818_MOBLIE_PM_PRT_ORIGINAL_REG
//#define RK2818_MOBLIE_PM_PRT_CHANGED_REG

#define PM_SAVE_REG_NUM 4
struct rk2818_pm_st{
unsigned int *pm_scu_reg;
unsigned int *pm_cpu_reg;
unsigned int *pm_gpio0_reg;
unsigned int *pm_gpio1_reg;
unsigned int *savereg;

unsigned int *pm_scu_reg_ch;
unsigned int *pm_cpu_reg_ch;
unsigned int *pm_gpio0_reg_ch;
unsigned int *pm_gpio1_reg_ch;
unsigned int *save_ch;

u16 scu_regbit;
u16 cpu_regbit;
u16 gpio0_regbit;
u16 gpio1_regbit;
};
unsigned int __tcmdata pm_scu_reg_save[PM_SCU_REG_NUM];
unsigned int __tcmdata pm_cpu_reg_save[PM_GENERAL_CPU_REG];
unsigned int __tcmdata pm_gpio0_reg_save[PM_SCU_GPIO_SWPORTC_NUM];
unsigned int __tcmdata pm_gpio1_reg_save[PM_SCU_GPIO_SWPORTC_NUM];
unsigned int __tcmdata pm_savereg[PM_SAVE_REG_NUM];

unsigned int __tcmdata pm_scu_reg_ch[PM_SCU_REG_NUM];
unsigned int __tcmdata pm_cpu_reg_ch[PM_GENERAL_CPU_REG];
unsigned int __tcmdata pm_gpio0_reg_save_ch[PM_SCU_GPIO_SWPORTC_NUM];
unsigned int __tcmdata pm_gpio1_reg_save_ch[PM_SCU_GPIO_SWPORTC_NUM];
unsigned int __tcmdata savereg_ch[PM_SAVE_REG_NUM];

struct rk2818_pm_st __tcmdata pm_save={
.pm_scu_reg=&pm_scu_reg_save[0],
.pm_cpu_reg=&pm_cpu_reg_save[0],
.pm_gpio0_reg=&pm_gpio0_reg_save[0],
.pm_gpio1_reg=&pm_gpio1_reg_save[0],
.savereg=&pm_savereg[0],

.pm_scu_reg_ch=&pm_scu_reg_ch[0],
.pm_cpu_reg_ch=&pm_cpu_reg_ch[0],
.pm_gpio0_reg_ch=&pm_gpio0_reg_save_ch[0],
.pm_gpio1_reg_ch=&pm_gpio1_reg_save_ch[0],
.save_ch=&savereg_ch[0],
};

#if 0
#define rk2818_define_value \
unsigned int pm_scu_reg_save[PM_SCU_REG_NUM];\
unsigned int pm_cpu_reg_save[PM_GENERAL_CPU_REG];\
unsigned int pm_gpio0_reg_save[PM_SCU_GPIO_SWPORTC_NUM];\
unsigned int pm_gpio1_reg_save[PM_SCU_GPIO_SWPORTC_NUM];\
unsigned int save[4];\
unsigned int pm_scu_reg_ch[PM_SCU_REG_NUM];\
unsigned int pm_cpu_reg_ch[PM_GENERAL_CPU_REG];\
struct rk2818_pm_st pm_save={&pm_scu_reg_save[0],&pm_cpu_reg_save[0],&pm_gpio0_reg_save[0],&pm_gpio1_reg_save[0],&save[0]\
,&pm_scu_reg_ch[0],&pm_cpu_reg_ch[0]\
};\
struct regulator *buck1

#endif
static int __tcmfunc pm_set_gpio_pinstate(unsigned int gpio,unsigned int output,unsigned int level)
{
	unsigned int *rk2818_gpio_reg;
	unsigned int regoff;

	if(gpio>=RK2818GPIO_TOTAL)
		return -1;
	
	if(gpio<32)
		rk2818_gpio_reg=(unsigned int *)RK2818_GPIO0_BASE;
	else
	{
		rk2818_gpio_reg=(unsigned int *)RK2818_GPIO1_BASE;
		gpio-=32;
	}
	
	regoff=PM_GPIO_SWPORTA_DR+(gpio/8)*3;
	gpio%=8;
	
	if(output)
	{
		rk2818_gpio_reg[regoff+1]|=(1<<gpio);//ddr set dir
		if(level==0)
			rk2818_gpio_reg[regoff]&=(~(1<<gpio));//ddr set value
		else
			rk2818_gpio_reg[regoff]|=(1<<gpio);
	}
	else
		rk2818_gpio_reg[regoff+1]&=(~(1<<gpio));//ddr set dir
	return 0;
}
static void __tcmfunc rk2818_pm_save_reg(unsigned int *save,unsigned int *source,int num)
{
	int i;
	if(save&&source)
	for(i=0;i<num;i++)
	{
		save[i]=source[i];
	}
		
}
static void __tcmfunc rk2818_pm_suspend_first(void)
{
	unsigned int *rk2818_scu_reg=(unsigned int *)RK2818_SCU_BASE;
	unsigned int *rk2818_cpu_reg=(unsigned int *)RK2818_REGFILE_BASE;
	unsigned int *rk2818_gpio0_reg=(unsigned int *)RK2818_GPIO0_BASE;
	unsigned int *rk2818_gpio1_reg=(unsigned int *)RK2818_GPIO1_BASE;
	//unsigned int *rk2818_ddr_reg=(unsigned int *)RK2818_SDRAMC_BASE;

	rk2818_pm_save_reg(pm_save.pm_scu_reg,rk2818_scu_reg,PM_SCU_REG_NUM);
	rk2818_pm_save_reg(pm_save.pm_cpu_reg,rk2818_cpu_reg,PM_GENERAL_CPU_REG);
	rk2818_pm_save_reg(pm_save.pm_gpio0_reg,rk2818_gpio0_reg,PM_SCU_GPIO_SWPORTC_NUM);
	rk2818_pm_save_reg(pm_save.pm_gpio1_reg,rk2818_gpio1_reg,PM_SCU_GPIO_SWPORTC_NUM);
	pm_save.scu_regbit=0;
	pm_save.cpu_regbit=0;
	pm_save.gpio0_regbit=0x6db;
	pm_save.gpio1_regbit=0x6db;
	//pm_save.savereg[0]=rk2818_ddr_reg[82];
	//rk2818_ddr_reg[82]=rk2818_ddr_reg[82]&(~(0xffff))&(~(0xf<<20));
	//pm_set_gpio_pinstate(RK2818_PIN_PC2,1,0);
	tcm_printascii("suspend frist\n");
}

static void __tcmfunc rk2818_pm_resume_first(void)
{
	//unsigned int *rk2818_ddr_reg=(unsigned int *)RK2818_SDRAMC_BASE;
	//pm_set_gpio_pinstate(RK2818_PIN_PC2,1,1);
	//rk2818_ddr_reg[82]=pm_save.savereg[0];
}
static void __tcmfunc rk2818_soc_scu_suspend(void)
{

	unsigned int *rk2818_scu_reg=(unsigned int *)RK2818_SCU_BASE;

    	pm_save.scu_regbit|=(0x1<<PM_SCU_CLKGATE0_CON);
	rk2818_scu_reg[PM_SCU_CLKGATE0_CON] = SCU_GATE0CLK_ALL_DIS&(~DIS_ARM_CLK)&(~DIS_TIMER_CLK)&(~DIS_GPIO0_CLK)&(~DIS_GPIO1_CLK)&(~DIS_INTC_CLK)&(~DIS_UART1_CLK);
		//&(~DIS_UART0_CLK)&(~DIS_UART1_CLK);


	pm_save.scu_regbit|=(0x1<<PM_SCU_CLKGATE1_CON);
	rk2818_scu_reg[PM_SCU_CLKGATE1_CON] =SCU_GATE1CLK_BASE_SET|EN_AXI_CLK/*&(~DIS_DDR_CLK)&(~DIS_DDR_HCLK)*/;

	pm_save.scu_regbit|=(0x1<<PM_SCU_CLKGATE2_CON);
        rk2818_scu_reg[PM_SCU_CLKGATE2_CON] =SCU_GATE2CLK_BASE_SET&(~DIS_ITCMBUS_CLK)
	&(~DIS_DTCM0BUS_CLK)&(~DIS_DTCM1BUS_CLK);
	
	//pm_save.scu_regbit|=(0x1<<PM_SCU_CLKSEL0_CON);
	//rk2818_scu_reg[PM_SCU_CLKSEL0_CON] |=(rk2818_scu_reg[PM_SCU_CLKSEL0_CON]&PM_BIT_CLEAR(CLKSEL0_HCLK,2)&PM_BIT_CLEAR(CLKSEL0_PCLK,2))
	//	|CLKSEL0_HCLK21;

	pm_save.scu_regbit|=(0x1<<PM_SCU_DPLL_CON);
	rk2818_scu_reg[PM_SCU_DPLL_CON] |= DSPPLL_POERDOWN;    //dsp pll power down
	
    	pm_save.scu_regbit|=(0x1<<PM_SCU_CPLL_CON);
	rk2818_scu_reg[PM_SCU_CPLL_CON] |= CPLL_POERDOWN;    //dsp pll power down

	tcm_udelay(1, 24);
	//tcm_printascii("dsp pll power down\n");
	
	pm_save.scu_regbit|=(0x1<<PM_SCU_PMU_CON);
	rk2818_scu_reg[PM_SCU_PMU_CON] |=LCDC_POWER_DOWN;
	rk2818_scu_reg[PM_SCU_PMU_CON] |=DSP_POWER_DOWN;

	tcm_udelay(1, 24);
	//tcm_printascii("dsp power down\n");

	pm_save.scu_regbit|=(0x1<<PM_CLKSEL2_CON);
	rk2818_scu_reg[PM_CLKSEL2_CON] =(rk2818_scu_reg[PM_CLKSEL2_CON]&(~0xf))|0x7;

	pm_save.scu_regbit|=(0x1<<PM_SCU_MODE_CON);
	rk2818_scu_reg[PM_SCU_MODE_CON] &= CPU_SLOW_MODE;	//cpu slow mode

	pm_save.scu_regbit|=(0x1<<PM_SCU_APLL_CON);
	//rk2818_scu_reg[PM_SCU_APLL_CON] |= ARMPLL_BYPASSMODE;//enable arm pll bypass
	rk2818_scu_reg[PM_SCU_APLL_CON] |= ARMPLL_POERDOWN;	//arm pll power down
	
	//tcm_printascii("arm pll power down\n");

	//rk2818_scu_reg[PM_SCU_PMU_CON] |=DDR_POWER_DOWN;
	//pm_save.scu_regbit|=(0x1<<PM_SCU_SOFTRST_CON);
	//rk2818_scu_reg[PM_SCU_SOFTRST_CON]|=RST_DDR_CORE_LOGIC;//RST_DDR_BUS/RST_DDR_CORE_LOGIC

	#ifdef RK2818_MOBLIE_PM_PRT_CHANGED_REG
	rk2818_pm_save_reg(pm_save.pm_scu_reg_ch,rk2818_scu_reg,PM_SCU_REG_NUM);
	#endif
}

/*static void __tcmfunc rk2818_soc_general_cpu_suspend(void)
{

	unsigned int *rk2818_cpu_reg=(unsigned int *)RK2818_REGFILE_BASE;
	//unsigned int *rk2818_gpio0_reg=(unsigned int *)RK2818_GPIO0_BASE;
	//unsigned int *rk2818_gpio1_reg=(unsigned int *)RK2818_GPIO1_BASE;

	#if 0
	pm_save.cpu_regbit|=(0x1<<PM_GPIO0_AB_PU_CON);
	rk2818_cpu_reg[PM_GPIO0_AB_PU_CON] =GPIO0_AB_NORMAL|PMGPIO_DN(GPIO0_A3);

	pm_save.cpu_regbit|=(0x1<<PM_GPIO0_CD_PU_CON);
	rk2818_cpu_reg[PM_GPIO0_CD_PU_CON] = GPIO0_CD_NORMAL;
	
	pm_save.cpu_regbit|=(0x1<<PM_GPIO1_AB_PU_CON);
	rk2818_cpu_reg[PM_GPIO1_AB_PU_CON] = GPIO1_AB_NORMAL;
	
	pm_save.cpu_regbit|=(0x1<<PM_GPIO1_CD_PU_CON);
	rk2818_cpu_reg[PM_GPIO1_CD_PU_CON] = GPIO1_CD_NORMAL;
	#endif

	pm_save.cpu_regbit|=(0x1<<PM_IOMUX_A_CON);
	pm_save.cpu_regbit|=(0x1<<PM_IOMUX_B_CON);

	#if 1  //set uart0 pin
	rk2818_cpu_reg[PM_IOMUX_A_CON] &=(~(0x3<<PM_UART0_OUT))&(~(0x3<<PM_UART0_IN));// 00 gpio 01uart
	rk2818_cpu_reg[PM_IOMUX_B_CON] &=(~(0x1<<PM_UART0_RTS))&(~(0x1<<PM_UART0_CTS));//
	pm_set_gpio_pinstate(RK2818_PIN_PG0,0,0);//uart0 sin pin
	pm_set_gpio_pinstate(RK2818_PIN_PG1,0,0);//uart0 sout pin	
	pm_set_gpio_pinstate(RK2818_PIN_PG0,0,0);//uart0 sin pin
	pm_set_gpio_pinstate(RK2818_PIN_PG1,0,0);//uart0 sout pin
	pm_set_gpio_pinstate(RK2818_PIN_PB2,0,0);//uart0 cts pin
	pm_set_gpio_pinstate(RK2818_PIN_PB3,0,0);//uart0 rts pin
	pm_set_gpio_pinstate(RK2818_PIN_PF7,0,0);//uart0 dtr pin
	//pm_set_gpio_pinstate(RK2818_PIN_PE0,0,0);//uart0 dsr pin
	#endif

	#if 0  //set uart1 pin
	rk2818_cpu_reg[PM_IOMUX_A_CON] &=(~(0x3<<PM_UART1_OUT))&(~(0x3<<PM_UART1_IN));// 00 gpio 01uart
	pm_set_gpio_pinstate(RK2818_PIN_PF0,0,0);//uart0 sin pin
	pm_set_gpio_pinstate(RK2818_PIN_PG1,0,0);//uart0 sout pin
	#endif

	#if 0 //set i2c0 pin
	rk2818_cpu_reg[PM_IOMUX_A_CON] |=(0x1<<PM_I2C0);// 1 gpio;0 i2c
	pm_set_gpio_pinstate(RK2818_PIN_PE4,0,0);//sda pin
	pm_set_gpio_pinstate(RK2818_PIN_PE5,0,0);//scl dsr pin
	#endif

	#if 0  //set i2c1 pin
	rk2818_cpu_reg[PM_IOMUX_A_CON] &=(~(0x3<<PM_I2C1));// 1 gpio;0 i2c
	pm_set_gpio_pinstate(RK2818_PIN_PE6,0,0);//sda pin
	pm_set_gpio_pinstate(RK2818_PIN_PE7,0,0);//scl dsr pin
	#endif

	#if 1  // sdio0
	rk2818_cpu_reg[PM_IOMUX_A_CON] &=(~(0x1<<PM_SDIO0_CMD))&(~(0x1<<PM_SDIO0_DATA));// 1 gpio;0 i2c
	pm_set_gpio_pinstate(RK2818_PIN_PH0,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PH1,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PH2,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PH3,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PH4,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PH5,0,0);
	//pm_set_gpio_pinstate(RK2818_PIN_PF3,0,0);
	#endif

	#if 0 // sdio1
	rk2818_cpu_reg[PM_IOMUX_A_CON] &=(~(0x1<<PM_SDIO1_CMD))&(~(0x1<<PM_SDIO1_DATA));// 1 gpio;0 i2c
	pm_set_gpio_pinstate(RK2818_PIN_PG2,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PG3,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PG4,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PG5,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PG6,0,0);
	pm_set_gpio_pinstate(RK2818_PIN_PG7,0,0);
	#endif

#ifdef RK2818_MOBLIE_PM_PRT_CHANGED_REG
	rk2818_pm_save_reg(pm_save.pm_cpu_reg_ch,rk2818_cpu_reg,PM_GENERAL_CPU_REG);
	rk2818_pm_save_reg(pm_save.pm_gpio0_reg_ch,rk2818_gpio0_reg,PM_SCU_GPIO_SWPORTC_NUM);
	rk2818_pm_save_reg(pm_save.pm_gpio1_reg_ch,rk2818_gpio1_reg,PM_SCU_GPIO_SWPORTC_NUM);
#endif

}*/

static void __tcmfunc rk2818_soc_resume(unsigned int *pm_save_reg,unsigned int *base_add,u16 regbit,int num)
{
	int i;
	tcm_printascii("rk2818_soc_resume\n");
	for(i=0;i<num;i++)
	{
	       if((regbit>>i)&0x0001)
		base_add[i]=pm_save_reg[i];
	}
}
void __tcmfunc rk2818_pm_soc_suspend(void)
{ 
	rk2818_soc_scu_suspend( );
	//rk2818_soc_general_cpu_suspend( );
}
void __tcmfunc rk2818_pm_soc_resume(void)
{
	rk2818_soc_resume(pm_save.pm_scu_reg,(unsigned int *)RK2818_SCU_BASE,pm_save.scu_regbit,PM_SCU_REG_NUM);
	//rk2818_soc_resume(pm_save.pm_cpu_reg,(unsigned int *)RK2818_REGFILE_BASE,pm_save.cpu_regbit,PM_GENERAL_CPU_REG);
}

/*void rk2818_pm_print(void)
{
	//rk2818_pm_reg_print(pm_save.pm_scu_reg,pm_save.pm_scu_reg_ch,PM_SCU_REG_NUM,"scu");
	//rk2818_pm_reg_print(pm_save.pm_cpu_reg,pm_save.pm_cpu_reg,PM_GENERAL_CPU_REG,"general_cpu");
	//rk2818_pm_reg_print(pm_save.pm_gpio0_reg,pm_save.pm_gpio0_reg,PM_SCU_GPIO_SWPORTC_NUM,"gpio0");
}

static void rk2818_pm_reg_print(unsigned int *pm_save_reg,unsigned int *pm_ch_reg,int num,char *name)
{
	 int i;

#ifdef RK2818_MOBLIE_PM_PRT_ORIGINAL_REG
	printk("***the follow inf is %s original reg***\n",name);
	for(i=0;i<num;i++)
	{
	    printk(" %d,%x",i,pm_save_reg[i]);
	}
	printk("\n");
#endif

#ifdef RK2818_MOBLIE_PM_PRT_CHANGED_REG
	printk("***the follow inf is %s changed reg***\n",name);
	for(i=0;i<num;i++)
	{
	    printk(" %d,%x",i,pm_ch_reg[i]);
	}
	printk("\n");
#endif
}*/

#else
#define rk2818_pm_suspend_first(a)
#define rk2818_pm_resume_first(b)
#define rk2818_pm_soc_suspend(a)
#define rk2818_pm_soc_resume(a)
#define rk2818_define_value

#endif

static int __tcmfunc rk2818_tcm_idle(void)
{
       //rk2818_define_value;
	volatile u32 unit;
	u32 ctrl_reg_62;
	
	u32 scu_mode = scu_readl(SCU_MODE_CON);
	u32 scu_apll = scu_readl(SCU_APLL_CON);
	u32 scu_clksel0 = scu_readl(SCU_CLKSEL0_CON);
	unsigned int *rk2818_scu_reg=(unsigned int *)RK2818_SCU_BASE;
	
	asm("b 1f; .align 5; 1:");
	asm("mcr p15, 0, r0, c7, c10, 4");	/* drain write buffer */
	
	scu_writel(scu_mode & ~(3 << 2), SCU_MODE_CON); // slow
	scu_writel(scu_apll | PLL_PD, SCU_APLL_CON); // powerdown

	ctrl_reg_62 = sdram_enter_self_refresh();
	rk2818_scu_reg[PM_SCU_CLKGATE2_CON]&=(~DIS_DTCM0BUS_CLK)&(~DIS_DTCM1BUS_CLK);
	tcm_udelay(1, 24);
	rk2818_pm_suspend_first( );
	tcm_udelay(1, 24);
	rk2818_pm_soc_suspend( );
	tcm_udelay(1, 24);
	asm("mcr p15, 0, r0, c7, c0, 4");	/* wait for interrupt */
	rk2818_pm_resume_first( );
	tcm_udelay(1, 24);
	rk2818_pm_soc_resume( );
	tcm_udelay(1, 24);
	sdram_exit_self_refresh(ctrl_reg_62);
	
	//regulator_set_voltage(buck1,1200000,1200000);

	scu_writel(scu_apll, SCU_APLL_CON); // powerup
	scu_writel(scu_clksel0 & (~3), SCU_CLKSEL0_CON);
	
	tcm_udelay(2, 24);

	unit = 7200;  /* 24m,0.3ms , 24*300*/
	while (unit-- > 0) {
		if (regfile_readl(CPU_APB_REG0) & 0x80)
			break;
	}

	tcm_udelay(5 << 8, 24);
	scu_writel(scu_clksel0, SCU_CLKSEL0_CON);
	tcm_udelay(5, 24);

	scu_writel(scu_mode, SCU_MODE_CON); // normal
	//rk2818_pm_print( );
	return unit;
}

static void rk2818_idle(void)
{
	unsigned long old_sp;
	unsigned long tcm_sp = ITCM_END & ~7;

	asm volatile ("mov %0, sp" : "=r" (old_sp));
	asm volatile ("mov sp, %0" :: "r" (tcm_sp));
	rk2818_tcm_idle();
	asm volatile ("mov sp, %0" :: "r" (old_sp));
}

static void dump_register(void)
{
#ifdef CONFIG_PM_DEBUG
	printk(KERN_DEBUG "SCU_APLL_CON       : 0x%08x\n", scu_readl(SCU_APLL_CON));
	printk(KERN_DEBUG "SCU_DPLL_CON       : 0x%08x\n", scu_readl(SCU_DPLL_CON));
	printk(KERN_DEBUG "SCU_CPLL_CON       : 0x%08x\n", scu_readl(SCU_CPLL_CON));
	printk(KERN_DEBUG "SCU_MODE_CON       : 0x%08x\n", scu_readl(SCU_MODE_CON));
	printk(KERN_DEBUG "SCU_PMU_CON        : 0x%08x\n", scu_readl(SCU_PMU_CON));
	printk(KERN_DEBUG "SCU_CLKSEL_CON     : 0x%08x 0x%08x 0x%08x\n", scu_readl(SCU_CLKSEL0_CON), scu_readl(SCU_CLKSEL1_CON), scu_readl(SCU_CLKSEL2_CON));
	printk(KERN_DEBUG "SCU_CLKGATE_CON    : 0x%08x 0x%08x 0x%08x\n", scu_readl(SCU_CLKGATE0_CON), scu_readl(SCU_CLKGATE1_CON), scu_readl(SCU_CLKGATE2_CON));
	printk(KERN_DEBUG "IRQ_INTEN          : 0x%08x 0x%02x\n", irq_readl(IRQ_REG_INTEN_L), irq_readl(IRQ_REG_INTEN_H));
	printk(KERN_DEBUG "IRQ_INTMASK        : 0x%08x 0x%02x\n", irq_readl(IRQ_REG_INTMASK_L), irq_readl(IRQ_REG_INTMASK_H));
	printk(KERN_DEBUG "IRQ_INTFORCE       : 0x%08x 0x%02x\n", irq_readl(IRQ_REG_INTFORCE_L), irq_readl(IRQ_REG_INTFORCE_H));
	printk(KERN_DEBUG "IRQ_RAWSTATUS      : 0x%08x 0x%02x\n", irq_readl(IRQ_REG_RAWSTATUS_L), irq_readl(IRQ_REG_RAWSTATUS_H));
	printk(KERN_DEBUG "IRQ_STATUS         : 0x%08x 0x%02x\n", irq_readl(IRQ_REG_STATUS_L), irq_readl(IRQ_REG_STATUS_H));
	printk(KERN_DEBUG "IRQ_MASKSTATUS     : 0x%08x 0x%02x\n", irq_readl(IRQ_REG_MASKSTATUS_L), irq_readl(IRQ_REG_MASKSTATUS_H));
	printk(KERN_DEBUG "IRQ_FINALSTATUS    : 0x%08x 0x%02x\n", irq_readl(IRQ_REG_FINALSTATUS_L), irq_readl(IRQ_REG_FINALSTATUS_H));
	printk(KERN_DEBUG "GPIO_INTEN         : 0x%08x 0x%08x\n", gpio0_readl(GPIO_INTEN), gpio1_readl(GPIO_INTEN));
	printk(KERN_DEBUG "GPIO_INTMASK       : 0x%08x 0x%08x\n", gpio0_readl(GPIO_INTMASK), gpio1_readl(GPIO_INTMASK));
	printk(KERN_DEBUG "GPIO_INTTYPE_LEVEL : 0x%08x 0x%08x\n", gpio0_readl(GPIO_INTTYPE_LEVEL), gpio1_readl(GPIO_INTTYPE_LEVEL));
	printk(KERN_DEBUG "GPIO_INT_POLARITY  : 0x%08x 0x%08x\n", gpio0_readl(GPIO_INT_POLARITY), gpio1_readl(GPIO_INT_POLARITY));
	printk(KERN_DEBUG "GPIO_INT_STATUS    : 0x%08x 0x%08x\n", gpio0_readl(GPIO_INT_STATUS), gpio1_readl(GPIO_INT_STATUS));
	printk(KERN_DEBUG "GPIO_INT_RAWSTATUS : 0x%08x 0x%08x\n", gpio0_readl(GPIO_INT_RAWSTATUS), gpio1_readl(GPIO_INT_RAWSTATUS));
#endif
}

static int rk2818_pm_enter(suspend_state_t state)
{
	//struct clk *arm_clk = clk_get(NULL, "arm");
	//unsigned long arm_rate = clk_get_rate(arm_clk);

	int irq_val = 0;
    	//struct regulator *buck1;
	//u32 scu_mode = scu_readl(SCU_MODE_CON);
	//u32 scu_apll = scu_readl(SCU_APLL_CON);
	//u32 scu_clksel0 = scu_readl(SCU_CLKSEL0_CON);
	printk(KERN_DEBUG "before core halt\n");

	//clk_set_rate(arm_clk, 24000000);
	dump_register();

#ifdef CONFIG_RK28_USB_WAKE
	rockchip_timer_clocksource_suspend_resume(1);
	while (!irq_val) {
		rk28_usb_suspend(0);
#endif

		rk2818_idle();

#ifdef CONFIG_RK28_USB_WAKE
		rk28_usb_suspend(1);
		__udelay(400);

		irq_val = rockchip_timer_clocksource_irq_checkandclear();
		irq_val |= rk28_usb_check_vbus_change();
		irq_val |= rk28_usb_check_connectid_change();
	}

	rockchip_timer_clocksource_suspend_resume(0);
#endif
	dump_register();
	//clk_set_rate(arm_clk, arm_rate);
	printk(KERN_DEBUG "quit arm halt,irq_val=0x%x\n", irq_val);
	return 0;
}

static struct platform_suspend_ops rk2818_pm_ops = {
	.enter		= rk2818_pm_enter,
	.valid		= suspend_valid_only_mem,
};

static int __init rk2818_pm_init(void)
{
	suspend_set_ops(&rk2818_pm_ops);
	return 0;
}

__initcall(rk2818_pm_init);

