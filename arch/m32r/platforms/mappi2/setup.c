/*
 *  linux/arch/m32r/platforms/mappi2/setup.c
 *
 *  Setup routines for Renesas MAPPI-II(M3A-ZA36) Board
 *
 *  Copyright (c) 2001-2005  Hiroyuki Kondo, Hirokazu Takata,
 *                           Hitoshi Yamamoto, Mamoru Sakugawa
 */

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/system.h>
#include <asm/m32r.h>
#include <asm/io.h>

#define irq2port(x) (M32R_ICU_CR1_PORTL + ((x - 1) * sizeof(unsigned long)))

icu_data_t icu_data[NR_IRQS];

static void disable_mappi2_irq(unsigned int irq)
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

static void enable_mappi2_irq(unsigned int irq)
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

static void mask_mappi2(struct irq_data *data)
{
	disable_mappi2_irq(data->irq);
}

static void unmask_mappi2(struct irq_data *data)
{
	enable_mappi2_irq(data->irq);
}

static void shutdown_mappi2(struct irq_data *data)
{
	unsigned long port;

	port = irq2port(data->irq);
	outl(M32R_ICUCR_ILEVEL7, port);
}

static struct irq_chip mappi2_irq_type =
{
	.name		= "MAPPI2-IRQ",
	.irq_shutdown	= shutdown_mappi2,
	.irq_mask	= mask_mappi2,
	.irq_unmask	= unmask_mappi2,
};

void __init init_IRQ(void)
{
#if defined(CONFIG_SMC91X)
	/* INT0 : LAN controller (SMC91111) */
	set_irq_chip_and_handler(M32R_IRQ_INT0, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_INT0].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD10;
	disable_mappi2_irq(M32R_IRQ_INT0);
#endif  /* CONFIG_SMC91X */

	/* MFT2 : system timer */
	set_irq_chip_and_handler(M32R_IRQ_MFT2, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_MFT2].icucr = M32R_ICUCR_IEN;
	disable_mappi2_irq(M32R_IRQ_MFT2);

#ifdef CONFIG_SERIAL_M32R_SIO
	/* SIO0_R : uart receive data */
	set_irq_chip_and_handler(M32R_IRQ_SIO0_R, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO0_R].icucr = 0;
	disable_mappi2_irq(M32R_IRQ_SIO0_R);

	/* SIO0_S : uart send data */
	set_irq_chip_and_handler(M32R_IRQ_SIO0_S, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO0_S].icucr = 0;
	disable_mappi2_irq(M32R_IRQ_SIO0_S);
	/* SIO1_R : uart receive data */
	set_irq_chip_and_handler(M32R_IRQ_SIO1_R, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO1_R].icucr = 0;
	disable_mappi2_irq(M32R_IRQ_SIO1_R);

	/* SIO1_S : uart send data */
	set_irq_chip_and_handler(M32R_IRQ_SIO1_S, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO1_S].icucr = 0;
	disable_mappi2_irq(M32R_IRQ_SIO1_S);
#endif  /* CONFIG_M32R_USE_DBG_CONSOLE */

#if defined(CONFIG_USB)
	/* INT1 : USB Host controller interrupt */
	set_irq_chip_and_handler(M32R_IRQ_INT1, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_INT1].icucr = M32R_ICUCR_ISMOD01;
	disable_mappi2_irq(M32R_IRQ_INT1);
#endif /* CONFIG_USB */

	/* ICUCR40: CFC IREQ */
	set_irq_chip_and_handler(PLD_IRQ_CFIREQ, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[PLD_IRQ_CFIREQ].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD01;
	disable_mappi2_irq(PLD_IRQ_CFIREQ);

#if defined(CONFIG_M32R_CFC)
	/* ICUCR41: CFC Insert */
	set_irq_chip_and_handler(PLD_IRQ_CFC_INSERT, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[PLD_IRQ_CFC_INSERT].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD00;
	disable_mappi2_irq(PLD_IRQ_CFC_INSERT);

	/* ICUCR42: CFC Eject */
	set_irq_chip_and_handler(PLD_IRQ_CFC_EJECT, &mappi2_irq_type,
				 handle_level_irq);
	icu_data[PLD_IRQ_CFC_EJECT].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD10;
	disable_mappi2_irq(PLD_IRQ_CFC_EJECT);
#endif /* CONFIG_MAPPI2_CFC */
}

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

static int __init platform_init(void)
{
	platform_device_register(&smc91x_device);
	return 0;
}
arch_initcall(platform_init);
