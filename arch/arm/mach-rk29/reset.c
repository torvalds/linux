#include <linux/kernel.h>
#include <linux/reboot.h>

#include <asm/io.h>
#include <asm/proc-fns.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/traps.h>
#include <asm/sections.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/stacktrace.h>

#include <mach/rk29_iomap.h>
#include <mach/cru.h>
#include <mach/memory.h>
#include <mach/sram.h>
#include <linux/clk.h>

#include <asm/delay.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

static inline void delay_500ns(void)
{
	int delay = 13;
	barrier();
	while (delay--)
		barrier();
}


#if 0
volatile int testflag;
static void  __rk29_reset_to_maskrom(void)
{
	u32 reg;
    asm("mrc    p15, 0, %0, c1, c0, 0\n\t"
        "bic    %0, %0, #(1 << 13)  @set vector to 0x00000000\n\t"
        "bic    %0, %0, #(1 << 0)   @disable mmu\n\t"
        "bic    %0, %0, #(1 << 12)  @disable I CACHE\n\t"
        "bic    %0, %0, #(1 << 2)   @disable D DACHE\n\t"
        "bic    %0, %0, #(1 << 11)      @disable \n\t"
        "bic    %0, %0, #(1 << 28)      @disable \n\t"
        "mcr    p15, 0, %0, c1, c0, 0\n\t"
    //      "mcr    p15, 0, %0, c8, c7, 0   @ invalidate whole TLB\n\t"
    //      "mcr    p15, 0, %0, c7, c5, 6   @ invalidate BTC\n\t"
        : "=r" (reg));

    asm("b 1f\n\t"
        ".align 5\n\t"
        "1:\n\t"
        "mcr    p15, 0, %0, c7, c10, 5\n\t"
        "mcr    p15, 0, %0, c7, c10, 4\n\t"
        "mov    pc, #0" : : "r" (reg));
} 
#endif

static void  pwm2gpiodefault(void)
{
	#define     REG_FILE_BASE_ADDR         RK29_GRF_BASE
	volatile unsigned int * pGRF_GPIO2L_IOMUX =  (volatile unsigned int *)(REG_FILE_BASE_ADDR + 0x58);
	#define     GPIO2_BASE_ADDR            RK29_GPIO2_BASE
	volatile unsigned int *pGPIO2_DIR = (volatile unsigned int *)(GPIO2_BASE_ADDR + 0x4);

	// iomux pwm2 to gpio2_a[3]
	*pGRF_GPIO2L_IOMUX &= ~(0x3<<6);
	// set gpio to input
	*pGPIO2_DIR &= ~(0x1<<3);

	memset((void *)RK29_PWM_BASE, 0, 0x40);
} 


extern void __rb( void*  );
void rb( void )
{
    void(*cb)(void* ) ;
    
    void * uart_base = (unsigned int *)ioremap( RK29_UART1_PHYS , RK29_UART1_SIZE );
    local_irq_disable();
    cb =  (void(*)(void* ))__pa(__rb);
    __cpuc_flush_kern_all();
    __cpuc_flush_user_all();
    //printk("begin to jump to reboot,uart1 va=0x%p\n" , uart_base);
    //while(testflag);    
    cb( uart_base );
}

void  rk29_arch_reset(int mode, const char *cmd)
{
	local_irq_disable();
	local_fiq_disable();

#ifdef CONFIG_MACH_RK29SDK
	/* from panic? loop for debug */
	if (system_state != SYSTEM_RESTART) {
		printk("\nLoop for debug...\n");
		while (1);
	}
#endif

	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CPU_MODE_MASK) | CRU_CPU_MODE_SLOW, CRU_MODE_CON);
	delay_500ns();

	/* from panic? */
	if (system_state != SYSTEM_RESTART)
		machine_power_off();

	pwm2gpiodefault();

	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | CRU_GENERAL_MODE_SLOW, CRU_MODE_CON);
	delay_500ns();

	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | CRU_CODEC_MODE_SLOW, CRU_MODE_CON);
	delay_500ns();
	
	cru_writel(0, CRU_CLKGATE0_CON);
	cru_writel(0, CRU_CLKGATE1_CON);
	cru_writel(0, CRU_CLKGATE2_CON);
	cru_writel(0, CRU_CLKGATE3_CON);
	delay_500ns();

	cru_writel(0, CRU_SOFTRST0_CON);
	cru_writel(0, CRU_SOFTRST1_CON);
	cru_writel(0, CRU_SOFTRST2_CON);
	

	//SPI0 clock source = periph_pll_clk, SPI0 divider=8
	cru_writel((cru_readl(CRU_CLKSEL6_CON) & ~0x1FF) | (7 << 2), CRU_CLKSEL6_CON);

	//eMMC divider=0x17, SD/MMC0 clock source=arm_pll_clk
	cru_writel((cru_readl(CRU_CLKSEL7_CON) & ~(3 | (0x3f << 18))) | (0x17 << 18), CRU_CLKSEL7_CON);

	//UART1 clock divider=0, UART1 clk =24MHz , UART0 and UART1 clock source=periph_pll_clk
	cru_writel((cru_readl(CRU_CLKSEL8_CON) & ~(7 | (0x3f << 14) | (3 << 20))) | (2 << 20), CRU_CLKSEL8_CON);

	writel(readl(RK29_GRF_PHYS + 0xc0) & ~(1 << 21), RK29_GRF_PHYS + 0xc0);
	writel(readl(RK29_GRF_PHYS + 0xbc) & ~(1 << 9), RK29_GRF_PHYS + 0xbc);

	writel(0, RK29_CPU_AXI_BUS0_PHYS);
	writel(0, RK29_AXI1_PHYS);

	//__cpuc_flush_kern_all();
	//__cpuc_flush_user_all();
	
    rb();

}


