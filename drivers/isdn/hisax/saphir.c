/* $Id: saphir.c,v 1.10.2.4 2004/01/13 23:48:39 keil Exp $
 *
 * low level stuff for HST Saphir 1
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to    HST High Soft Tech GmbH
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];
static char *saphir_rev = "$Revision: 1.10.2.4 $";

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define ISAC_DATA	0
#define HSCX_DATA	1
#define ADDRESS_REG	2
#define IRQ_REG		3
#define SPARE_REG	4
#define RESET_REG	5

static inline u_char
readreg(unsigned int ale, unsigned int adr, u_char off)
{
	register u_char ret;

	byteout(ale, off);
	ret = bytein(adr);
	return (ret);
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	byteout(ale, off);
	insb(adr, data, size);
}


static inline void
writereg(unsigned int ale, unsigned int adr, u_char off, u_char data)
{
	byteout(ale, off);
	byteout(adr, data);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	byteout(ale, off);
	outsb(adr, data, size);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.saphir.ale, cs->hw.saphir.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.saphir.ale, cs->hw.saphir.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.saphir.ale, cs->hw.saphir.isac, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.saphir.ale, cs->hw.saphir.isac, 0, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.saphir.ale, cs->hw.saphir.hscx,
		offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.saphir.ale, cs->hw.saphir.hscx,
		offset + (hscx ? 0x40 : 0), value);
}

#define READHSCX(cs, nr, reg) readreg(cs->hw.saphir.ale, \
		cs->hw.saphir.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.saphir.ale, \
		cs->hw.saphir.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.saphir.ale, \
		cs->hw.saphir.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.saphir.ale, \
		cs->hw.saphir.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static irqreturn_t
saphir_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val;
	u_long flags;

	spin_lock_irqsave(&cs->lock, flags);
	val = readreg(cs->hw.saphir.ale, cs->hw.saphir.hscx, HSCX_ISTA + 0x40);
      Start_HSCX:
	if (val)
		hscx_int_main(cs, val);
	val = readreg(cs->hw.saphir.ale, cs->hw.saphir.isac, ISAC_ISTA);
      Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	val = readreg(cs->hw.saphir.ale, cs->hw.saphir.hscx, HSCX_ISTA + 0x40);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.saphir.ale, cs->hw.saphir.isac, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	/* Watchdog */
	if (cs->hw.saphir.timer.function) 
		mod_timer(&cs->hw.saphir.timer, jiffies+1*HZ);
	else
		printk(KERN_WARNING "saphir: Spurious timer!\n");
	writereg(cs->hw.saphir.ale, cs->hw.saphir.hscx, HSCX_MASK, 0xFF);
	writereg(cs->hw.saphir.ale, cs->hw.saphir.hscx, HSCX_MASK + 0x40, 0xFF);
	writereg(cs->hw.saphir.ale, cs->hw.saphir.isac, ISAC_MASK, 0xFF);
	writereg(cs->hw.saphir.ale, cs->hw.saphir.isac, ISAC_MASK, 0);
	writereg(cs->hw.saphir.ale, cs->hw.saphir.hscx, HSCX_MASK, 0);
	writereg(cs->hw.saphir.ale, cs->hw.saphir.hscx, HSCX_MASK + 0x40, 0);
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

static void
SaphirWatchDog(struct IsdnCardState *cs)
{
	u_long flags;

	spin_lock_irqsave(&cs->lock, flags);
        /* 5 sec WatchDog, so read at least every 4 sec */
	cs->readisac(cs, ISAC_RBCH);
	spin_unlock_irqrestore(&cs->lock, flags);
	mod_timer(&cs->hw.saphir.timer, jiffies+1*HZ);
}

static void
release_io_saphir(struct IsdnCardState *cs)
{
	byteout(cs->hw.saphir.cfg_reg + IRQ_REG, 0xff);
	del_timer(&cs->hw.saphir.timer);
	cs->hw.saphir.timer.function = NULL;
	if (cs->hw.saphir.cfg_reg)
		release_region(cs->hw.saphir.cfg_reg, 6);
}

static int
saphir_reset(struct IsdnCardState *cs)
{
	u_char irq_val;

	switch(cs->irq) {
		case 5: irq_val = 0;
			break;
		case 3: irq_val = 1;
			break;
		case 11:
			irq_val = 2;
			break;
		case 12:
			irq_val = 3;
			break;
		case 15:
			irq_val = 4;
			break;
		default:
			printk(KERN_WARNING "HiSax: saphir wrong IRQ %d\n",
				cs->irq);
			return (1);
	}
	byteout(cs->hw.saphir.cfg_reg + IRQ_REG, irq_val);
	byteout(cs->hw.saphir.cfg_reg + RESET_REG, 1);
	mdelay(10);
	byteout(cs->hw.saphir.cfg_reg + RESET_REG, 0);
	mdelay(10);
	byteout(cs->hw.saphir.cfg_reg + IRQ_REG, irq_val);
	byteout(cs->hw.saphir.cfg_reg + SPARE_REG, 0x02);
	return (0);
}

static int
saphir_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
		case CARD_RESET:
			spin_lock_irqsave(&cs->lock, flags);
			saphir_reset(cs);
			spin_unlock_irqrestore(&cs->lock, flags);
			return(0);
		case CARD_RELEASE:
			release_io_saphir(cs);
			return(0);
		case CARD_INIT:
			spin_lock_irqsave(&cs->lock, flags);
			inithscxisac(cs, 3);
			spin_unlock_irqrestore(&cs->lock, flags);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}


int __init
setup_saphir(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, saphir_rev);
	printk(KERN_INFO "HiSax: HST Saphir driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_HSTSAPHIR)
		return (0);

	/* IO-Ports */
	cs->hw.saphir.cfg_reg = card->para[1];
	cs->hw.saphir.isac = card->para[1] + ISAC_DATA;
	cs->hw.saphir.hscx = card->para[1] + HSCX_DATA;
	cs->hw.saphir.ale = card->para[1] + ADDRESS_REG;
	cs->irq = card->para[0];
	if (!request_region(cs->hw.saphir.cfg_reg, 6, "saphir")) {
		printk(KERN_WARNING
			"HiSax: %s config port %x-%x already in use\n",
			CardType[card->typ],
			cs->hw.saphir.cfg_reg,
			cs->hw.saphir.cfg_reg + 5);
		return (0);
	}

	printk(KERN_INFO "HiSax: %s config irq:%d io:0x%X\n",
		CardType[cs->typ], cs->irq, cs->hw.saphir.cfg_reg);

	setup_isac(cs);
	cs->hw.saphir.timer.function = (void *) SaphirWatchDog;
	cs->hw.saphir.timer.data = (long) cs;
	init_timer(&cs->hw.saphir.timer);
	cs->hw.saphir.timer.expires = jiffies + 4*HZ;
	add_timer(&cs->hw.saphir.timer);
	if (saphir_reset(cs)) {
		release_io_saphir(cs);
		return (0);
	}
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &saphir_card_msg;
	cs->irq_func = &saphir_interrupt;
	ISACVersion(cs, "saphir:");
	if (HscxVersion(cs, "saphir:")) {
		printk(KERN_WARNING
		    "saphir: wrong HSCX versions check IO address\n");
		release_io_saphir(cs);
		return (0);
	}
	return (1);
}
