/*
 * linux/arch/arm/mach-sa1100/generic.c
 *
 * Author: Nicolas Pitre
 *
 * Code common to all SA11x0 machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/pm.h>
#include <linux/cpufreq.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

#include <video/sa1100fb.h>

#include <asm/div64.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/irq.h>
#include <asm/system_misc.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

#include "generic.h"
#include <clocksource/pxa.h>

unsigned int reset_status;
EXPORT_SYMBOL(reset_status);

#define NR_FREQS	16

/*
 * This table is setup for a 3.6864MHz Crystal.
 */
struct cpufreq_frequency_table sa11x0_freq_table[NR_FREQS+1] = {
	{ .frequency = 59000,	/*  59.0 MHz */},
	{ .frequency = 73700,	/*  73.7 MHz */},
	{ .frequency = 88500,	/*  88.5 MHz */},
	{ .frequency = 103200,	/* 103.2 MHz */},
	{ .frequency = 118000,	/* 118.0 MHz */},
	{ .frequency = 132700,	/* 132.7 MHz */},
	{ .frequency = 147500,	/* 147.5 MHz */},
	{ .frequency = 162200,	/* 162.2 MHz */},
	{ .frequency = 176900,	/* 176.9 MHz */},
	{ .frequency = 191700,	/* 191.7 MHz */},
	{ .frequency = 206400,	/* 206.4 MHz */},
	{ .frequency = 221200,	/* 221.2 MHz */},
	{ .frequency = 235900,	/* 235.9 MHz */},
	{ .frequency = 250700,	/* 250.7 MHz */},
	{ .frequency = 265400,	/* 265.4 MHz */},
	{ .frequency = 280200,	/* 280.2 MHz */},
	{ .frequency = CPUFREQ_TABLE_END, },
};

unsigned int sa11x0_getspeed(unsigned int cpu)
{
	if (cpu)
		return 0;
	return sa11x0_freq_table[PPCR & 0xf].frequency;
}

/*
 * Default power-off for SA1100
 */
static void sa1100_power_off(void)
{
	mdelay(100);
	local_irq_disable();
	/* disable internal oscillator, float CS lines */
	PCFR = (PCFR_OPDE | PCFR_FP | PCFR_FS);
	/* enable wake-up on GPIO0 (Assabet...) */
	PWER = GFER = GRER = 1;
	/*
	 * set scratchpad to zero, just in case it is used as a
	 * restart address by the bootloader.
	 */
	PSPR = 0;
	/* enter sleep mode */
	PMCR = PMCR_SF;
}

void sa11x0_restart(enum reboot_mode mode, const char *cmd)
{
	if (mode == REBOOT_SOFT) {
		/* Jump into ROM at address 0 */
		soft_restart(0);
	} else {
		/* Use on-chip reset capability */
		RSRR = RSRR_SWR;
	}
}

static void sa11x0_register_device(struct platform_device *dev, void *data)
{
	int err;
	dev->dev.platform_data = data;
	err = platform_device_register(dev);
	if (err)
		printk(KERN_ERR "Unable to register device %s: %d\n",
			dev->name, err);
}


static struct resource sa11x0udc_resources[] = {
	[0] = DEFINE_RES_MEM(__PREG(Ser0UDCCR), SZ_64K),
	[1] = DEFINE_RES_IRQ(IRQ_Ser0UDC),
};

static u64 sa11x0udc_dma_mask = 0xffffffffUL;

static struct platform_device sa11x0udc_device = {
	.name		= "sa11x0-udc",
	.id		= -1,
	.dev		= {
		.dma_mask = &sa11x0udc_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa11x0udc_resources),
	.resource	= sa11x0udc_resources,
};

static struct resource sa11x0uart1_resources[] = {
	[0] = DEFINE_RES_MEM(__PREG(Ser1UTCR0), SZ_64K),
	[1] = DEFINE_RES_IRQ(IRQ_Ser1UART),
};

static struct platform_device sa11x0uart1_device = {
	.name		= "sa11x0-uart",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(sa11x0uart1_resources),
	.resource	= sa11x0uart1_resources,
};

static struct resource sa11x0uart3_resources[] = {
	[0] = DEFINE_RES_MEM(__PREG(Ser3UTCR0), SZ_64K),
	[1] = DEFINE_RES_IRQ(IRQ_Ser3UART),
};

