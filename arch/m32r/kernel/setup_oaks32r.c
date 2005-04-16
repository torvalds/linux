/*
 *  linux/arch/m32r/kernel/setup_oaks32r.c
 *
 *  Setup routines for OAKS32R Board
 *
 *  Copyright (c) 2002-2004   Hiroyuki Kondo, Hirokazu Takata,
 *                            Hitoshi Yamamoto, Mamoru Sakugawa
 */

#include <linux/config.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>

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

static void mask_and_ack_mappi(unsigned int irq)
{
	disable_oaks32r_irq(irq);
}

static void end_oaks32r_irq(unsigned int irq)
{
	enable_oaks32r_irq(irq);
}

static unsigned int startup_oaks32r_irq(unsigned int irq)
{
	enable_oaks32r_irq(irq);
	return (0);
}

static void shutdown_oaks32r_irq(unsigned int irq)
{
	unsigned long port;

	port = irq2port(irq);
	outl(M32R_ICUCR_ILEVEL7, port);
}

static struct hw_interrupt_type oaks32r_irq_type =
{
	"OAKS32R-IRQ",
	startup_oaks32r_irq,
	shutdown_oaks32r_irq,
	enable_oaks32r_irq,
	disable_oaks32r_irq,
	mask_and_ack_mappi,
	end_oaks32r_irq
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
	irq_desc[M32R_IRQ_INT3].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_INT3].handler = &oaks32r_irq_type;
	irq_desc[M32R_IRQ_INT3].action = 0;
	irq_desc[M32R_IRQ_INT3].depth = 1;
	icu_data[M32R_IRQ_INT3].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD10;
	disable_oaks32r_irq(M32R_IRQ_INT3);
#endif /* CONFIG_M32R_NE2000 */

	/* MFT2 : system timer */
	irq_desc[M32R_IRQ_MFT2].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_MFT2].handler = &oaks32r_irq_type;
	irq_desc[M32R_IRQ_MFT2].action = 0;
	irq_desc[M32R_IRQ_MFT2].depth = 1;
	icu_data[M32R_IRQ_MFT2].icucr = M32R_ICUCR_IEN;
	disable_oaks32r_irq(M32R_IRQ_MFT2);

#ifdef CONFIG_SERIAL_M32R_SIO
	/* SIO0_R : uart receive data */
	irq_desc[M32R_IRQ_SIO0_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_R].handler = &oaks32r_irq_type;
	irq_desc[M32R_IRQ_SIO0_R].action = 0;
	irq_desc[M32R_IRQ_SIO0_R].depth = 1;
	icu_data[M32R_IRQ_SIO0_R].icucr = 0;
	disable_oaks32r_irq(M32R_IRQ_SIO0_R);

	/* SIO0_S : uart send data */
	irq_desc[M32R_IRQ_SIO0_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_S].handler = &oaks32r_irq_type;
	irq_desc[M32R_IRQ_SIO0_S].action = 0;
	irq_desc[M32R_IRQ_SIO0_S].depth = 1;
	icu_data[M32R_IRQ_SIO0_S].icucr = 0;
	disable_oaks32r_irq(M32R_IRQ_SIO0_S);

	/* SIO1_R : uart receive data */
	irq_desc[M32R_IRQ_SIO1_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_R].handler = &oaks32r_irq_type;
	irq_desc[M32R_IRQ_SIO1_R].action = 0;
	irq_desc[M32R_IRQ_SIO1_R].depth = 1;
	icu_data[M32R_IRQ_SIO1_R].icucr = 0;
	disable_oaks32r_irq(M32R_IRQ_SIO1_R);

	/* SIO1_S : uart send data */
	irq_desc[M32R_IRQ_SIO1_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_S].handler = &oaks32r_irq_type;
	irq_desc[M32R_IRQ_SIO1_S].action = 0;
	irq_desc[M32R_IRQ_SIO1_S].depth = 1;
	icu_data[M32R_IRQ_SIO1_S].icucr = 0;
	disable_oaks32r_irq(M32R_IRQ_SIO1_S);
#endif /* CONFIG_SERIAL_M32R_SIO */

}
