/*
 *  linux/arch/m32r/kernel/setup_mappi.c
 *
 *  Setup routines for Renesas MAPPI Board
 *
 *  Copyright (c) 2001-2005  Hiroyuki Kondo, Hirokazu Takata,
 *                           Hitoshi Yamamoto
 */

#include <linux/config.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/system.h>
#include <asm/m32r.h>
#include <asm/io.h>

#define irq2port(x) (M32R_ICU_CR1_PORTL + ((x - 1) * sizeof(unsigned long)))

#ifndef CONFIG_SMP
typedef struct {
	unsigned long icucr;  /* ICU Control Register */
} icu_data_t;
#endif /* CONFIG_SMP */

icu_data_t icu_data[NR_IRQS];

static void disable_mappi_irq(unsigned int irq)
{
	unsigned long port, data;

	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_ILEVEL7;
	outl(data, port);
}

static void enable_mappi_irq(unsigned int irq)
{
	unsigned long port, data;

	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_IEN|M32R_ICUCR_ILEVEL6;
	outl(data, port);
}

static void mask_and_ack_mappi(unsigned int irq)
{
	disable_mappi_irq(irq);
}

static void end_mappi_irq(unsigned int irq)
{
	enable_mappi_irq(irq);
}

static unsigned int startup_mappi_irq(unsigned int irq)
{
	enable_mappi_irq(irq);
	return (0);
}

static void shutdown_mappi_irq(unsigned int irq)
{
	unsigned long port;

	port = irq2port(irq);
	outl(M32R_ICUCR_ILEVEL7, port);
}

static struct hw_interrupt_type mappi_irq_type =
{
	.typename = "MAPPI-IRQ",
	.startup = startup_mappi_irq,
	.shutdown = shutdown_mappi_irq,
	.enable = enable_mappi_irq,
	.disable = disable_mappi_irq,
	.ack = mask_and_ack_mappi,
	.end = end_mappi_irq
};

void __init init_IRQ(void)
{
	static int once = 0;

	if (once)
		return;
	else
		once++;

#ifdef CONFIG_NE2000
	/* INT0 : LAN controller (RTL8019AS) */
	irq_desc[M32R_IRQ_INT0].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_INT0].handler = &mappi_irq_type;
	irq_desc[M32R_IRQ_INT0].action = 0;
	irq_desc[M32R_IRQ_INT0].depth = 1;
	icu_data[M32R_IRQ_INT0].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD10;
	disable_mappi_irq(M32R_IRQ_INT0);
#endif /* CONFIG_M32R_NE2000 */

	/* MFT2 : system timer */
	irq_desc[M32R_IRQ_MFT2].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_MFT2].handler = &mappi_irq_type;
	irq_desc[M32R_IRQ_MFT2].action = 0;
	irq_desc[M32R_IRQ_MFT2].depth = 1;
	icu_data[M32R_IRQ_MFT2].icucr = M32R_ICUCR_IEN;
	disable_mappi_irq(M32R_IRQ_MFT2);

#ifdef CONFIG_SERIAL_M32R_SIO
	/* SIO0_R : uart receive data */
	irq_desc[M32R_IRQ_SIO0_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_R].handler = &mappi_irq_type;
	irq_desc[M32R_IRQ_SIO0_R].action = 0;
	irq_desc[M32R_IRQ_SIO0_R].depth = 1;
	icu_data[M32R_IRQ_SIO0_R].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO0_R);

	/* SIO0_S : uart send data */
	irq_desc[M32R_IRQ_SIO0_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_S].handler = &mappi_irq_type;
	irq_desc[M32R_IRQ_SIO0_S].action = 0;
	irq_desc[M32R_IRQ_SIO0_S].depth = 1;
	icu_data[M32R_IRQ_SIO0_S].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO0_S);

	/* SIO1_R : uart receive data */
	irq_desc[M32R_IRQ_SIO1_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_R].handler = &mappi_irq_type;
	irq_desc[M32R_IRQ_SIO1_R].action = 0;
	irq_desc[M32R_IRQ_SIO1_R].depth = 1;
	icu_data[M32R_IRQ_SIO1_R].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO1_R);

	/* SIO1_S : uart send data */
	irq_desc[M32R_IRQ_SIO1_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_S].handler = &mappi_irq_type;
	irq_desc[M32R_IRQ_SIO1_S].action = 0;
	irq_desc[M32R_IRQ_SIO1_S].depth = 1;
	icu_data[M32R_IRQ_SIO1_S].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO1_S);
#endif /* CONFIG_SERIAL_M32R_SIO */

#if defined(CONFIG_M32R_PCC)
	/* INT1 : pccard0 interrupt */
	irq_desc[M32R_IRQ_INT1].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_INT1].handler = &mappi_irq_type;
	irq_desc[M32R_IRQ_INT1].action = 0;
	irq_desc[M32R_IRQ_INT1].depth = 1;
	icu_data[M32R_IRQ_INT1].icucr = M32R_ICUCR_IEN | M32R_ICUCR_ISMOD00;
	disable_mappi_irq(M32R_IRQ_INT1);

	/* INT2 : pccard1 interrupt */
	irq_desc[M32R_IRQ_INT2].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_INT2].handler = &mappi_irq_type;
	irq_desc[M32R_IRQ_INT2].action = 0;
	irq_desc[M32R_IRQ_INT2].depth = 1;
	icu_data[M32R_IRQ_INT2].icucr = M32R_ICUCR_IEN | M32R_ICUCR_ISMOD00;
	disable_mappi_irq(M32R_IRQ_INT2);
#endif /* CONFIG_M32RPCC */
}

#if defined(CONFIG_FB_S1D13XXX)

#include <video/s1d13xxxfb.h>
#include <asm/s1d13806.h>

static struct s1d13xxxfb_pdata s1d13xxxfb_data = {
	.initregs		= s1d13xxxfb_initregs,
	.initregssize		= ARRAY_SIZE(s1d13xxxfb_initregs),
	.platform_init_video	= NULL,
#ifdef CONFIG_PM
	.platform_suspend_video	= NULL,
	.platform_resume_video	= NULL,
#endif
};

static struct resource s1d13xxxfb_resources[] = {
	[0] = {
		.start  = 0x10200000UL,
		.end    = 0x1033FFFFUL,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 0x10000000UL,
		.end    = 0x100001FFUL,
		.flags  = IORESOURCE_MEM,
	}
};

static struct platform_device s1d13xxxfb_device = {
	.name		= S1D_DEVICENAME,
	.id		= 0,
	.dev            = {
		.platform_data  = &s1d13xxxfb_data,
	},
	.num_resources  = ARRAY_SIZE(s1d13xxxfb_resources),
	.resource       = s1d13xxxfb_resources,
};

static int __init platform_init(void)
{
	platform_device_register(&s1d13xxxfb_device);
	return 0;
}
arch_initcall(platform_init);
#endif
