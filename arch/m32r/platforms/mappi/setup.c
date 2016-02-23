/*
 *  linux/arch/m32r/platforms/mappi/setup.c
 *
 *  Setup routines for Renesas MAPPI Board
 *
 *  Copyright (c) 2001-2005  Hiroyuki Kondo, Hirokazu Takata,
 *                           Hitoshi Yamamoto
 */

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/m32r.h>
#include <asm/io.h>

#define irq2port(x) (M32R_ICU_CR1_PORTL + ((x - 1) * sizeof(unsigned long)))

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

static void mask_mappi(struct irq_data *data)
{
	disable_mappi_irq(data->irq);
}

static void unmask_mappi(struct irq_data *data)
{
	enable_mappi_irq(data->irq);
}

static void shutdown_mappi(struct irq_data *data)
{
	unsigned long port;

	port = irq2port(data->irq);
	outl(M32R_ICUCR_ILEVEL7, port);
}

static struct irq_chip mappi_irq_type =
{
	.name		= "MAPPI-IRQ",
	.irq_shutdown	= shutdown_mappi,
	.irq_mask	= mask_mappi,
	.irq_unmask	= unmask_mappi,
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
	irq_set_chip_and_handler(M32R_IRQ_INT0, &mappi_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_INT0].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD11;
	disable_mappi_irq(M32R_IRQ_INT0);
#endif /* CONFIG_M32R_NE2000 */

	/* MFT2 : system timer */
	irq_set_chip_and_handler(M32R_IRQ_MFT2, &mappi_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_MFT2].icucr = M32R_ICUCR_IEN;
	disable_mappi_irq(M32R_IRQ_MFT2);

#ifdef CONFIG_SERIAL_M32R_SIO
	/* SIO0_R : uart receive data */
	irq_set_chip_and_handler(M32R_IRQ_SIO0_R, &mappi_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO0_R].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO0_R);

	/* SIO0_S : uart send data */
	irq_set_chip_and_handler(M32R_IRQ_SIO0_S, &mappi_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO0_S].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO0_S);

	/* SIO1_R : uart receive data */
	irq_set_chip_and_handler(M32R_IRQ_SIO1_R, &mappi_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO1_R].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO1_R);

	/* SIO1_S : uart send data */
	irq_set_chip_and_handler(M32R_IRQ_SIO1_S, &mappi_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO1_S].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO1_S);
#endif /* CONFIG_SERIAL_M32R_SIO */

#if defined(CONFIG_M32R_PCC)
	/* INT1 : pccard0 interrupt */
	irq_set_chip_and_handler(M32R_IRQ_INT1, &mappi_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_INT1].icucr = M32R_ICUCR_IEN | M32R_ICUCR_ISMOD00;
	disable_mappi_irq(M32R_IRQ_INT1);

	/* INT2 : pccard1 interrupt */
	irq_set_chip_and_handler(M32R_IRQ_INT2, &mappi_irq_type,
				 handle_level_irq);
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
