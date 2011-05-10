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
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/clkdev.h>

#include <mach/csp/mm_addr.h>
#include <mach/hardware.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/hardware/arm_timer.h>
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

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}
}

/*
 * Where is the timer (VA)?
 */
#define TIMER0_VA_BASE		 MM_IO_BASE_TMR
#define TIMER1_VA_BASE		(MM_IO_BASE_TMR + 0x20)
#define TIMER2_VA_BASE		(MM_IO_BASE_TMR + 0x40)
#define TIMER3_VA_BASE          (MM_IO_BASE_TMR + 0x60)

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

#define TICKS_PER_uSEC     TIMER0_FREQUENCY_MHZ

/*
 *  These are useconds NOT ticks.
 *
 */
#define mSEC_1                          1000
#define mSEC_5                          (mSEC_1 * 5)
#define mSEC_10                         (mSEC_1 * 10)
#define mSEC_25                         (mSEC_1 * 25)
#define SEC_1                           (mSEC_1 * 1000)

/*
 * How long is the timer interval?
 */
#define TIMER_INTERVAL	(TICKS_PER_uSEC * mSEC_10)
#if TIMER_INTERVAL >= 0x100000
#define TIMER_RELOAD	(TIMER_INTERVAL >> 8)
#define TIMER_DIVISOR	(TIMER_CTRL_DIV256)
#define TICKS2USECS(x)	(256 * (x) / TICKS_PER_uSEC)
#elif TIMER_INTERVAL >= 0x10000
#define TIMER_RELOAD	(TIMER_INTERVAL >> 4)	/* Divide by 16 */
#define TIMER_DIVISOR	(TIMER_CTRL_DIV16)
#define TICKS2USECS(x)	(16 * (x) / TICKS_PER_uSEC)
#else
#define TIMER_RELOAD	(TIMER_INTERVAL)
#define TIMER_DIVISOR	(TIMER_CTRL_DIV1)
#define TICKS2USECS(x)	((x) / TICKS_PER_uSEC)
#endif

static void timer_set_mode(enum clock_event_mode mode,
			   struct clock_event_device *clk)
{
	unsigned long ctrl;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		writel(TIMER_RELOAD, TIMER0_VA_BASE + TIMER_LOAD);

		ctrl = TIMER_CTRL_PERIODIC;
		ctrl |=
		    TIMER_DIVISOR | TIMER_CTRL_32BIT | TIMER_CTRL_IE |
		    TIMER_CTRL_ENABLE;
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* period set, and timer enabled in 'next_event' hook */
		ctrl = TIMER_CTRL_ONESHOT;
		ctrl |= TIMER_DIVISOR | TIMER_CTRL_32BIT | TIMER_CTRL_IE;
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		ctrl = 0;
	}

	writel(ctrl, TIMER0_VA_BASE + TIMER_CTRL);
}

static int timer_set_next_event(unsigned long evt,
				struct clock_event_device *unused)
{
	unsigned long ctrl = readl(TIMER0_VA_BASE + TIMER_CTRL);

	writel(evt, TIMER0_VA_BASE + TIMER_LOAD);
	writel(ctrl | TIMER_CTRL_ENABLE, TIMER0_VA_BASE + TIMER_CTRL);

	return 0;
}

static struct clock_event_device timer0_clockevent = {
	.name = "timer0",
	.shift = 32,
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = timer_set_mode,
	.set_next_event = timer_set_next_event,
};

/*
 * IRQ handler for the timer
 */
static irqreturn_t bcmring_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &timer0_clockevent;

	writel(1, TIMER0_VA_BASE + TIMER_INTCLR);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction bcmring_timer_irq = {
	.name = "bcmring Timer Tick",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = bcmring_timer_interrupt,
};

static cycle_t bcmring_get_cycles_timer1(struct clocksource *cs)
{
	return ~readl(TIMER1_VA_BASE + TIMER_VALUE);
}

static cycle_t bcmring_get_cycles_timer3(struct clocksource *cs)
{
	return ~readl(TIMER3_VA_BASE + TIMER_VALUE);
}

static struct clocksource clocksource_bcmring_timer1 = {
	.name = "timer1",
	.rating = 200,
	.read = bcmring_get_cycles_timer1,
	.mask = CLOCKSOURCE_MASK(32),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct clocksource clocksource_bcmring_timer3 = {
	.name = "timer3",
	.rating = 100,
	.read = bcmring_get_cycles_timer3,
	.mask = CLOCKSOURCE_MASK(32),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init bcmring_clocksource_init(void)
{
	/* setup timer1 as free-running clocksource */
	writel(0, TIMER1_VA_BASE + TIMER_CTRL);
	writel(0xffffffff, TIMER1_VA_BASE + TIMER_LOAD);
	writel(0xffffffff, TIMER1_VA_BASE + TIMER_VALUE);
	writel(TIMER_CTRL_32BIT | TIMER_CTRL_ENABLE | TIMER_CTRL_PERIODIC,
	       TIMER1_VA_BASE + TIMER_CTRL);

	clocksource_register_khz(&clocksource_bcmring_timer1,
				 TIMER1_FREQUENCY_MHZ * 1000);

	/* setup timer3 as free-running clocksource */
	writel(0, TIMER3_VA_BASE + TIMER_CTRL);
	writel(0xffffffff, TIMER3_VA_BASE + TIMER_LOAD);
	writel(0xffffffff, TIMER3_VA_BASE + TIMER_VALUE);
	writel(TIMER_CTRL_32BIT | TIMER_CTRL_ENABLE | TIMER_CTRL_PERIODIC,
	       TIMER3_VA_BASE + TIMER_CTRL);

	clocksource_register_khz(&clocksource_bcmring_timer3,
				 TIMER3_FREQUENCY_KHZ);

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
	setup_irq(IRQ_TIMER0, &bcmring_timer_irq);

	bcmring_clocksource_init();

	timer0_clockevent.mult =
	    div_sc(1000000, NSEC_PER_SEC, timer0_clockevent.shift);
	timer0_clockevent.max_delta_ns =
	    clockevent_delta2ns(0xffffffff, &timer0_clockevent);
	timer0_clockevent.min_delta_ns =
	    clockevent_delta2ns(0xf, &timer0_clockevent);

	timer0_clockevent.cpumask = cpumask_of(0);
	clockevents_register_device(&timer0_clockevent);
}

struct sys_timer bcmring_timer = {
	.init = bcmring_init_timer,
};
