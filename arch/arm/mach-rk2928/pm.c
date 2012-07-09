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

#include <mach/system.h>
#include <mach/sram.h>
#include <mach/gpio.h>

void __sramfunc sram_printch(char byte)
{
#ifdef DEBUG_UART_BASE
	writel_relaxed(byte, DEBUG_UART_BASE);
	dsb();

	/* loop check LSR[6], Transmitter Empty bit */
	while (!(readl_relaxed(DEBUG_UART_BASE + 0x14) & 0x40))
		barrier();

	if (byte == '\n')
		sram_printch('\r');
#endif
}

