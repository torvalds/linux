/* $Id: s0box.c,v 2.6.2.4 2004/01/13 23:48:39 keil Exp $
 *
 * low level stuff for Creatix S0BOX
 *
 * Author       Enrik Berkhan
 * Copyright    by Enrik Berkhan <enrik@starfleet.inka.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

static const char *s0box_revision = "$Revision: 2.6.2.4 $";

static inline void
writereg(unsigned int padr, signed int addr, u_char off, u_char val) {
	outb_p(0x1c, padr + 2);
	outb_p(0x14, padr + 2);
	outb_p((addr + off) & 0x7f, padr);
	outb_p(0x16, padr + 2);
	outb_p(val, padr);
	outb_p(0x17, padr + 2);
	outb_p(0x14, padr + 2);
	outb_p(0x1c, padr + 2);
}

static u_char nibtab[] = { 1, 9, 5, 0xd, 3, 0xb, 7, 0xf,
			   0, 0, 0, 0, 0, 0, 0, 0,
			   0, 8, 4, 0xc, 2, 0xa, 6, 0xe };

static inline u_char
readreg(unsigned int padr, signed int addr, u_char off) {
	register u_char n1, n2;

	outb_p(0x1c, padr + 2);
	outb_p(0x14, padr + 2);
	outb_p((addr + off) | 0x80, padr);
	outb_p(0x16, padr + 2);
	outb_p(0x17, padr + 2);
	n1 = (inb_p(padr + 1) >> 3) & 0x17;
	outb_p(0x16, padr + 2);
	n2 = (inb_p(padr + 1) >> 3) & 0x17;
	outb_p(0x14, padr + 2);
	outb_p(0x1c, padr + 2);
	return nibtab[n1] | (nibtab[n2] << 4);
}

static inline void
read_fifo(unsigned int padr, signed int adr, u_char *data, int size)
{
	int i;
	register u_char n1, n2;

	outb_p(0x1c, padr + 2);
	outb_p(0x14, padr + 2);
	outb_p(adr | 0x80, padr);
	outb_p(0x16, padr + 2);
	for (i = 0; i < size; i++) {
		outb_p(0x17, padr + 2);
		n1 = (inb_p(padr + 1) >> 3) & 0x17;
		outb_p(0x16, padr + 2);
		n2 = (inb_p(padr + 1) >> 3) & 0x17;
		*(data++) = nibtab[n1] | (nibtab[n2] << 4);
	}
	outb_p(0x14, padr + 2);
	outb_p(0x1c, padr + 2);
	return;
}

static inline void
write_fifo(unsigned int padr, signed int adr, u_char *data, int size)
{
	int i;
	outb_p(0x1c, padr + 2);
	outb_p(0x14, padr + 2);
	outb_p(adr & 0x7f, padr);
	for (i = 0; i < size; i++) {
		outb_p(0x16, padr + 2);
		outb_p(*(data++), padr);
		outb_p(0x17, padr + 2);
	}
	outb_p(0x14, padr + 2);
	outb_p(0x1c, padr + 2);
	return;
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.teles3.cfg_reg, cs->hw.teles3.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.teles3.cfg_reg, cs->hw.teles3.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	read_fifo(cs->hw.teles3.cfg_reg, cs->hw.teles3.isacfifo, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	write_fifo(cs->hw.teles3.cfg_reg, cs->hw.teles3.isacfifo, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[hscx], offset));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[hscx], offset, value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[nr], reg)
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[nr], reg, data)
#define READHSCXFIFO(cs, nr, ptr, cnt) read_fifo(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscxfifo[nr], ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) write_fifo(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscxfifo[nr], ptr, cnt)

#include "hscx_irq.c"

static irqreturn_t
s0box_interrupt(int intno, void *dev_id)
{
#define MAXCOUNT 5
	struct IsdnCardState *cs = dev_id;
	u_char val;
	u_long flags;
	int count = 0;

	spin_lock_irqsave(&cs->lock, flags);
	val = readreg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[1], HSCX_ISTA);
Start_HSCX:
	if (val)
		hscx_int_main(cs, val);
	val = readreg(cs->hw.teles3.cfg_reg, cs->hw.teles3.isac, ISAC_ISTA);
Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	count++;
	val = readreg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[1], HSCX_ISTA);
	if (val && count < MAXCOUNT) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.teles3.cfg_reg, cs->hw.teles3.isac, ISAC_ISTA);
	if (val && count < MAXCOUNT) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (count >= MAXCOUNT)
		printk(KERN_WARNING "S0Box: more than %d loops in s0box_interrupt\n", count);
	writereg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[0], HSCX_MASK, 0xFF);
	writereg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[1], HSCX_MASK, 0xFF);
	writereg(cs->hw.teles3.cfg_reg, cs->hw.teles3.isac, ISAC_MASK, 0xFF);
	writereg(cs->hw.teles3.cfg_reg, cs->hw.teles3.isac, ISAC_MASK, 0x0);
	writereg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[0], HSCX_MASK, 0x0);
	writereg(cs->hw.teles3.cfg_reg, cs->hw.teles3.hscx[1], HSCX_MASK, 0x0);
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

static void
release_io_s0box(struct IsdnCardState *cs)
{
	release_region(cs->hw.teles3.cfg_reg, 8);
}

static int
S0Box_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
	case CARD_RESET:
		break;
	case CARD_RELEASE:
		release_io_s0box(cs);
		break;
	case CARD_INIT:
		spin_lock_irqsave(&cs->lock, flags);
		inithscxisac(cs, 3);
		spin_unlock_irqrestore(&cs->lock, flags);
		break;
	case CARD_TEST:
		break;
	}
	return (0);
}

int __devinit
setup_s0box(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, s0box_revision);
	printk(KERN_INFO "HiSax: S0Box IO driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_S0BOX)
		return (0);

	cs->hw.teles3.cfg_reg = card->para[1];
	cs->hw.teles3.hscx[0] = -0x20;
	cs->hw.teles3.hscx[1] = 0x0;
	cs->hw.teles3.isac = 0x20;
	cs->hw.teles3.isacfifo = cs->hw.teles3.isac + 0x3e;
	cs->hw.teles3.hscxfifo[0] = cs->hw.teles3.hscx[0] + 0x3e;
	cs->hw.teles3.hscxfifo[1] = cs->hw.teles3.hscx[1] + 0x3e;
	cs->irq = card->para[0];
	if (!request_region(cs->hw.teles3.cfg_reg, 8, "S0Box parallel I/O")) {
		printk(KERN_WARNING "HiSax: S0Box ports %x-%x already in use\n",
		       cs->hw.teles3.cfg_reg,
		       cs->hw.teles3.cfg_reg + 7);
		return 0;
	}
	printk(KERN_INFO "HiSax: S0Box config irq:%d isac:0x%x  cfg:0x%x\n",
	       cs->irq,
	       cs->hw.teles3.isac, cs->hw.teles3.cfg_reg);
	printk(KERN_INFO "HiSax: hscx A:0x%x  hscx B:0x%x\n",
	       cs->hw.teles3.hscx[0], cs->hw.teles3.hscx[1]);
	setup_isac(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &S0Box_card_msg;
	cs->irq_func = &s0box_interrupt;
	ISACVersion(cs, "S0Box:");
	if (HscxVersion(cs, "S0Box:")) {
		printk(KERN_WARNING
		       "S0Box: wrong HSCX versions check IO address\n");
		release_io_s0box(cs);
		return (0);
	}
	return (1);
}
