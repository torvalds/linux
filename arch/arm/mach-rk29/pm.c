#define DEBUG

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
#include <asm/tlbflush.h>
#include <asm/hardware/gic.h>

#include <mach/rk29_iomap.h>
#include <mach/cru.h>
#include <mach/pmu.h>
#include <mach/board.h>
#include <mach/system.h>
#include <mach/sram.h>
#include <mach/gpio.h>
#include <mach/ddr.h>
#include <mach/memtester.h>
#include <mach/iomux.h>
#include <mach/pm-vol.h>

#include <asm/vfp.h>

#define grf_readl(offset) readl(RK29_GRF_BASE + offset)
#define grf_writel(v, offset) do { writel(v, RK29_GRF_BASE + offset); readl(RK29_GRF_BASE + offset); } while (0)

static unsigned long save_sp;

static inline void delay_500ns(void)
{
	LOOP(LOOPS_PER_USEC);
}

static inline void delay_300us(void)
{
	LOOP(300 * LOOPS_PER_USEC);
}

#ifdef DEBUG
 void/* inline*/ __sramfunc sram_printch(char byte)
{
	unsigned long flags;
	u32 gate1, gate2;

	local_irq_save(flags);
	gate1 = cru_readl(CRU_CLKGATE1_CON);
	gate2 = cru_readl(CRU_CLKGATE2_CON);
	cru_writel(gate1 & ~((1 << CLK_GATE_PCLK_PEIRPH % 32) | (1 << CLK_GATE_ACLK_PEIRPH % 32) | (1 << CLK_GATE_ACLK_CPU_PERI % 32)), CRU_CLKGATE1_CON);
	cru_writel(gate2 & ~(1 << CLK_GATE_UART1 % 32), CRU_CLKGATE2_CON);
	delay_500ns();

	writel(byte, RK29_UART1_BASE);

	/* loop check LSR[6], Transmitter Empty bit */
	while (!(readl(RK29_UART1_BASE + 0x14) & 0x40))
		barrier();

	cru_writel(gate2, CRU_CLKGATE2_CON);
	cru_writel(gate1, CRU_CLKGATE1_CON);
	local_irq_restore(flags);
	if (byte == '\n')
		sram_printch('\r');
}

 void __sramfunc sram_printascii(const char *s)
{
	while (*s) {
		if (*s == '\n')
		{
		    sram_printch('\r');
		}
	    sram_printch(*s);
	    s++;
	}
}
void print(const char *s)
{
    sram_printascii(s);
}

void __sramfunc print_Hex(unsigned int hex)
{
	int i = 8;
	sram_printch('0');
	sram_printch('x');
	while (i--) {
		unsigned char c = (hex & 0xF0000000) >> 28;
		sram_printch(c < 0xa ? c + '0' : c - 0xa + 'a');
		hex <<= 4;
	}
}

void __sramfunc print_Dec (uint32_t n)
{
    if (n >= 10)
    {
        print_Dec(n / 10);
        n %= 10;
    }
    sram_printch((char)(n + '0'));
}

void print_Dec_3(uint32_t value)
{
    if(value<10)
    {
        print("  ");
    }
    else if(value<100)
    {
        print(" ");
    }
    else
    {
    }
    print_Dec(value);
}

static void /* inline*/ __sramfunc printhex(unsigned int hex)
{
	int i = 8;
	sram_printch('0');
	sram_printch('x');
	while (i--) {
		unsigned char c = (hex & 0xF0000000) >> 28;
		sram_printch(c < 0xa ? c + '0' : c - 0xa + 'a');
		hex <<= 4;
	}
}
#else
static void inline sram_printch(char byte) {}
static void inline sram_printascii(const char *s) {}
static void inline printhex(unsigned int hex) {}
#endif /* DEBUG */

