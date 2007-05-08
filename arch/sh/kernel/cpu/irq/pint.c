/*
 * arch/sh/kernel/cpu/irq/pint.c - Interrupt handling for PINT-based IRQs.
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2003 Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/machvec.h>

#if defined(CONFIG_CPU_SUBTYPE_SH7705)
#define INTC_INTER      0xA4000014UL
#define INTC_IPRD       0xA4000018UL
#define INTC_ICR2       0xA4000012UL

/* PFC */
#define PORT_PACR       0xA4000100UL
#define PORT_PBCR       0xA4000102UL
#define PORT_PCCR       0xA4000104UL
#define PORT_PDCR       0xA4000106UL
#define PORT_PECR       0xA4000108UL
#define PORT_PFCR       0xA400010AUL
#define PORT_PGCR       0xA400010CUL
#define PORT_PHCR       0xA400010EUL
#define PORT_PJCR       0xA4000110UL
#define PORT_PKCR       0xA4000112UL
#define PORT_PLCR       0xA4000114UL
#define PORT_PMCR       0xA4000118UL
#define PORT_PNCR       0xA400011AUL
#define PORT_PECR2      0xA4050148UL
#define PORT_PFCR2      0xA405014AUL
#define PORT_PNCR2      0xA405015AUL

/* I/O port */
#define PORT_PADR       0xA4000120UL
#define PORT_PBDR       0xA4000122UL
#define PORT_PCDR       0xA4000124UL
#define PORT_PDDR       0xA4000126UL
#define PORT_PEDR       0xA4000128UL
#define PORT_PFDR       0xA400012AUL
#define PORT_PGDR       0xA400012CUL
#define PORT_PHDR       0xA400012EUL
#define PORT_PJDR       0xA4000130UL
#define PORT_PKDR       0xA4000132UL
#define PORT_PLDR       0xA4000134UL
#define PORT_PMDR       0xA4000138UL
#define PORT_PNDR       0xA400013AUL

#define PINT0_IRQ       40
#define PINT8_IRQ       41
#define PINT_IRQ_BASE   86

#define PINT0_IPR_ADDR          INTC_IPRD
#define PINT0_IPR_POS           3
#define PINT0_PRIORITY      2

#define PINT8_IPR_ADDR          INTC_IPRD
#define PINT8_IPR_POS           2
#define PINT8_PRIORITY      2

#endif /* CONFIG_CPU_SUBTYPE_SH7705 */

static unsigned char pint_map[256];
static unsigned long portcr_mask;

static void enable_pint_irq(unsigned int irq);
static void disable_pint_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_pint_irq disable_pint_irq

static void mask_and_ack_pint(unsigned int);
static void end_pint_irq(unsigned int irq);

static unsigned int startup_pint_irq(unsigned int irq)
{
	enable_pint_irq(irq);
	return 0; /* never anything pending */
}

static struct hw_interrupt_type pint_irq_type = {
	.typename = "PINT-IRQ",
	.startup = startup_pint_irq,
	.shutdown = shutdown_pint_irq,
	.enable = enable_pint_irq,
	.disable = disable_pint_irq,
	.ack = mask_and_ack_pint,
	.end = end_pint_irq
};

static void disable_pint_irq(unsigned int irq)
{
	unsigned long val;

	val = ctrl_inw(INTC_INTER);
	val &= ~(1 << (irq - PINT_IRQ_BASE));
	ctrl_outw(val, INTC_INTER);	/* disable PINTn */
	portcr_mask &= ~(3 << (irq - PINT_IRQ_BASE)*2);
}

static void enable_pint_irq(unsigned int irq)
{
	unsigned long val;

	val = ctrl_inw(INTC_INTER);
	val |= 1 << (irq - PINT_IRQ_BASE);
	ctrl_outw(val, INTC_INTER);	/* enable PINTn */
	portcr_mask |= 3 << (irq - PINT_IRQ_BASE)*2;
}

static void mask_and_ack_pint(unsigned int irq)
{
	disable_pint_irq(irq);
}

static void end_pint_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_pint_irq(irq);
}

void make_pint_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].chip = &pint_irq_type;
	disable_pint_irq(irq);
}

static struct ipr_data pint_ipr_map[] = {
	{ PINT0_IRQ, PINT0_IPR_ADDR, PINT0_IPR_POS, PINT0_PRIORITY },
	{ PINT8_IRQ, PINT8_IPR_ADDR, PINT8_IPR_POS, PINT8_PRIORITY },
};

void __init init_IRQ_pint(void)
{
	int i;

	make_ipr_irq(pint_ipr_map, ARRAY_SIZE(pint_ipr_map));

	enable_irq(PINT0_IRQ);
	enable_irq(PINT8_IRQ);

	for(i = 0; i < 16; i++)
		make_pint_irq(PINT_IRQ_BASE + i);

	for(i = 0; i < 256; i++) {
		if (i & 1)
			pint_map[i] = 0;
		else if (i & 2)
			pint_map[i] = 1;
		else if (i & 4)
			pint_map[i] = 2;
		else if (i & 8)
			pint_map[i] = 3;
		else if (i & 0x10)
			pint_map[i] = 4;
		else if (i & 0x20)
			pint_map[i] = 5;
		else if (i & 0x40)
			pint_map[i] = 6;
		else if (i & 0x80)
			pint_map[i] = 7;
	}
}

int ipr_irq_demux(int irq)
{
	unsigned long creg, dreg, d, sav;

	if (irq == PINT0_IRQ) {
#if defined(CONFIG_CPU_SUBTYPE_SH7705) || defined(CONFIG_CPU_SUBTYPE_SH7707)
		creg = PORT_PACR;
		dreg = PORT_PADR;
#else
		creg = PORT_PCCR;
		dreg = PORT_PCDR;
#endif
		sav = ctrl_inw(creg);
		ctrl_outw(sav | portcr_mask, creg);
		d = (~ctrl_inb(dreg) ^ ctrl_inw(INTC_ICR2)) &
			ctrl_inw(INTC_INTER) & 0xff;
		ctrl_outw(sav, creg);

		if (d == 0)
			return irq;

		return PINT_IRQ_BASE + pint_map[d];
	} else if (irq == PINT8_IRQ) {
#if defined(CONFIG_CPU_SUBTYPE_SH7705) || defined(CONFIG_CPU_SUBTYPE_SH7707)
		creg = PORT_PBCR;
		dreg = PORT_PBDR;
#else
		creg = PORT_PFCR;
		dreg = PORT_PFDR;
#endif
		sav = ctrl_inw(creg);
		ctrl_outw(sav | (portcr_mask >> 16), creg);
		d = (~ctrl_inb(dreg) ^ (ctrl_inw(INTC_ICR2) >> 8)) &
			(ctrl_inw(INTC_INTER) >> 8) & 0xff;
		ctrl_outw(sav, creg);

		if (d == 0)
			return irq;

		return PINT_IRQ_BASE + 8 + pint_map[d];
	}

	return irq;
}

