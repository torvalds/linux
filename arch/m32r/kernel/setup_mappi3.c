/*
 *  linux/arch/m32r/kernel/setup_mappi3.c
 *
 *  Setup routines for Renesas MAPPI-III(M3A-2170) Board
 *
 *  Copyright (c) 2001-2005  Hiroyuki Kondo, Hirokazu Takata,
 *                           Hitoshi Yamamoto, Mamoru Sakugawa
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

icu_data_t icu_data[NR_IRQS];

static void disable_mappi3_irq(unsigned int irq)
{
	unsigned long port, data;

	if ((irq == 0) ||(irq >= NR_IRQS))  {
		printk("bad irq 0x%08x\n", irq);
		return;
	}
	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_ILEVEL7;
	outl(data, port);
}

static void enable_mappi3_irq(unsigned int irq)
{
	unsigned long port, data;

	if ((irq == 0) ||(irq >= NR_IRQS))  {
		printk("bad irq 0x%08x\n", irq);
		return;
	}
	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_IEN|M32R_ICUCR_ILEVEL6;
	outl(data, port);
}

static void mask_and_ack_mappi3(unsigned int irq)
{
	disable_mappi3_irq(irq);
}

static void end_mappi3_irq(unsigned int irq)
{
	enable_mappi3_irq(irq);
}

static unsigned int startup_mappi3_irq(unsigned int irq)
{
	enable_mappi3_irq(irq);
	return (0);
}

static void shutdown_mappi3_irq(unsigned int irq)
{
	unsigned long port;

	port = irq2port(irq);
	outl(M32R_ICUCR_ILEVEL7, port);
}

static struct hw_interrupt_type mappi3_irq_type =
{
	.typename = "MAPPI3-IRQ",
	.startup = startup_mappi3_irq,
	.shutdown = shutdown_mappi3_irq,
	.enable = enable_mappi3_irq,
	.disable = disable_mappi3_irq,
	.ack = mask_and_ack_mappi3,
	.end = end_mappi3_irq
};

void __init init_IRQ(void)
{
#if defined(CONFIG_SMC91X)
	/* INT0 : LAN controller (SMC91111) */
	irq_desc[M32R_IRQ_INT0].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_INT0].handler = &mappi3_irq_type;
	irq_desc[M32R_IRQ_INT0].action = 0;
	irq_desc[M32R_IRQ_INT0].depth = 1;
	icu_data[M32R_IRQ_INT0].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD10;
	disable_mappi3_irq(M32R_IRQ_INT0);
#endif  /* CONFIG_SMC91X */

	/* MFT2 : system timer */
	irq_desc[M32R_IRQ_MFT2].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_MFT2].handler = &mappi3_irq_type;
	irq_desc[M32R_IRQ_MFT2].action = 0;
	irq_desc[M32R_IRQ_MFT2].depth = 1;
	icu_data[M32R_IRQ_MFT2].icucr = M32R_ICUCR_IEN;
	disable_mappi3_irq(M32R_IRQ_MFT2);

#ifdef CONFIG_SERIAL_M32R_SIO
	/* SIO0_R : uart receive data */
	irq_desc[M32R_IRQ_SIO0_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_R].handler = &mappi3_irq_type;
	irq_desc[M32R_IRQ_SIO0_R].action = 0;
	irq_desc[M32R_IRQ_SIO0_R].depth = 1;
	icu_data[M32R_IRQ_SIO0_R].icucr = 0;
	disable_mappi3_irq(M32R_IRQ_SIO0_R);

	/* SIO0_S : uart send data */
	irq_desc[M32R_IRQ_SIO0_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_S].handler = &mappi3_irq_type;
	irq_desc[M32R_IRQ_SIO0_S].action = 0;
	irq_desc[M32R_IRQ_SIO0_S].depth = 1;
	icu_data[M32R_IRQ_SIO0_S].icucr = 0;
	disable_mappi3_irq(M32R_IRQ_SIO0_S);
	/* SIO1_R : uart receive data */
	irq_desc[M32R_IRQ_SIO1_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_R].handler = &mappi3_irq_type;
	irq_desc[M32R_IRQ_SIO1_R].action = 0;
	irq_desc[M32R_IRQ_SIO1_R].depth = 1;
	icu_data[M32R_IRQ_SIO1_R].icucr = 0;
	disable_mappi3_irq(M32R_IRQ_SIO1_R);

	/* SIO1_S : uart send data */
	irq_desc[M32R_IRQ_SIO1_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_S].handler = &mappi3_irq_type;
	irq_desc[M32R_IRQ_SIO1_S].action = 0;
	irq_desc[M32R_IRQ_SIO1_S].depth = 1;
	icu_data[M32R_IRQ_SIO1_S].icucr = 0;
	disable_mappi3_irq(M32R_IRQ_SIO1_S);
