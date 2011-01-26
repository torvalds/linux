/*
 *  linux/arch/m32r/platforms/oaks32r/setup.c
 *
 *  Setup routines for OAKS32R Board
 *
 *  Copyright (c) 2002-2005  Hiroyuki Kondo, Hirokazu Takata,
 *                           Hitoshi Yamamoto, Mamoru Sakugawa
 */

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/m32r.h>
#include <asm/io.h>

#define irq2port(x) (M32R_ICU_CR1_PORTL + ((x - 1) * sizeof(unsigned long)))

icu_data_t icu_data[NR_IRQS];

static void disable_oaks32r_irq(unsigned int irq)
{
	unsigned long port, data;

	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_ILEVEL7;
	outl(data, port);
}

static void enable_oaks32r_irq(unsigned int irq)
{
	unsigned long port, data;

	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_IEN|M32R_ICUCR_ILEVEL6;
	outl(data, port);
}

static void mask_oaks32r(struct irq_data *data)
{
	disable_oaks32r_irq(data->irq);
}

static void unmask_oaks32r(struct irq_data *data)
{
	enable_oaks32r_irq(data->irq);
}

static void shutdown_oaks32r(struct irq_data *data)
{
	unsigned long port;

	port = irq2port(data->irq);
	outl(M32R_ICUCR_ILEVEL7, port);
}

static struct irq_chip oaks32r_irq_type =
{
	.name		= "OAKS32R-IRQ",
	.irq_shutdown	= shutdown_oaks32r,
	.irq_mask	= mask_oaks32r,
	.irq_unmask	= unmask_oaks32r,
};

void __init init_IRQ(void)
{
	static int once = 0;

	if (once)
		return;
	else
		once++;

#ifdef CONFIG_NE2000
	/* INT3 : LAN controller (RTL8019AS) */
	set_irq_chip_and_handler(M32R_IRQ_INT3, &oaks32r_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_INT3].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD10;
	disable_oaks32r_irq(M32R_IRQ_INT3);
#endif /* CONFIG_M32R_NE2000 */

	/* MFT2 : system timer */
	set_irq_chip_and_handler(M32R_IRQ_MFT2, &oaks32r_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_MFT2].icucr = M32R_ICUCR_IEN;
	disable_oaks32r_irq(M32R_IRQ_MFT2);

#ifdef CONFIG_SERIAL_M32R_SIO
	/* SIO0_R : uart receive data */
	set_irq_chip_and_handler(M32R_IRQ_SIO0_R, &oaks32r_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO0_R].icucr = 0;
	disable_oaks32r_irq(M32R_IRQ_SIO0_R);

	/* SIO0_S : uart send data */
	set_irq_chip_and_handler(M32R_IRQ_SIO0_S, &oaks32r_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO0_S].icucr = 0;
	disable_oaks32r_irq(M32R_IRQ_SIO0_S);

	/* SIO1_R : uart receive data */
	set_irq_chip_and_handler(M32R_IRQ_SIO1_R, &oaks32r_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO1_R].icucr = 0;
	disable_oaks32r_irq(M32R_IRQ_SIO1_R);

	/* SIO1_S : uart send data */
	set_irq_chip_and_handler(M32R_IRQ_SIO1_S, &oaks32r_irq_type,
				 handle_level_irq);
	icu_data[M32R_IRQ_SIO1_S].icucr = 0;
	disable_oaks32r_irq(M32R_IRQ_SIO1_S);
#endif /* CONFIG_SERIAL_M32R_SIO */
}
