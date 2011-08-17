/*
 *  derived from linux/arch/arm/mach-versatile/core.c
 *  linux/arch/arm/mach-bcmring/core.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/* Portions copyright Broadcom 2008 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/clkdev.h>

#include <mach/csp/mm_addr.h>
#include <mach/hardware.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/timer-sp.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include <cfg_global.h>

#include "clock.h"

#include <csp/secHw.h>
#include <mach/csp/secHw_def.h>
#include <mach/csp/chipcHw_inline.h>
#include <mach/csp/tmrHw_reg.h>

#define AMBA_DEVICE(name, initname, base, plat, size)       \
static struct amba_device name##_device = {     \
   .dev = {                                     \
      .coherent_dma_mask = ~0,                  \
      .init_name = initname,                    \
      .platform_data = plat                     \
   },                                           \
   .res = {                                     \
      .start = MM_ADDR_IO_##base,               \
		.end = MM_ADDR_IO_##base + (size) - 1,    \
      .flags = IORESOURCE_MEM                   \
   },                                           \
   .dma_mask = ~0,                              \
   .irq = {                                     \
      IRQ_##base                                \
   }                                            \
}


AMBA_DEVICE(uartA, "uarta", UARTA, NULL, SZ_4K);
AMBA_DEVICE(uartB, "uartb", UARTB, NULL, SZ_4K);

static struct clk pll1_clk = {
	.name = "PLL1",
	.type = CLK_TYPE_PRIMARY | CLK_TYPE_PLL1,
	.rate_hz = 2000000000,
	.use_cnt = 7,
};

static struct clk uart_clk = {
	.name = "UART",
	.type = CLK_TYPE_PROGRAMMABLE,
	.csp_id = chipcHw_CLOCK_UART,
	.rate_hz = HW_CFG_UART_CLK_HZ,
	.parent = &pll1_clk,
};

static struct clk dummy_apb_pclk = {
	.name = "BUSCLK",
	.type = CLK_TYPE_PRIMARY,
	.mode = CLK_MODE_XTAL,
};

/* Timer 0 - 25 MHz, Timer3 at bus clock rate, typically  150-166 MHz */
#if defined(CONFIG_ARCH_FPGA11107)
/* fpga cpu/bus are currently 30 times slower so scale frequency as well to */
/* slow down Linux's sense of time */
#define TIMER0_FREQUENCY_MHZ  (tmrHw_LOW_FREQUENCY_MHZ * 30)
#define TIMER1_FREQUENCY_MHZ  (tmrHw_LOW_FREQUENCY_MHZ * 30)
#define TIMER3_FREQUENCY_MHZ  (tmrHw_HIGH_FREQUENCY_MHZ * 30)
#define TIMER3_FREQUENCY_KHZ   (tmrHw_HIGH_FREQUENCY_HZ / 1000 * 30)
#else
#define TIMER0_FREQUENCY_MHZ  tmrHw_LOW_FREQUENCY_MHZ
#define TIMER1_FREQUENCY_MHZ  tmrHw_LOW_FREQUENCY_MHZ
#define TIMER3_FREQUENCY_MHZ  tmrHw_HIGH_FREQUENCY_MHZ
#define TIMER3_FREQUENCY_KHZ  (tmrHw_HIGH_FREQUENCY_HZ / 1000)
#endif

static struct clk sp804_timer012_clk = {
	.name = "sp804-timer-0,1,2",
	.type = CLK_TYPE_PRIMARY,
	.mode = CLK_MODE_XTAL,
	.rate_hz = TIMER1_FREQUENCY_MHZ * 1000000,
};

static struct clk sp804_timer3_clk = {
	.name = "sp804-timer-3",
	.type = CLK_TYPE_PRIMARY,
	.mode = CLK_MODE_XTAL,
	.rate_hz = TIMER3_FREQUENCY_KHZ * 1000,
};

