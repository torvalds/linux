#include <linux/clockchips.h>
#include <linux/platform_device.h>
#include <asm/mach/time.h>
#include <asm/localtimer.h>
#include <asm/smp_twd.h>
#include <mach/io.h>
#include <mach/irqs.h>

#define TIMER_NAME	"rk_timer"
#define BASE		RK2928_TIMER0_BASE
#define OFFSET		0x20

static struct resource rk_timer_resources[] __initdata = {
	{
		.name   = "cs_base",
		.start  = (unsigned long) BASE + 1 * OFFSET,
		.flags  = IORESOURCE_MEM,
	}, {
		.name   = "cs_clk",
		.start  = (unsigned long) "timer1",
	}, {
		.name   = "cs_pclk",
		.start  = (unsigned long) "pclk_timer0",
	},

	{
		.name   = "ce_base0",
		.start  = (unsigned long) BASE + 0 * OFFSET,
		.flags  = IORESOURCE_MEM,
	}, {
		.name   = "ce_irq0",
		.start  = (unsigned long) IRQ_TIMER0,
		.flags  = IORESOURCE_IRQ,
	}, {
		.name   = "ce_clk0",
		.start  = (unsigned long) "timer0",
	}, {
		.name   = "ce_pclk0",
		.start  = (unsigned long) "pclk_timer0",
	},
};

static struct platform_device rk_timer_device __initdata = {
        .name           = TIMER_NAME,
        .id             = 0,
        .resource       = rk_timer_resources,
        .num_resources  = ARRAY_SIZE(rk_timer_resources),
};

static struct platform_device *rk_timer_devices[] __initdata = {
	&rk_timer_device,
};

static void __init rk_timer_init(void)
{
#ifdef CONFIG_HAVE_ARM_TWD
        twd_base = RK30_PTIMER_BASE;
#endif
	early_platform_add_devices(rk_timer_devices, ARRAY_SIZE(rk_timer_devices));
        early_platform_driver_register_all(TIMER_NAME);
        early_platform_driver_probe(TIMER_NAME, 1, 0);
}

struct sys_timer rk30_timer = {
	.init = rk_timer_init
};

struct sys_timer rk2928_timer = {
	.init = rk_timer_init
};

#ifdef CONFIG_LOCAL_TIMERS
/*
 * Setup the local clock events for a CPU.
 */
int __cpuinit local_timer_setup(struct clock_event_device *evt)
{
	evt->irq = IRQ_LOCALTIMER;
	twd_timer_setup(evt);
	evt->features &= ~CLOCK_EVT_FEAT_C3STOP;
	return 0;
}
#endif