/*volatile __sramdata */int ddr_debug;
module_param(ddr_debug, int, 0644);
#if 1
static int inline calc_crc32(u32 addr, size_t len)
{
     return crc32_le(~0,(const unsigned char *)addr,len);
}
void __sramfunc ddr_testmode(void)
{    
    int32_t g_crc1,g_crc2;
    uint32_t nMHz;
    uint32_t n = 0;
    extern char _stext[], _etext[];
    if(ddr_debug == 1)
    {
        for (;;)
        {
	        sram_printch(' ');
	        sram_printch('8');
	        sram_printch('8');
	        sram_printch('8');
	        sram_printch(' ');
            g_crc1 = calc_crc32((u32)_stext, (size_t)(_etext-_stext));
            nMHz = 333 + (random32()>>25);
            if(nMHz > 402)
                nMHz = 402;
	        printhex(nMHz);
	        sram_printch(' ');
	        printhex(n++);
            //ddr_print("%s change freq to: %d MHz\n", __func__, nMHz);
            ddr_change_freq(nMHz);
            g_crc2 = calc_crc32((u32)_stext, (size_t)(_etext-_stext));
            if (g_crc1!=g_crc2)
            {
	            sram_printch(' ');
	            sram_printch('f');
	            sram_printch('a');
	            sram_printch('i');
	            sram_printch('l');
	        }
               //ddr_print("check image crc32 success--crc value = 0x%x!, count:%d\n",g_crc1, n++);
           //     sram_printascii("change freq success\n");
        }
    }
    else if(ddr_debug == 2)
    {
        for (;;)
        {
	        sram_printch(' ');
	        sram_printch('9');
	        sram_printch('9');
	        sram_printch('9');
	        sram_printch(' ');
            g_crc1 = calc_crc32((u32)_stext, (size_t)(_etext-_stext));
            nMHz = (random32()>>13);// 16.7s max
            ddr_suspend();
            delayus(nMHz);
            ddr_resume();
	        printhex(nMHz);
	        sram_printch(' ');
	        printhex(n++);
            g_crc2 = calc_crc32((u32)_stext, (size_t)(_etext-_stext));
            if (g_crc1!=g_crc2)
            {
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
    }
    else if(ddr_debug == 3)
    {
        memtester();
    }
}
#else
void __sramfunc ddr_testmode(void)
{}

#endif 
void __sramfunc pm_clk_switch_32k(void);

void __sramfunc pm_wfi(void)
{
	u32 clksel0;
	sram_printch('7');
	clksel0 = cru_readl(CRU_CLKSEL0_CON);
	/* set arm clk 24MHz/32 = 750KHz */
	cru_writel(clksel0 | 0x1F, CRU_CLKSEL0_CON);

	sram_printch('8');
	dsb();
	asm("wfi");
	sram_printch('8');

	/* resume arm clk */
	cru_writel(clksel0, CRU_CLKSEL0_CON);
	sram_printch('7');


}

static void __sramfunc rk29_sram_suspend(void)
{
	u32 vol;

	if ((ddr_debug == 1) || (ddr_debug == 2))
		ddr_testmode();

	sram_printch('5');
	ddr_suspend();

	sram_printch('6');
	vol=rk29_suspend_voltage_set(1000000);
#ifdef CONFIG_RK29_CLK_SWITCH_TO_32K
	pm_clk_switch_32k();
#else
	pm_wfi();
#endif
	rk29_suspend_voltage_resume(vol);
	sram_printch('6');

	ddr_resume();
	sram_printch('5');
}

static void noinline rk29_suspend(void)
{
	DDR_SAVE_SP(save_sp);
	rk29_sram_suspend();
	DDR_RESTORE_SP(save_sp);
}

static void dump_irq(void)
{
	u32 irq_gpio = (readl(RK29_GICPERI_BASE + GIC_DIST_PENDING_SET + 8) >> 23) & 0x7F;
	printk("wakeup irq: %08x %08x %01x\n",
		readl(RK29_GICPERI_BASE + GIC_DIST_PENDING_SET + 4),
		readl(RK29_GICPERI_BASE + GIC_DIST_PENDING_SET + 8),
		readl(RK29_GICPERI_BASE + GIC_DIST_PENDING_SET + 12) & 0xf);
	if (irq_gpio & 1)
		printk("wakeup gpio0: %08x\n", readl(RK29_GPIO0_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 2)
		printk("wakeup gpio1: %08x\n", readl(RK29_GPIO1_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 4)
		printk("wakeup gpio2: %08x\n", readl(RK29_GPIO2_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 8)
		printk("wakeup gpio3: %08x\n", readl(RK29_GPIO3_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 0x10)
		printk("wakeup gpio4: %08x\n", readl(RK29_GPIO4_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 0x20)
		printk("wakeup gpio5: %08x\n", readl(RK29_GPIO5_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 0x40)
		printk("wakeup gpio6: %08x\n", readl(RK29_GPIO6_BASE + GPIO_INT_STATUS));
}

#define DUMP_GPIO_INTEN(ID) \
do { \
	u32 en = readl(RK29_GPIO##ID##_BASE + GPIO_INTEN); \
	if (en) { \
		sram_printascii("GPIO" #ID "_INTEN: "); \
		printhex(en); \
		sram_printch('\n'); \
	} \
} while (0)

static void dump_inten(void)
{
	DUMP_GPIO_INTEN(0);
	DUMP_GPIO_INTEN(1);
	DUMP_GPIO_INTEN(2);
	DUMP_GPIO_INTEN(3);
	DUMP_GPIO_INTEN(4);
	DUMP_GPIO_INTEN(5);
	DUMP_GPIO_INTEN(6);
}



#define DUMP_GPIO_PULL(ID) \
do { \
	u32 state = readl(RK29_GRF_BASE + GRF_GPIO0_PULL + (ID<<2)); \
	sram_printascii("GPIO" #ID "_PULL: "); \
	printhex(state); \
	sram_printch('\n'); \
} while (0)

static void dump_io_pull(void)
{
	DUMP_GPIO_PULL(0);
	DUMP_GPIO_PULL(1);
	DUMP_GPIO_PULL(2);
	DUMP_GPIO_PULL(3);
	DUMP_GPIO_PULL(4);
	DUMP_GPIO_PULL(5);
	DUMP_GPIO_PULL(6);
}
#ifdef CONFIG_RK29_NEON_POWERDOMAIN_SET
/*******************************neon powermain***********************/
#define pmu_read(offset)		readl(RK29_PMU_BASE + (offset))
#define pmu_write(offset, value)	writel((value), RK29_PMU_BASE + (offset))
#define PMU_PG_CON 0x10
#define vfpreg(_vfp_) #_vfp_

#define fmrx(_vfp_) ({			\
	u32 __v;			\
	asm("mrc p10, 7, %0, " vfpreg(_vfp_) ", cr0, 0 @ fmrx	%0, " #_vfp_	\
	    : "=r" (__v) : : "cc");	\
	__v;				\
 })

#define fmxr(_vfp_,_var_)		\
	asm("mcr p10, 7, %0, " vfpreg(_vfp_) ", cr0, 0 @ fmxr	" #_vfp_ ", %0"	\
	   : : "r" (_var_) : "cc")
extern void vfp_save_state(void *location, u32 fpexc);
extern void vfp_load_state(void *location, u32 fpexc);
// extern  __sramdata u64  saveptr[33];
static u32  saveptr[2][60]={};
void  neon_powerdomain_off(void)
{
	int ret,i=0;
	int *p;
	p=&saveptr[0][0];
	
	 unsigned int fpexc = fmrx(FPEXC);  //get neon Logic gate
	 
    	fmxr(FPEXC, fpexc | FPEXC_EN);  //open neon Logic gate
  	for(i=0;i<36;i++){
	vfp_save_state(p,fpexc);                        //save neon reg,32 D reg,2 control reg
	p++;
   	}  
	fmxr(FPEXC, fpexc & ~FPEXC_EN);    //close neon Logic gate
	
	 ret=pmu_read(PMU_PG_CON);                   //get power domain state
	pmu_write(PMU_PG_CON,ret|(0x1<<1));          //powerdomain off neon
	printk("neon powerdomain is off\n");
}
void   neon_powerdomain_on(void)
{
	int ret,i=0;
	int *p;
	p=&saveptr[0][0];
	
	ret=pmu_read(PMU_PG_CON);                   //get power domain state
	pmu_write(PMU_PG_CON,ret&~(0x1<<1));                //powerdomain on neon
	mdelay(4);
	
	unsigned int fpexc = fmrx(FPEXC);              //get neon Logic gate
	fmxr(FPEXC, fpexc | FPEXC_EN);                   //open neon Logic gate
	for(i=0;i<36;i++){
	vfp_load_state(p,fpexc);   //recovery neon reg, 32 D reg,2 control reg
	p++;
	}
    	fmxr(FPEXC, fpexc | FPEXC_EN);	    //open neon Logic gate
	printk("neon powerdomain is on\n");
}
#endif

void pm_gpio_suspend(void);
void pm_gpio_resume(void);

static int rk29_pm_enter(suspend_state_t state)
{
	u32 apll, cpll, gpll, mode, clksel0;
	u32 clkgate[4];
	
	#ifdef CONFIG_RK29_NEON_POWERDOMAIN_SET
	neon_powerdomain_off();
	#endif
	
	// memory teseter
	if (ddr_debug == 3)
		ddr_testmode();

	// dump GPIO INTEN for debug
	dump_inten();
	// dump GPIO PULL state for debug
	//if you want to display the information, please enable the code.
#if 0
	dump_io_pull();
#endif

	sram_printch('0');
	flush_tlb_all();
	interface_ctr_reg_pread();

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
#ifdef CONFIG_RK29_JTAG
		   | (1 << CLK_GATE_PCLK_CORE)
		   | (1 << CLK_GATE_ATCLK_CORE)
		   | (1 << CLK_GATE_ATCLK_CPU)
		   | (1 << CLK_GATE_DEBUG)
		   | (1 << CLK_GATE_TPIU)
#endif
		   ) | clkgate[0], CRU_CLKGATE0_CON);
	cru_writel(~0, CRU_CLKGATE1_CON);
	cru_writel(~((1 << CLK_GATE_GPIO1 % 32)
		   | (1 << CLK_GATE_GPIO2 % 32)
		   | (1 << CLK_GATE_GPIO3 % 32)
		   | (1 << CLK_GATE_GPIO4 % 32)
		   | (1 << CLK_GATE_GPIO5 % 32)
		   | (1 << CLK_GATE_GPIO6 % 32)
		   | (1 << CLK_GATE_PWM % 32)
#ifdef CONFIG_RK29_JTAG
		   | (1 << CLK_GATE_JTAG % 32)
#endif
		   ) | clkgate[2], CRU_CLKGATE2_CON);
	cru_writel(~0, CRU_CLKGATE3_CON);
	sram_printch('1');

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
	sram_printch('2');

	/* suspend codec pll */
	cpll = cru_readl(CRU_CPLL_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | CRU_CODEC_MODE_SLOW, CRU_MODE_CON);
	cru_writel(cpll | PLL_BYPASS, CRU_CPLL_CON);
	cru_writel(cpll | PLL_PD | PLL_BYPASS, CRU_CPLL_CON);
	delay_500ns();
	sram_printch('3');

	/* suspend general pll */
	gpll = cru_readl(CRU_GPLL_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | CRU_GENERAL_MODE_SLOW, CRU_MODE_CON);
	cru_writel(gpll | PLL_BYPASS, CRU_GPLL_CON);
	cru_writel(gpll | PLL_PD | PLL_BYPASS, CRU_GPLL_CON);
	delay_500ns();
	/* set aclk_periph = hclk_periph = pclk_periph = 24MHz */
	cru_writel(clksel0 & ~0x7FC000, CRU_CLKSEL0_CON);

	sram_printch('4');
	
	rk29_suspend();
	
	sram_printch('4');
	
	/* resume general pll */
	cru_writel(gpll, CRU_GPLL_CON);
	delay_300us();
	/* restore aclk_periph/hclk_periph/pclk_periph */
	cru_writel(cru_readl(CRU_CLKSEL0_CON) | (clksel0 & 0x7FC000), CRU_CLKSEL0_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | (mode & CRU_GENERAL_MODE_MASK), CRU_MODE_CON);
	sram_printch('3');

	/* resume codec pll */
	cru_writel(cpll, CRU_CPLL_CON);
	delay_300us();
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | (mode & CRU_CODEC_MODE_MASK), CRU_MODE_CON);
	sram_printch('2');

	/* resume arm pll */
	cru_writel(apll, CRU_APLL_CON);
	delay_300us();
	/* restore core/aclk_cpu/hclk_cpu/pclk_cpu */
	cru_writel(cru_readl(CRU_CLKSEL0_CON) | (clksel0 & 0xFFF), CRU_CLKSEL0_CON);
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CPU_MODE_MASK) | (mode & CRU_CPU_MODE_MASK), CRU_MODE_CON);
	sram_printch('1');

	/* enable clock */
	cru_writel(clkgate[0], CRU_CLKGATE0_CON);
	cru_writel(clkgate[1], CRU_CLKGATE1_CON);
	cru_writel(clkgate[2], CRU_CLKGATE2_CON);
	cru_writel(clkgate[3], CRU_CLKGATE3_CON);
	sram_printascii("0\n");

	dump_irq();

	#ifdef CONFIG_RK29_NEON_POWERDOMAIN_SET
	neon_powerdomain_on();
	#endif
	
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
			cru_writel(mode_con, CRU_MODE_CON);
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
	ddr_debug = 0;

#ifdef CONFIG_EARLYSUSPEND
	pm_set_vt_switch(0); /* disable vt switch while suspend */
#endif

	return 0;
}
__initcall(rk29_pm_init);