static struct clk_lookup lookups[] = {
	{			/* Bus clock */
		.con_id = "apb_pclk",
		.clk = &dummy_apb_pclk,
	}, {			/* UART0 */
		.dev_id = "uarta",
		.clk = &uart_clk,
	}, {			/* UART1 */
		.dev_id = "uartb",
		.clk = &uart_clk,
	}, {			/* SP804 timer 0 */
		.dev_id = "sp804",
		.con_id = "timer0",
		.clk = &sp804_timer012_clk,
	}, {			/* SP804 timer 1 */
		.dev_id = "sp804",
		.con_id = "timer1",
		.clk = &sp804_timer012_clk,
	}, {			/* SP804 timer 3 */
		.dev_id = "sp804",
		.con_id = "timer3",
		.clk = &sp804_timer3_clk,
	}
};

static struct amba_device *amba_devs[] __initdata = {
	&uartA_device,
	&uartB_device,
};

void __init bcmring_amba_init(void)
{
	int i;
	u32 bus_clock;

/* Linux is run initially in non-secure mode. Secure peripherals */
/* generate FIQ, and must be handled in secure mode. Until we have */
/* a linux security monitor implementation, keep everything in */
/* non-secure mode. */
	chipcHw_busInterfaceClockEnable(chipcHw_REG_BUS_CLOCK_SPU);
	secHw_setUnsecure(secHw_BLK_MASK_CHIP_CONTROL |
			  secHw_BLK_MASK_KEY_SCAN |
			  secHw_BLK_MASK_TOUCH_SCREEN |
			  secHw_BLK_MASK_UART0 |
			  secHw_BLK_MASK_UART1 |
			  secHw_BLK_MASK_WATCHDOG |
			  secHw_BLK_MASK_SPUM |
			  secHw_BLK_MASK_DDR2 |
			  secHw_BLK_MASK_SPU |
			  secHw_BLK_MASK_PKA |
			  secHw_BLK_MASK_RNG |
			  secHw_BLK_MASK_RTC |
			  secHw_BLK_MASK_OTP |
			  secHw_BLK_MASK_BOOT |
			  secHw_BLK_MASK_MPU |
			  secHw_BLK_MASK_TZCTRL | secHw_BLK_MASK_INTR);

	/* Only the devices attached to the AMBA bus are enabled just before the bus is */
	/* scanned and the drivers are loaded. The clocks need to be on for the AMBA bus */
	/* driver to access these blocks. The bus is probed, and the drivers are loaded. */
	/* FIXME Need to remove enable of PIF once CLCD clock enable used properly in FPGA. */
	bus_clock = chipcHw_REG_BUS_CLOCK_GE
	    | chipcHw_REG_BUS_CLOCK_SDIO0 | chipcHw_REG_BUS_CLOCK_SDIO1;

	chipcHw_busInterfaceClockEnable(bus_clock);

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}
}

/*
 * Where is the timer (VA)?
 */
#define TIMER0_VA_BASE		((void __iomem *)MM_IO_BASE_TMR)
#define TIMER1_VA_BASE		((void __iomem *)(MM_IO_BASE_TMR + 0x20))
#define TIMER2_VA_BASE		((void __iomem *)(MM_IO_BASE_TMR + 0x40))
#define TIMER3_VA_BASE          ((void __iomem *)(MM_IO_BASE_TMR + 0x60))

static int __init bcmring_clocksource_init(void)
{
	/* setup timer1 as free-running clocksource */
	sp804_clocksource_init(TIMER1_VA_BASE, "timer1");

	/* setup timer3 as free-running clocksource */
	sp804_clocksource_init(TIMER3_VA_BASE, "timer3");

	return 0;
}

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init bcmring_init_timer(void)
{
	printk(KERN_INFO "bcmring_init_timer\n");
	/*
	 * Initialise to a known state (all timers off)
	 */
	writel(0, TIMER0_VA_BASE + TIMER_CTRL);
	writel(0, TIMER1_VA_BASE + TIMER_CTRL);
	writel(0, TIMER2_VA_BASE + TIMER_CTRL);
	writel(0, TIMER3_VA_BASE + TIMER_CTRL);

	/*
	 * Make irqs happen for the system timer
	 */
	bcmring_clocksource_init();

	sp804_clockevents_register(TIMER0_VA_BASE, IRQ_TIMER0, "timer0");
}

struct sys_timer bcmring_timer = {
	.init = bcmring_init_timer,
};

void __init bcmring_init_early(void)
{
	clkdev_add_table(lookups, ARRAY_SIZE(lookups));
}
