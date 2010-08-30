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
//函数名: sdram_enter_self_refresh
//描述: SDRAM进入自刷新模式
//参数说明:
//返回值: 对于DDR就是CTRL_REG_62的值，供sdram_exit_self_refresh使用
//相关全局变量:
//注意:(1)系统完全idle后才能进入自刷新模式，进入自刷新后不能再访问SDRAM
//     (2)要进入自刷新模式，必须保证运行时这个函数所调用到的所有代码不在SDRAM上
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
//函数名: sdram_exit_self_refresh
//描述: SDRAM退出自刷新模式
//参数说明:
//返回值:
//相关全局变量:
//注意:(1)SDRAM在自刷新模式后不能被访问，必须先退出自刷新模式
//     (2)必须保证运行时这个函数的代码不在SDRAM上
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

static void __tcmfunc rk2818_tcm_idle(void)
{
	u32 ctrl_reg_62;

	asm("b 1f; .align 5; 1:");
	asm("mcr p15, 0, r0, c7, c10, 4");	/* drain write buffer */

	ctrl_reg_62 = sdram_enter_self_refresh();
	asm("mcr p15, 0, r0, c7, c0, 4");	/* wait for interrupt */
	sdram_exit_self_refresh(ctrl_reg_62);
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
	int irq_val = 0;
	struct clk *arm_clk = clk_get(NULL, "arm");
	unsigned long arm_rate = clk_get_rate(arm_clk);

	printk(KERN_DEBUG "before core halt\n");

	clk_set_rate(arm_clk, 24000000);
	dump_register();

#ifdef CONFIG_RK28_USB_WAKE
	rockchip_timer_clocksource_suspend_resume(1);

	while (!irq_val) {
		rk28_usb_suspend(0);
#endif

		rk2818_idle();

#ifdef CONFIG_RK28_USB_WAKE
		rk28_usb_suspend(1);
		udelay(400);

		irq_val = rockchip_timer_clocksource_irq_checkandclear();
		irq_val |= rk28_usb_check_vbus_change();
		irq_val |= rk28_usb_check_connectid_change();
	}

	rockchip_timer_clocksource_suspend_resume(0);
#endif

	dump_register();
	clk_set_rate(arm_clk, arm_rate);

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

