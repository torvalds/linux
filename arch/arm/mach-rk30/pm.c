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

#include <mach/pmu.h>
#include <mach/board.h>
#include <mach/system.h>
#include <plat/sram.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

void __sramfunc sram_printch(char byte)
{
	writel_relaxed(byte, RK30_UART1_BASE);
	dsb();

	/* loop check LSR[6], Transmitter Empty bit */
	while (!(readl_relaxed(RK30_UART1_BASE + 0x14) & 0x40))
		barrier();

	if (byte == '\n')
		sram_printch('\r');
}

#ifdef CONFIG_DDR_TEST
static int ddr_debug;
module_param(ddr_debug, int, 0644);

static int inline calc_crc32(u32 addr, size_t len)
{
     return crc32_le(~0,(const unsigned char *)addr,len);
}

static void __sramfunc ddr_testmode(void)
{
	int32_t g_crc1,g_crc2;
	uint32_t nMHz;
	uint32_t n = 0;
	extern char _stext[], _etext[];

	if (ddr_debug == 1) {
		for (;;) {
			sram_printascii("change freq\n");
			g_crc1 = calc_crc32((u32)_stext, (size_t)(_etext-_stext));
			nMHz = 333 + random32();
			nMHz %= 490;
			if (nMHz < 100)
				nMHz = 100;
//			nMHz = ddr_change_freq(nMHz);
			sram_printhex(nMHz);
			sram_printch(' ');
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
//			ddr_suspend();
//			delayus(nMHz);
//			ddr_resume();
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
}
#else
static void __sramfunc ddr_testmode(void) {}
#endif

static void dump_irq(void)
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
	if (irq_gpio & 0x10)
		printk("wakeup gpio4: %08x\n", readl_relaxed(RK30_GPIO4_BASE + GPIO_INT_STATUS));
	if (irq_gpio & 0x40)
		printk("wakeup gpio6: %08x\n", readl_relaxed(RK30_GPIO6_BASE + GPIO_INT_STATUS));
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

static void dump_inten(void)
{
	DUMP_GPIO_INTEN(0);
	DUMP_GPIO_INTEN(1);
	DUMP_GPIO_INTEN(2);
	DUMP_GPIO_INTEN(3);
	DUMP_GPIO_INTEN(4);
	DUMP_GPIO_INTEN(6);
}

static int rk30_pm_enter(suspend_state_t state)
{
#ifdef CONFIG_DDR_TEST
	// memory tester
	if (ddr_debug != 2)
		ddr_testmode();
#endif

	// dump GPIO INTEN for debug
	dump_inten();

	sram_printch('0');
	flush_tlb_all();

	dsb();
	wfi();

	sram_printascii("0\n");

	dump_irq();

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