static struct platform_device sa11x0uart3_device = {
	.name		= "sa11x0-uart",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(sa11x0uart3_resources),
	.resource	= sa11x0uart3_resources,
};

static struct resource sa11x0mcp_resources[] = {
	[0] = DEFINE_RES_MEM(__PREG(Ser4MCCR0), SZ_64K),
	[1] = DEFINE_RES_MEM(__PREG(Ser4MCCR1), 4),
	[2] = DEFINE_RES_IRQ(IRQ_Ser4MCP),
};

static u64 sa11x0mcp_dma_mask = 0xffffffffUL;

static struct platform_device sa11x0mcp_device = {
	.name		= "sa11x0-mcp",
	.id		= -1,
	.dev = {
		.dma_mask = &sa11x0mcp_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa11x0mcp_resources),
	.resource	= sa11x0mcp_resources,
};

void __init sa11x0_ppc_configure_mcp(void)
{
	/* Setup the PPC unit for the MCP */
	PPDR &= ~PPC_RXD4;
	PPDR |= PPC_TXD4 | PPC_SCLK | PPC_SFRM;
	PSDR |= PPC_RXD4;
	PSDR &= ~(PPC_TXD4 | PPC_SCLK | PPC_SFRM);
	PPSR &= ~(PPC_TXD4 | PPC_SCLK | PPC_SFRM);
}

void sa11x0_register_mcp(struct mcp_plat_data *data)
{
	sa11x0_register_device(&sa11x0mcp_device, data);
}

static struct resource sa11x0ssp_resources[] = {
	[0] = DEFINE_RES_MEM(0x80070000, SZ_64K),
	[1] = DEFINE_RES_IRQ(IRQ_Ser4SSP),
};

static u64 sa11x0ssp_dma_mask = 0xffffffffUL;

static struct platform_device sa11x0ssp_device = {
	.name		= "sa11x0-ssp",
	.id		= -1,
	.dev = {
		.dma_mask = &sa11x0ssp_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa11x0ssp_resources),
	.resource	= sa11x0ssp_resources,
};

static struct resource sa11x0fb_resources[] = {
	[0] = DEFINE_RES_MEM(0xb0100000, SZ_64K),
	[1] = DEFINE_RES_IRQ(IRQ_LCD),
};

static struct platform_device sa11x0fb_device = {
	.name		= "sa11x0-fb",
	.id		= -1,
	.dev = {
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa11x0fb_resources),
	.resource	= sa11x0fb_resources,
};

void sa11x0_register_lcd(struct sa1100fb_mach_info *inf)
{
	sa11x0_register_device(&sa11x0fb_device, inf);
}

static struct platform_device sa11x0pcmcia_device = {
	.name		= "sa11x0-pcmcia",
	.id		= -1,
};

static struct platform_device sa11x0mtd_device = {
	.name		= "sa1100-mtd",
	.id		= -1,
};

void sa11x0_register_mtd(struct flash_platform_data *flash,
			 struct resource *res, int nr)
{
	flash->name = "sa1100";
	sa11x0mtd_device.resource = res;
	sa11x0mtd_device.num_resources = nr;
	sa11x0_register_device(&sa11x0mtd_device, flash);
}

static struct resource sa11x0ir_resources[] = {
	DEFINE_RES_MEM(__PREG(Ser2UTCR0), 0x24),
	DEFINE_RES_MEM(__PREG(Ser2HSCR0), 0x1c),
	DEFINE_RES_MEM(__PREG(Ser2HSCR2), 0x04),
	DEFINE_RES_IRQ(IRQ_Ser2ICP),
};

static struct platform_device sa11x0ir_device = {
	.name		= "sa11x0-ir",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(sa11x0ir_resources),
	.resource	= sa11x0ir_resources,
};

void sa11x0_register_irda(struct irda_platform_data *irda)
{
	sa11x0_register_device(&sa11x0ir_device, irda);
}

static struct resource sa1100_rtc_resources[] = {
	DEFINE_RES_MEM(0x90010000, 0x40),
	DEFINE_RES_IRQ_NAMED(IRQ_RTC1Hz, "rtc 1Hz"),
	DEFINE_RES_IRQ_NAMED(IRQ_RTCAlrm, "rtc alarm"),
};

static struct platform_device sa11x0rtc_device = {
	.name		= "sa1100-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(sa1100_rtc_resources),
	.resource	= sa1100_rtc_resources,
};

