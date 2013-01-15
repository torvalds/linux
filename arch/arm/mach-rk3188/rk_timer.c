#include <linux/platform_device.h>
#include <asm/mach/time.h>
#include <mach/io.h>
#include <mach/irqs.h>

#define TIMER_NAME	"rk_timer"
#define BASE		RK30_TIMER0_BASE
#define OFFSET		0x20

static struct resource rk_timer_resources[] __initdata = {
	{
		.name   = "cs_base",
		.start  = (unsigned long) BASE + 5 * OFFSET,
		.flags  = IORESOURCE_MEM,
	}, {
		.name   = "cs_clk",
		.start  = (unsigned long) "timer6",
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

	{
		.name   = "ce_base1",
		.start  = (unsigned long) BASE + 1 * OFFSET,
		.flags  = IORESOURCE_MEM,
	}, {
		.name   = "ce_irq1",
		.start  = (unsigned long) IRQ_TIMER1,
		.flags  = IORESOURCE_IRQ,
	}, {
		.name   = "ce_clk1",
		.start  = (unsigned long) "timer1",
	}, {
		.name   = "ce_pclk1",
		.start  = (unsigned long) "pclk_timer0",
	},

	{
		.name   = "ce_base2",
		.start  = (unsigned long) BASE + 3 * OFFSET,
		.flags  = IORESOURCE_MEM,
	}, {
		.name   = "ce_irq2",
		.start  = (unsigned long) IRQ_TIMER4,
		.flags  = IORESOURCE_IRQ,
	}, {
		.name   = "ce_clk2",
		.start  = (unsigned long) "timer4",
	}, {
		.name   = "ce_pclk2",
		.start  = (unsigned long) "pclk_timer0",
	},

	{
		.name   = "ce_base3",
		.start  = (unsigned long) BASE + 4 * OFFSET,
		.flags  = IORESOURCE_MEM,
	}, {
		.name   = "ce_irq3",
		.start  = (unsigned long) IRQ_TIMER5,
		.flags  = IORESOURCE_IRQ,
	}, {
		.name   = "ce_clk3",
		.start  = (unsigned long) "timer5",
	}, {
		.name   = "ce_pclk3",
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
	early_platform_add_devices(rk_timer_devices, ARRAY_SIZE(rk_timer_devices));
        early_platform_driver_register_all(TIMER_NAME);
        early_platform_driver_probe(TIMER_NAME, 1, 0);
}

struct sys_timer rk30_timer = {
	.init = rk_timer_init
};
