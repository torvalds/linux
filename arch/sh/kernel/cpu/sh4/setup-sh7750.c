/*
 * SH7750/SH7751 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 *  Copyright (C) 2006  Jamie Lenehan
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/io.h>
#include <asm/sci.h>

static struct resource rtc_resources[] = {
	[0] = {
		.start	= 0xffc80000,
		.end	= 0xffc80000 + 0x58 - 1,
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
		.start	= 20,
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
#ifndef CONFIG_SH_RTS7751R2D
		.mapbase	= 0xffe00000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCI,
		.irqs		= { 23, 24, 25, 0 },
	}, {
#endif
		.mapbase	= 0xffe80000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 40, 41, 43, 42 },
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

static struct platform_device *sh7750_devices[] __initdata = {
	&rtc_device,
	&sci_device,
};

static int __init sh7750_devices_setup(void)
{
	return platform_add_devices(sh7750_devices,
				    ARRAY_SIZE(sh7750_devices));
}
__initcall(sh7750_devices_setup);

static struct ipr_data ipr_irq_table[] = {
	/* IRQ, IPR-idx, shift, priority */
	{ 16, 0, 12, 2 }, /* TMU0 TUNI*/
	{ 17, 0, 12, 2 }, /* TMU1 TUNI */
	{ 18, 0,  4, 2 }, /* TMU2 TUNI */
	{ 19, 0,  4, 2 }, /* TMU2 TIPCI */
	{ 27, 1, 12, 2 }, /* WDT ITI */
	{ 20, 0,  0, 2 }, /* RTC ATI (alarm) */
	{ 21, 0,  0, 2 }, /* RTC PRI (period) */
	{ 22, 0,  0, 2 }, /* RTC CUI (carry) */
	{ 23, 1,  4, 3 }, /* SCI ERI */
	{ 24, 1,  4, 3 }, /* SCI RXI */
	{ 25, 1,  4, 3 }, /* SCI TXI */
	{ 40, 2,  4, 3 }, /* SCIF ERI */
	{ 41, 2,  4, 3 }, /* SCIF RXI */
	{ 42, 2,  4, 3 }, /* SCIF BRI */
	{ 43, 2,  4, 3 }, /* SCIF TXI */
	{ 34, 2,  8, 7 }, /* DMAC DMTE0 */
	{ 35, 2,  8, 7 }, /* DMAC DMTE1 */
	{ 36, 2,  8, 7 }, /* DMAC DMTE2 */
	{ 37, 2,  8, 7 }, /* DMAC DMTE3 */
	{ 38, 2,  8, 7 }, /* DMAC DMAE */
};

static unsigned long ipr_offsets[] = {
	0xffd00004UL,	/* 0: IPRA */
	0xffd00008UL,	/* 1: IPRB */
	0xffd0000cUL,	/* 2: IPRC */
	0xffd00010UL,	/* 3: IPRD */
};

static struct ipr_desc ipr_irq_desc = {
	.ipr_offsets	= ipr_offsets,
	.nr_offsets	= ARRAY_SIZE(ipr_offsets),

	.ipr_data	= ipr_irq_table,
	.nr_irqs	= ARRAY_SIZE(ipr_irq_table),

	.chip = {
		.name	= "IPR-sh7750",
	},
};

#ifdef CONFIG_CPU_SUBTYPE_SH7751
static struct ipr_data ipr_irq_table_sh7751[] = {
	{ 44, 2,  8, 7 }, /* DMAC DMTE4 */
	{ 45, 2,  8, 7 }, /* DMAC DMTE5 */
	{ 46, 2,  8, 7 }, /* DMAC DMTE6 */
	{ 47, 2,  8, 7 }, /* DMAC DMTE7 */
	/* The following use INTC_INPRI00 for masking, which is a 32-bit
	   register, not a 16-bit register like the IPRx registers, so it
	   would need special support */
	/*{ 72, INTPRI00,  8, ? },*/ /* TMU3 TUNI */
	/*{ 76, INTPRI00, 12, ? },*/ /* TMU4 TUNI */
};

static struct ipr_desc ipr_irq_desc_sh7751 = {
	.ipr_offsets	= ipr_offsets,
	.nr_offsets	= ARRAY_SIZE(ipr_offsets),

	.ipr_data	= ipr_irq_table_sh7751,
	.nr_irqs	= ARRAY_SIZE(ipr_irq_table_sh7751),

	.chip = {
		.name	= "IPR-sh7751",
	},
};
#endif

void __init init_IRQ_ipr(void)
{
	register_ipr_controller(&ipr_irq_desc);
#ifdef CONFIG_CPU_SUBTYPE_SH7751
	register_ipr_controller(&ipr_irq_desc_sh7751);
#endif
}

#define INTC_ICR	0xffd00000UL
#define INTC_ICR_IRLM   (1<<7)

/* enable individual interrupt mode for external interupts */
void ipr_irq_enable_irlm(void)
{
	ctrl_outw(ctrl_inw(INTC_ICR) | INTC_ICR_IRLM, INTC_ICR);
}