static struct resource sa11x0dma_resources[] = {
	DEFINE_RES_MEM(DMA_PHYS, DMA_SIZE),
	DEFINE_RES_IRQ(IRQ_DMA0),
	DEFINE_RES_IRQ(IRQ_DMA1),
	DEFINE_RES_IRQ(IRQ_DMA2),
	DEFINE_RES_IRQ(IRQ_DMA3),
	DEFINE_RES_IRQ(IRQ_DMA4),
	DEFINE_RES_IRQ(IRQ_DMA5),
};

static u64 sa11x0dma_dma_mask = DMA_BIT_MASK(32);

static struct platform_device sa11x0dma_device = {
	.name		= "sa11x0-dma",
	.id		= -1,
	.dev = {
		.dma_mask = &sa11x0dma_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa11x0dma_resources),
	.resource	= sa11x0dma_resources,
};

static struct platform_device *sa11x0_devices[] __initdata = {
	&sa11x0udc_device,
	&sa11x0uart1_device,
	&sa11x0uart3_device,
	&sa11x0ssp_device,
	&sa11x0pcmcia_device,
	&sa11x0rtc_device,
	&sa11x0dma_device,
};

static int __init sa1100_init(void)
{
	pm_power_off = sa1100_power_off;
	return platform_add_devices(sa11x0_devices, ARRAY_SIZE(sa11x0_devices));
}

arch_initcall(sa1100_init);

void __init sa11x0_init_late(void)
{
	sa11x0_pm_init();
}

/*
 * Common I/O mapping:
 *
 * Typically, static virtual address mappings are as follow:
 *
 * 0xf0000000-0xf3ffffff:	miscellaneous stuff (CPLDs, etc.)
 * 0xf4000000-0xf4ffffff:	SA-1111
 * 0xf5000000-0xf5ffffff:	reserved (used by cache flushing area)
 * 0xf6000000-0xfffeffff:	reserved (internal SA1100 IO defined above)
 * 0xffff0000-0xffff0fff:	SA1100 exception vectors
 * 0xffff2000-0xffff2fff:	Minicache copy_user_page area
 *
 * Below 0xe8000000 is reserved for vm allocation.
 *
 * The machine specific code must provide the extra mapping beside the
 * default mapping provided here.
 */

static struct map_desc standard_io_desc[] __initdata = {
	{	/* PCM */
		.virtual	=  0xf8000000,
		.pfn		= __phys_to_pfn(0x80000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* SCM */
		.virtual	=  0xfa000000,
		.pfn		= __phys_to_pfn(0x90000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* MER */
		.virtual	=  0xfc000000,
		.pfn		= __phys_to_pfn(0xa0000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* LCD + DMA */
		.virtual	=  0xfe000000,
		.pfn		= __phys_to_pfn(0xb0000000),
		.length		= 0x00200000,
		.type		= MT_DEVICE
	},
};

void __init sa1100_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
}

void __init sa1100_timer_init(void)
{
	pxa_timer_nodt_init(IRQ_OST0, io_p2v(0x90000000), 3686400);
}

/*
 * Disable the memory bus request/grant signals on the SA1110 to
 * ensure that we don't receive spurious memory requests.  We set
 * the MBGNT signal false to ensure the SA1111 doesn't own the
 * SDRAM bus.
 */
void sa1110_mb_disable(void)
{
	unsigned long flags;

	local_irq_save(flags);
	
	PGSR &= ~GPIO_MBGNT;
	GPCR = GPIO_MBGNT;
	GPDR = (GPDR & ~GPIO_MBREQ) | GPIO_MBGNT;

	GAFR &= ~(GPIO_MBGNT | GPIO_MBREQ);

	local_irq_restore(flags);
}

/*
 * If the system is going to use the SA-1111 DMA engines, set up
 * the memory bus request/grant pins.
 */
void sa1110_mb_enable(void)
{
	unsigned long flags;

	local_irq_save(flags);

	PGSR &= ~GPIO_MBGNT;
	GPCR = GPIO_MBGNT;
	GPDR = (GPDR & ~GPIO_MBREQ) | GPIO_MBGNT;

	GAFR |= (GPIO_MBGNT | GPIO_MBREQ);
	TUCR |= TUCR_MR;

	local_irq_restore(flags);
}

