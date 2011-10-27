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
#include <mach/pmu.h>
#include <mach/loader.h>
#include <mach/board.h>

#include <asm/delay.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

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

#if 0
extern void __rb( void*  );
static void rb( void )
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
#endif

static volatile u32 __sramdata reboot_reason = 0;
static void __sramfunc __noreturn rk29_rb_with_softreset(void)
{
	u32 reg;
	u32 reason = __raw_readl((u32)&reboot_reason - SRAM_CODE_OFFSET + 0x10130000);

	asm volatile (
	    "mrc	p15, 0, %0, c1, c0, 0\n\t"
	    "bic	%0, %0, #(1 << 0)	@disable MMU\n\t"
	    "bic	%0, %0, #(1 << 13)	@set vector to 0x00000000\n\t"
	    "bic	%0, %0, #(1 << 12)	@disable I CACHE\n\t"
	    "bic	%0, %0, #(1 << 2)	@disable D DACHE\n\t"
	    "bic	%0, %0, #(1 << 11)	@disable Branch prediction\n\t"
            "bic	%0, %0, #(1 << 28)	@disable TEX Remap\n\t"
	    "mcr	p15, 0, %0, c1, c0, 0\n\t"
	    "mov	%0, #0\n\t"
	    "mcr	p15, 0, %0, c8, c7, 0	@invalidate whole TLB\n\t"
	    "mcr	p15, 0, %0, c7, c5, 6	@invalidate BTC\n\t"
	    "dsb\n\t"
	    "isb\n\t"
	    "b		1f\n\t"
	    ".align 5\n\t"
	    "1:\n\t"
	    : "=r" (reg));

	writel(0x00019a00, RK29_CRU_PHYS + CRU_SOFTRST2_CON);
	dsb();
	LOOP(10 * LOOPS_PER_USEC);

	writel(0xffffffff, RK29_CRU_PHYS + CRU_SOFTRST2_CON);
	writel(0xffffffff, RK29_CRU_PHYS + CRU_SOFTRST1_CON);
	writel(0xd9fdfdc0, RK29_CRU_PHYS + CRU_SOFTRST0_CON);
	dsb();

	LOOP(100 * LOOPS_PER_USEC);

	writel(0, RK29_CRU_PHYS + CRU_SOFTRST0_CON);
	writel(0, RK29_CRU_PHYS + CRU_SOFTRST1_CON);
	writel(0x00019a00, RK29_CRU_PHYS + CRU_SOFTRST2_CON);
	dsb();
	LOOP(10 * LOOPS_PER_USEC);
	writel(0, RK29_CRU_PHYS + CRU_SOFTRST2_CON);
	dsb();
	LOOP(10 * LOOPS_PER_USEC);

	/* reset GRF_MEM_CON, else bootloader usb function may not work properly */
	writel(0, RK29_GRF_PHYS + 0xac);
	dsb();

	if (reason) {
		__raw_writel(0, RK29_TIMER0_PHYS + 0x8);
		__raw_writel(reason, RK29_TIMER0_PHYS + 0x0);
	}

	asm volatile (
	    "b 1f\n\t"
	    ".align 5\n\t"
	    "1:\n\t"
	    "dsb\n\t"
	    "isb\n\t"
	    "mov	pc, #0");

	while (1);
}

