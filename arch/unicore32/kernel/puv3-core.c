/*
 *  linux/arch/unicore32/kernel/puv3-core.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/amba/bus.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/cnt32_to_63.h>
#include <linux/usb/musb.h>

#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/pm.h>

/*
 * This is the PKUnity sched_clock implementation.  This has
 * a resolution of 271ns, and a maximum value of 32025597s (370 days).
 *
 * The return value is guaranteed to be monotonic in that range as
 * long as there is always less than 582 seconds between successive
 * calls to this function.
 *
 *  ( * 1E9 / CLOCK_TICK_RATE ) -> about 2235/32
 */
unsigned long long sched_clock(void)
{
	unsigned long long v = cnt32_to_63(readl(OST_OSCR));

	/* original conservative method, but overflow frequently
	 * v *= NSEC_PER_SEC >> 12;
	 * do_div(v, CLOCK_TICK_RATE >> 12);
	 */
	v = ((v & 0x7fffffffffffffffULL) * 2235) >> 5;

	return v;
}

static struct resource puv3_usb_resources[] = {
	/* order is significant! */
	{
		.start		= io_v2p(PKUNITY_USB_BASE),
		.end		= io_v2p(PKUNITY_USB_BASE) + 0x3ff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_USB,
		.flags		= IORESOURCE_IRQ,
	}, {
		.start		= IRQ_USB,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct musb_hdrc_config	puv3_usb_config[] = {
	{
		.num_eps = 16,
		.multipoint = 1,
#ifdef CONFIG_USB_INVENTRA_DMA
		.dma = 1,
		.dma_channels = 8,
#endif
	},
};

static struct musb_hdrc_platform_data puv3_usb_plat = {
	.mode		= MUSB_HOST,
	.min_power	= 100,
	.clock		= 0,
	.config		= puv3_usb_config,
};

static struct resource puv3_mmc_resources[] = {
	[0] = {
		.start	= io_v2p(PKUNITY_SDC_BASE),
		.end	= io_v2p(PKUNITY_SDC_BASE) + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SDC,
		.end	= IRQ_SDC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource puv3_unigfx_resources[] = {
	[0] = {
		.start	= io_v2p(PKUNITY_UNIGFX_BASE),
		.end	= io_v2p(PKUNITY_UNIGFX_BASE) + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource puv3_rtc_resources[] = {
	[0] = {
		.start = io_v2p(PKUNITY_RTC_BASE),
		.end   = io_v2p(PKUNITY_RTC_BASE) + 0xff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_RTCAlarm,
		.end   = IRQ_RTCAlarm,
		.flags = IORESOURCE_IRQ,
	},
	[2] = {
		.start = IRQ_RTC,
		.end   = IRQ_RTC,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource puv3_pwm_resources[] = {
	[0] = {
		.start	= io_v2p(PKUNITY_OST_BASE) + 0x80,
		.end	= io_v2p(PKUNITY_OST_BASE) + 0xff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource puv3_uart0_resources[] = {
	[0] = {
		.start = io_v2p(PKUNITY_UART0_BASE),
		.end   = io_v2p(PKUNITY_UART0_BASE) + 0xff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_UART0,
		.end   = IRQ_UART0,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource puv3_uart1_resources[] = {
	[0] = {
		.start = io_v2p(PKUNITY_UART1_BASE),
		.end   = io_v2p(PKUNITY_UART1_BASE) + 0xff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_UART1,
		.end   = IRQ_UART1,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource puv3_umal_resources[] = {
	[0] = {
		.start = io_v2p(PKUNITY_UMAL_BASE),
		.end   = io_v2p(PKUNITY_UMAL_BASE) + 0x1fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_UMAL,
		.end   = IRQ_UMAL,
		.flags = IORESOURCE_IRQ
	}
};

#ifdef CONFIG_PUV3_PM

#define SAVE(x)		sleep_save[SLEEP_SAVE_##x] = x
#define RESTORE(x)	x = sleep_save[SLEEP_SAVE_##x]

/*
 * List of global PXA peripheral registers to preserve.
 * More ones like CP and general purpose register values are preserved
 * with the stack pointer in sleep.S.
 */
enum {
	SLEEP_SAVE_PM_PLLDDRCFG,
	SLEEP_SAVE_COUNT
};


static void puv3_cpu_pm_save(unsigned long *sleep_save)
{
/*	SAVE(PM_PLLDDRCFG); */
}

static void puv3_cpu_pm_restore(unsigned long *sleep_save)
{
/*	RESTORE(PM_PLLDDRCFG); */
}

static int puv3_cpu_pm_prepare(void)
{
	/* set resume return address */
	writel(virt_to_phys(puv3_cpu_resume), PM_DIVCFG);
	return 0;
}

static void puv3_cpu_pm_enter(suspend_state_t state)
{
	/* Clear reset status */
	writel(RESETC_RSSR_HWR | RESETC_RSSR_WDR
			| RESETC_RSSR_SMR | RESETC_RSSR_SWR, RESETC_RSSR);

	switch (state) {
/*	case PM_SUSPEND_ON:
		puv3_cpu_idle();
		break; */
	case PM_SUSPEND_MEM:
		puv3_cpu_pm_prepare();
		puv3_cpu_suspend(PM_PMCR_SFB);
		break;
	}
}

static int puv3_cpu_pm_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM;
}

static void puv3_cpu_pm_finish(void)
{
	/* ensure not to come back here if it wasn't intended */
	/* PSPR = 0; */
}

static struct puv3_cpu_pm_fns puv3_cpu_pm_fnss = {
	.save_count	= SLEEP_SAVE_COUNT,
	.valid		= puv3_cpu_pm_valid,
	.save		= puv3_cpu_pm_save,
	.restore	= puv3_cpu_pm_restore,
	.enter		= puv3_cpu_pm_enter,
	.prepare	= puv3_cpu_pm_prepare,
	.finish		= puv3_cpu_pm_finish,
};

static void __init puv3_init_pm(void)
{
	puv3_cpu_pm_fns = &puv3_cpu_pm_fnss;
}
#else
static inline void puv3_init_pm(void) {}
#endif

void puv3_ps2_init(void)
{
	struct clk *bclk32;

	bclk32 = clk_get(NULL, "BUS32_CLK");
	writel(clk_get_rate(bclk32) / 200000, PS2_CNT); /* should > 5us */
}

void __init puv3_core_init(void)
{
	puv3_init_pm();
	puv3_ps2_init();

	platform_device_register_simple("PKUnity-v3-RTC", -1,
			puv3_rtc_resources, ARRAY_SIZE(puv3_rtc_resources));
	platform_device_register_simple("PKUnity-v3-UMAL", -1,
			puv3_umal_resources, ARRAY_SIZE(puv3_umal_resources));
	platform_device_register_simple("PKUnity-v3-MMC", -1,
			puv3_mmc_resources, ARRAY_SIZE(puv3_mmc_resources));
	platform_device_register_simple("PKUnity-v3-UNIGFX", -1,
			puv3_unigfx_resources, ARRAY_SIZE(puv3_unigfx_resources));
	platform_device_register_simple("PKUnity-v3-PWM", -1,
			puv3_pwm_resources, ARRAY_SIZE(puv3_pwm_resources));
	platform_device_register_simple("PKUnity-v3-UART", 0,
			puv3_uart0_resources, ARRAY_SIZE(puv3_uart0_resources));
	platform_device_register_simple("PKUnity-v3-UART", 1,
			puv3_uart1_resources, ARRAY_SIZE(puv3_uart1_resources));
	platform_device_register_simple("PKUnity-v3-AC97", -1, NULL, 0);
	platform_device_register_resndata(&platform_bus, "musb_hdrc", -1,
			puv3_usb_resources, ARRAY_SIZE(puv3_usb_resources),
			&puv3_usb_plat, sizeof(puv3_usb_plat));
}

