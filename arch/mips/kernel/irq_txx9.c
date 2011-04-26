/*
 * Based on linux/arch/mips/jmr3927/rbhma3100/irq.c,
 *          linux/arch/mips/tx4927/common/tx4927_irq.c,
 *          linux/arch/mips/tx4938/common/irq.c
 *
 * Copyright 2001, 2003-2005 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         ahennessy@mvista.com
 *         source@mvista.com
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <asm/txx9irq.h>

struct txx9_irc_reg {
	u32 cer;
	u32 cr[2];
	u32 unused0;
	u32 ilr[8];
	u32 unused1[4];
	u32 imr;
	u32 unused2[7];
	u32 scr;
	u32 unused3[7];
	u32 ssr;
	u32 unused4[7];
	u32 csr;
};

/* IRCER : Int. Control Enable */
#define TXx9_IRCER_ICE	0x00000001

/* IRCR : Int. Control */
#define TXx9_IRCR_LOW	0x00000000
#define TXx9_IRCR_HIGH	0x00000001
#define TXx9_IRCR_DOWN	0x00000002
#define TXx9_IRCR_UP	0x00000003
#define TXx9_IRCR_EDGE(cr)	((cr) & 0x00000002)

/* IRSCR : Int. Status Control */
#define TXx9_IRSCR_EIClrE	0x00000100
#define TXx9_IRSCR_EIClr_MASK	0x0000000f

/* IRCSR : Int. Current Status */
#define TXx9_IRCSR_IF	0x00010000
#define TXx9_IRCSR_ILV_MASK	0x00000700
#define TXx9_IRCSR_IVL_MASK	0x0000001f

#define irc_dlevel	0
#define irc_elevel	1

static struct txx9_irc_reg __iomem *txx9_ircptr __read_mostly;

static struct {
	unsigned char level;
	unsigned char mode;
} txx9irq[TXx9_MAX_IR] __read_mostly;

static void txx9_irq_unmask(struct irq_data *d)
{
	unsigned int irq_nr = d->irq - TXX9_IRQ_BASE;
	u32 __iomem *ilrp = &txx9_ircptr->ilr[(irq_nr % 16 ) / 2];
	int ofs = irq_nr / 16 * 16 + (irq_nr & 1) * 8;

	__raw_writel((__raw_readl(ilrp) & ~(0xff << ofs))
		     | (txx9irq[irq_nr].level << ofs),
		     ilrp);
#ifdef CONFIG_CPU_TX39XX
	/* update IRCSR */
	__raw_writel(0, &txx9_ircptr->imr);
	__raw_writel(irc_elevel, &txx9_ircptr->imr);
#endif
}

static inline void txx9_irq_mask(struct irq_data *d)
{
	unsigned int irq_nr = d->irq - TXX9_IRQ_BASE;
	u32 __iomem *ilrp = &txx9_ircptr->ilr[(irq_nr % 16) / 2];
	int ofs = irq_nr / 16 * 16 + (irq_nr & 1) * 8;

	__raw_writel((__raw_readl(ilrp) & ~(0xff << ofs))
		     | (irc_dlevel << ofs),
		     ilrp);
#ifdef CONFIG_CPU_TX39XX
	/* update IRCSR */
	__raw_writel(0, &txx9_ircptr->imr);
	__raw_writel(irc_elevel, &txx9_ircptr->imr);
	/* flush write buffer */
	__raw_readl(&txx9_ircptr->ssr);
#else
	mmiowb();
#endif
}

static void txx9_irq_mask_ack(struct irq_data *d)
{
	unsigned int irq_nr = d->irq - TXX9_IRQ_BASE;

	txx9_irq_mask(d);
	/* clear edge detection */
	if (unlikely(TXx9_IRCR_EDGE(txx9irq[irq_nr].mode)))
		__raw_writel(TXx9_IRSCR_EIClrE | irq_nr, &txx9_ircptr->scr);
}

static int txx9_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	unsigned int irq_nr = d->irq - TXX9_IRQ_BASE;
	u32 cr;
	u32 __iomem *crp;
	int ofs;
	int mode;

	if (flow_type & IRQF_TRIGGER_PROBE)
		return 0;
	switch (flow_type & IRQF_TRIGGER_MASK) {
	case IRQF_TRIGGER_RISING:	mode = TXx9_IRCR_UP;	break;
	case IRQF_TRIGGER_FALLING:	mode = TXx9_IRCR_DOWN;	break;
	case IRQF_TRIGGER_HIGH:	mode = TXx9_IRCR_HIGH;	break;
	case IRQF_TRIGGER_LOW:	mode = TXx9_IRCR_LOW;	break;
	default:
		return -EINVAL;
	}
	crp = &txx9_ircptr->cr[(unsigned int)irq_nr / 8];
	cr = __raw_readl(crp);
	ofs = (irq_nr & (8 - 1)) * 2;
	cr &= ~(0x3 << ofs);
	cr |= (mode & 0x3) << ofs;
	__raw_writel(cr, crp);
	txx9irq[irq_nr].mode = mode;
	return 0;
}

static struct irq_chip txx9_irq_chip = {
	.name		= "TXX9",
	.irq_ack	= txx9_irq_mask_ack,
	.irq_mask	= txx9_irq_mask,
	.irq_mask_ack	= txx9_irq_mask_ack,
	.irq_unmask	= txx9_irq_unmask,
	.irq_set_type	= txx9_irq_set_type,
};

void __init txx9_irq_init(unsigned long baseaddr)
{
	int i;

	txx9_ircptr = ioremap(baseaddr, sizeof(struct txx9_irc_reg));
	for (i = 0; i < TXx9_MAX_IR; i++) {
		txx9irq[i].level = 4; /* middle level */
		txx9irq[i].mode = TXx9_IRCR_LOW;
		irq_set_chip_and_handler(TXX9_IRQ_BASE + i, &txx9_irq_chip,
					 handle_level_irq);
	}

	/* mask all IRC interrupts */
	__raw_writel(0, &txx9_ircptr->imr);
	for (i = 0; i < 8; i++)
		__raw_writel(0, &txx9_ircptr->ilr[i]);
	/* setup IRC interrupt mode (Low Active) */
	for (i = 0; i < 2; i++)
		__raw_writel(0, &txx9_ircptr->cr[i]);
	/* enable interrupt control */
	__raw_writel(TXx9_IRCER_ICE, &txx9_ircptr->cer);
	__raw_writel(irc_elevel, &txx9_ircptr->imr);
}

int __init txx9_irq_set_pri(int irc_irq, int new_pri)
{
	int old_pri;

	if ((unsigned int)irc_irq >= TXx9_MAX_IR)
		return 0;
	old_pri = txx9irq[irc_irq].level;
	txx9irq[irc_irq].level = new_pri;
	return old_pri;
}

int txx9_irq(void)
{
	u32 csr = __raw_readl(&txx9_ircptr->csr);

	if (likely(!(csr & TXx9_IRCSR_IF)))
		return TXX9_IRQ_BASE + (csr & (TXx9_MAX_IR - 1));
	return -1;
}
