/*
 * SH7780 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <asm/sci.h>

static struct resource rtc_resources[] = {
	[0] = {
		.start	= 0xffe80000,
		.end	= 0xffe80000 + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Period IRQ */
		.start	= 21,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* Carry IRQ */
		.start	= 22,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		/* Alarm IRQ */
		.start	= 23,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xffe00000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 40, 41, 43, 42 },
	}, {
		.mapbase	= 0xffe10000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 76, 77, 79, 78 },
	}, {
		.flags = 0,
	}
};

static struct platform_device sci_device = {
	.name		= "sh-sci",
	.id		= -1,
	.dev		= {
		.platform_data	= sci_platform_data,
	},
};

static struct platform_device *sh7780_devices[] __initdata = {
	&rtc_device,
	&sci_device,
};

static int __init sh7780_devices_setup(void)
{
	return platform_add_devices(sh7780_devices,
				    ARRAY_SIZE(sh7780_devices));
}
__initcall(sh7780_devices_setup);

static struct intc2_data intc2_irq_table[] = {
	{ TIMER_IRQ, 0, 24, 0, INTC_TMU0_MSK, 2 },
	{ 21, 1, 0, 0, INTC_RTC_MSK, TIMER_PRIORITY },
	{ 22, 1, 1, 0, INTC_RTC_MSK, TIMER_PRIORITY },
	{ 23, 1, 2, 0, INTC_RTC_MSK, TIMER_PRIORITY },
	{ SCIF0_ERI_IRQ, 8, 24, 0, INTC_SCIF0_MSK, SCIF0_PRIORITY },
	{ SCIF0_RXI_IRQ, 8, 24, 0, INTC_SCIF0_MSK, SCIF0_PRIORITY },
	{ SCIF0_BRI_IRQ, 8, 24, 0, INTC_SCIF0_MSK, SCIF0_PRIORITY },
	{ SCIF0_TXI_IRQ, 8, 24, 0, INTC_SCIF0_MSK, SCIF0_PRIORITY },

	{ SCIF1_ERI_IRQ, 8, 16, 0, INTC_SCIF1_MSK, SCIF1_PRIORITY },
	{ SCIF1_RXI_IRQ, 8, 16, 0, INTC_SCIF1_MSK, SCIF1_PRIORITY },
	{ SCIF1_BRI_IRQ, 8, 16, 0, INTC_SCIF1_MSK, SCIF1_PRIORITY },
	{ SCIF1_TXI_IRQ, 8, 16, 0, INTC_SCIF1_MSK, SCIF1_PRIORITY },

	{ PCIC0_IRQ, 0x10,  8, 0, INTC_PCIC0_MSK, PCIC0_PRIORITY },
	{ PCIC1_IRQ, 0x10,  0, 0, INTC_PCIC1_MSK, PCIC1_PRIORITY },
	{ PCIC2_IRQ, 0x14, 24, 0, INTC_PCIC2_MSK, PCIC2_PRIORITY },
	{ PCIC3_IRQ, 0x14, 16, 0, INTC_PCIC3_MSK, PCIC3_PRIORITY },
	{ PCIC4_IRQ, 0x14,  8, 0, INTC_PCIC4_MSK, PCIC4_PRIORITY },
};

void __init init_IRQ_intc2(void)
{
	make_intc2_irq(intc2_irq_table, ARRAY_SIZE(intc2_irq_table));
}