#endif  /* CONFIG_M32R_USE_DBG_CONSOLE */

#if defined(CONFIG_USB)
	/* INT1 : USB Host controller interrupt */
	irq_desc[M32R_IRQ_INT1].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_INT1].handler = &mappi3_irq_type;
	irq_desc[M32R_IRQ_INT1].action = 0;
	irq_desc[M32R_IRQ_INT1].depth = 1;
	icu_data[M32R_IRQ_INT1].icucr = M32R_ICUCR_ISMOD01;
	disable_mappi3_irq(M32R_IRQ_INT1);
#endif /* CONFIG_USB */

	/* CFC IREQ */
	irq_desc[PLD_IRQ_CFIREQ].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_CFIREQ].handler = &mappi3_irq_type;
	irq_desc[PLD_IRQ_CFIREQ].action = 0;
	irq_desc[PLD_IRQ_CFIREQ].depth = 1;	/* disable nested irq */
	icu_data[PLD_IRQ_CFIREQ].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD01;
	disable_mappi3_irq(PLD_IRQ_CFIREQ);

#if defined(CONFIG_M32R_CFC)
	/* ICUCR41: CFC Insert & eject */
	irq_desc[PLD_IRQ_CFC_INSERT].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_CFC_INSERT].handler = &mappi3_irq_type;
	irq_desc[PLD_IRQ_CFC_INSERT].action = 0;
	irq_desc[PLD_IRQ_CFC_INSERT].depth = 1;	/* disable nested irq */
	icu_data[PLD_IRQ_CFC_INSERT].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD00;
	disable_mappi3_irq(PLD_IRQ_CFC_INSERT);

#endif /* CONFIG_M32R_CFC */

	/* IDE IREQ */
	irq_desc[PLD_IRQ_IDEIREQ].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_IDEIREQ].handler = &mappi3_irq_type;
	irq_desc[PLD_IRQ_IDEIREQ].action = 0;
	irq_desc[PLD_IRQ_IDEIREQ].depth = 1;	/* disable nested irq */
	icu_data[PLD_IRQ_IDEIREQ].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD10;
	disable_mappi3_irq(PLD_IRQ_IDEIREQ);

}

#if defined(CONFIG_SMC91X)

#define LAN_IOSTART     0x300
#define LAN_IOEND       0x320
static struct resource smc91x_resources[] = {
	[0] = {
		.start  = (LAN_IOSTART),
		.end    = (LAN_IOEND),
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = M32R_IRQ_INT0,
		.end    = M32R_IRQ_INT0,
		.flags  = IORESOURCE_IRQ,
	}
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources  = ARRAY_SIZE(smc91x_resources),
	.resource       = smc91x_resources,
};

#endif

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
		.start  = 0x1d600000UL,
		.end    = 0x1d73FFFFUL,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 0x1d400000UL,
		.end    = 0x1d4001FFUL,
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
#endif

static int __init platform_init(void)
{
#if defined(CONFIG_SMC91X)
	platform_device_register(&smc91x_device);
#endif
#if defined(CONFIG_FB_S1D13XXX)
	platform_device_register(&s1d13xxxfb_device);
#endif
	return 0;
}
arch_initcall(platform_init);