void rk29_arch_reset(int mode, const char *cmd)
{
	void (*rb2)(void);
	u32 boot_mode = BOOT_MODE_REBOOT;

	if (cmd) {
		if (!strcmp(cmd, "loader") || !strcmp(cmd, "bootloader")) {
			reboot_reason = SYS_LOADER_ERR_FLAG;
		} else if (!strcmp(cmd, "recovery")) {
			reboot_reason = SYS_LOADER_REBOOT_FLAG + BOOT_RECOVER;
			boot_mode = BOOT_MODE_RECOVERY;
		} else if (!strcmp(cmd, "charge")) {
			boot_mode = BOOT_MODE_CHARGE;
		}
	} else {
		if (system_state != SYSTEM_RESTART)
			boot_mode = BOOT_MODE_PANIC;
	}
	writel(boot_mode, RK29_GRF_BASE + 0xdc); // GRF_OS_REG3

	rb2 = (void(*)(void))((u32)rk29_rb_with_softreset - SRAM_CODE_OFFSET + 0x10130000);

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
	LOOP(LOOPS_PER_USEC);

	pwm2gpiodefault();

	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | CRU_GENERAL_MODE_SLOW, CRU_MODE_CON);
	LOOP(LOOPS_PER_USEC);

	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | CRU_CODEC_MODE_SLOW, CRU_MODE_CON);
	LOOP(LOOPS_PER_USEC);

	cru_writel(0, CRU_CLKGATE0_CON);
	cru_writel(0, CRU_CLKGATE1_CON);
	cru_writel(0, CRU_CLKGATE2_CON);
	cru_writel(0, CRU_CLKGATE3_CON);
	LOOP(LOOPS_PER_USEC);

	cru_writel(0, CRU_SOFTRST0_CON);
	cru_writel(0, CRU_SOFTRST1_CON);
	cru_writel(0, CRU_SOFTRST2_CON);
	LOOP(LOOPS_PER_USEC);

	cru_writel(1 << 16 | 1 << 13 | 1 << 11 | 1 << 1, CRU_CLKGATE3_CON);
	LOOP(LOOPS_PER_USEC);

	writel(readl(RK29_PMU_BASE + PMU_PD_CON) & ~(1 << PD_VCODEC), RK29_PMU_BASE + PMU_PD_CON);
	dsb();
	while (readl(RK29_PMU_BASE + PMU_PD_ST) & (1 << PD_VCODEC))
		;
	LOOP(10 * LOOPS_PER_MSEC);

	writel(readl(RK29_PMU_BASE + PMU_PD_CON) & ~(1 << PD_DISPLAY), RK29_PMU_BASE + PMU_PD_CON);
	dsb();
	while (readl(RK29_PMU_BASE + PMU_PD_ST) & (1 << PD_DISPLAY))
		;
	LOOP(10 * LOOPS_PER_MSEC);

	writel(readl(RK29_PMU_BASE + PMU_PD_CON) & ~(1 << PD_GPU), RK29_PMU_BASE + PMU_PD_CON);
	dsb();
	while (readl(RK29_PMU_BASE + PMU_PD_ST) & (1 << PD_GPU))
		;
	LOOP(10 * LOOPS_PER_MSEC);

	cru_writel(0, CRU_CLKGATE3_CON);
	LOOP(LOOPS_PER_USEC);

	//SPI0 clock source = periph_pll_clk, SPI0 divider=8
	cru_writel((cru_readl(CRU_CLKSEL6_CON) & ~0x1FF) | (7 << 2), CRU_CLKSEL6_CON);

	//eMMC divider=0x17, SD/MMC0 clock source=arm_pll_clk
	cru_writel((cru_readl(CRU_CLKSEL7_CON) & ~(3 | (0x3f << 18))) | (0x17 << 18), CRU_CLKSEL7_CON);

	//UART1 clock divider=0, UART1 clk =24MHz , UART0 and UART1 clock source=periph_pll_clk
	cru_writel((cru_readl(CRU_CLKSEL8_CON) & ~(7 | (0x3f << 14) | (3 << 20))) | (2 << 20), CRU_CLKSEL8_CON);

	// remap bit control = 0, normal mode
	writel(readl(RK29_GRF_BASE + 0xc0) & ~(1 << 21), RK29_GRF_BASE + 0xc0);
	// emmc_and_boot_en control=0, normal mode
	writel(readl(RK29_GRF_BASE + 0xbc) & ~(1 << 9), RK29_GRF_BASE + 0xbc);
	dsb();

	writel(0, RK29_CPU_AXI_BUS0_PHYS);
	writel(0, RK29_AXI1_PHYS);
	dsb();

	// SDMMC_CLKSRC=0, clk_source=clock divider 0
	writel(0, RK29_EMMC_PHYS + 0x0c);
	// SDMMC_CTYPE=0, card_width=1 bit mode
	writel(0, RK29_EMMC_PHYS + 0x18);
	// SDMMC_BLKSIZ=0x200, Block size=512
	writel(0x200, RK29_EMMC_PHYS + 0x1c);
	dsb();

	__cpuc_flush_kern_all();
	__cpuc_flush_user_all();
	
	rb2();
}


