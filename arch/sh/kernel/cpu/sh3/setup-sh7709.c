/*
 * SH7707/SH7709 Setup
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
	[0] =	{
		.start	= 0xfffffec0,
		.end	= 0xfffffec0 + 0x1e,
		.flags  = IORESOURCE_IO,
	},
	[1] =	{
		.start  = 20,
		.flags	= IORESOURCE_IRQ,
	},
	[2] =	{
		.start	= 21,
		.flags	= IORESOURCE_IRQ,
	},
	[3] =	{
		.start	= 22,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xfffffe80,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCI,
		.irqs		= { 23, 24, 25, 0 },
	}, {
		.mapbase	= 0xa4000150,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 56, 57, 59, 58 },
	}, {
		.mapbase	= 0xa4000140,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_IRDA,
		.irqs		= { 52, 53, 55, 54 },
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

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static struct platform_device *sh7709_devices[] __initdata = {
	&sci_device,
	&rtc_device,
};

static int __init sh7709_devices_setup(void)
{
	return platform_add_devices(sh7709_devices,
		ARRAY_SIZE(sh7709_devices));
}
__initcall(sh7709_devices_setup);

static struct ipr_data ipr_irq_table[] = {
	{ 16, 0, 12, 2 }, /* TMU TUNI0 */
	{ 17, 0, 8,  4 }, /* TMU TUNI1 */
	{ 18, 0, 4,  1 }, /* TMU TUNI1 */
	{ 19, 0, 4,  1 }, /* TMU TUNI1 */
	{ 20, 0, 0,  2 }, /* RTC CUI */
	{ 21, 0, 0,  2 }, /* RTC CUI */
	{ 22, 0, 0,  2 }, /* RTC CUI */

	{ 23, 1, 4,  3 }, /* SCI */
	{ 24, 1, 4,  3 }, /* SCI */
	{ 25, 1, 4,  3 }, /* SCI */
	{ 26, 1, 4,  3 }, /* SCI */
	{ 27, 1, 12, 3 }, /* WDT ITI */

	{ 32, 2, 0,  1 }, /* IRQ 0 */
	{ 33, 2, 4,  1 }, /* IRQ 1 */
	{ 34, 2, 8,  1 }, /* IRQ 2 APM */
	{ 35, 2, 12, 1 }, /* IRQ 3 TOUCHSCREEN */

	{ 36, 3, 0,  1 }, /* IRQ 4 */
	{ 37, 3, 4,  1 }, /* IRQ 5 */

	{ 48, 4, 12, 7 }, /* DMA */
	{ 49, 4, 12, 7 }, /* DMA */
	{ 50, 4, 12, 7 }, /* DMA */
	{ 51, 4, 12, 7 }, /* DMA */

	{ 52, 4, 8,  3 }, /* IRDA */
	{ 53, 4, 8,  3 }, /* IRDA */
	{ 54, 4, 8,  3 }, /* IRDA */
	{ 55, 4, 8,  3 }, /* IRDA */

	{ 56, 4, 4,  3 }, /* SCIF */
	{ 57, 4, 4,  3 }, /* SCIF */
	{ 58, 4, 4,  3 }, /* SCIF */
	{ 59, 4, 4,  3 }, /* SCIF */
};

static unsigned long ipr_offsets[] = {
	0xfffffee2,	/* 0: IPRA */
	0xfffffee4,	/* 1: IPRB */
	0xa4000016,	/* 2: IPRC */
	0xa4000018,	/* 3: IPRD */
	0xa400001a,	/* 4: IPRE */
};

static struct ipr_desc ipr_irq_desc = {
	.ipr_offsets	= ipr_offsets,
	.nr_offsets	= ARRAY_SIZE(ipr_offsets),

	.ipr_data	= ipr_irq_table,
	.nr_irqs	= ARRAY_SIZE(ipr_irq_table),

	.chip = {
		.name	= "IPR-sh7709",
	},
};

void __init plat_irq_setup(void)
{
	register_ipr_controller(&ipr_irq_desc);
}
