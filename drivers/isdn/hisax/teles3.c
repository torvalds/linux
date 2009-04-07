/* $Id: teles3.c,v 2.19.2.4 2004/01/13 23:48:39 keil Exp $
 *
 * low level stuff for Teles 16.3 & PNP isdn cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 */
#include <linux/init.h>
#include <linux/isapnp.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

static const char *teles3_revision = "$Revision: 2.19.2.4 $";

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

static inline u_char
readreg(unsigned int adr, u_char off)
{
	return (bytein(adr + off));
}

static inline void
writereg(unsigned int adr, u_char off, u_char data)
{
	byteout(adr + off, data);
}


static inline void
read_fifo(unsigned int adr, u_char * data, int size)
{
	insb(adr, data, size);
}

static void
write_fifo(unsigned int adr, u_char * data, int size)
{
	outsb(adr, data, size);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.teles3.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.teles3.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	read_fifo(cs->hw.teles3.isacfifo, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	write_fifo(cs->hw.teles3.isacfifo, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.teles3.hscx[hscx], offset));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.teles3.hscx[hscx], offset, value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.teles3.hscx[nr], reg)
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.teles3.hscx[nr], reg, data)
#define READHSCXFIFO(cs, nr, ptr, cnt) read_fifo(cs->hw.teles3.hscxfifo[nr], ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) write_fifo(cs->hw.teles3.hscxfifo[nr], ptr, cnt)

#include "hscx_irq.c"

static irqreturn_t
teles3_interrupt(int intno, void *dev_id)
{
#define MAXCOUNT 5
	struct IsdnCardState *cs = dev_id;
	u_char val;
	u_long flags;
	int count = 0;

	spin_lock_irqsave(&cs->lock, flags);
	val = readreg(cs->hw.teles3.hscx[1], HSCX_ISTA);
      Start_HSCX:
	if (val)
		hscx_int_main(cs, val);
	val = readreg(cs->hw.teles3.isac, ISAC_ISTA);
      Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	count++;
	val = readreg(cs->hw.teles3.hscx[1], HSCX_ISTA);
	if (val && count < MAXCOUNT) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.teles3.isac, ISAC_ISTA);
	if (val && count < MAXCOUNT) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (count >= MAXCOUNT)
		printk(KERN_WARNING "Teles3: more than %d loops in teles3_interrupt\n", count);
	writereg(cs->hw.teles3.hscx[0], HSCX_MASK, 0xFF);
	writereg(cs->hw.teles3.hscx[1], HSCX_MASK, 0xFF);
	writereg(cs->hw.teles3.isac, ISAC_MASK, 0xFF);
	writereg(cs->hw.teles3.isac, ISAC_MASK, 0x0);
	writereg(cs->hw.teles3.hscx[0], HSCX_MASK, 0x0);
	writereg(cs->hw.teles3.hscx[1], HSCX_MASK, 0x0);
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

static inline void
release_ioregs(struct IsdnCardState *cs, int mask)
{
	if (mask & 1)
		release_region(cs->hw.teles3.isac + 32, 32);
	if (mask & 2)
		release_region(cs->hw.teles3.hscx[0] + 32, 32);
	if (mask & 4)
		release_region(cs->hw.teles3.hscx[1] + 32, 32);
}

static void
release_io_teles3(struct IsdnCardState *cs)
{
	if (cs->typ == ISDN_CTYPE_TELESPCMCIA) {
		release_region(cs->hw.teles3.hscx[1], 96);
	} else {
		if (cs->hw.teles3.cfg_reg) {
			if (cs->typ == ISDN_CTYPE_COMPAQ_ISA) {
				release_region(cs->hw.teles3.cfg_reg, 1);
			} else {
				release_region(cs->hw.teles3.cfg_reg, 8);
			}
		}
		release_ioregs(cs, 0x7);
	}
}

static int
reset_teles3(struct IsdnCardState *cs)
{
	u_char irqcfg;

	if (cs->typ != ISDN_CTYPE_TELESPCMCIA) {
		if ((cs->hw.teles3.cfg_reg) && (cs->typ != ISDN_CTYPE_COMPAQ_ISA)) {
			switch (cs->irq) {
				case 2:
				case 9:
					irqcfg = 0x00;
					break;
				case 3:
					irqcfg = 0x02;
					break;
				case 4:
					irqcfg = 0x04;
					break;
				case 5:
					irqcfg = 0x06;
					break;
				case 10:
					irqcfg = 0x08;
					break;
				case 11:
					irqcfg = 0x0A;
					break;
				case 12:
					irqcfg = 0x0C;
					break;
				case 15:
					irqcfg = 0x0E;
					break;
				default:
					return(1);
			}
			byteout(cs->hw.teles3.cfg_reg + 4, irqcfg);
			HZDELAY(HZ / 10 + 1);
			byteout(cs->hw.teles3.cfg_reg + 4, irqcfg | 1);
			HZDELAY(HZ / 10 + 1);
		} else if (cs->typ == ISDN_CTYPE_COMPAQ_ISA) {
			byteout(cs->hw.teles3.cfg_reg, 0xff);
			HZDELAY(2);
			byteout(cs->hw.teles3.cfg_reg, 0x00);
			HZDELAY(2);
		} else {
			/* Reset off for 16.3 PnP , thanks to Georg Acher */
			byteout(cs->hw.teles3.isac + 0x3c, 0);
			HZDELAY(2);
			byteout(cs->hw.teles3.isac + 0x3c, 1);
			HZDELAY(2);
		}
	}
	return(0);
}

static int
Teles_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
		case CARD_RESET:
			spin_lock_irqsave(&cs->lock, flags);
			reset_teles3(cs);
			spin_unlock_irqrestore(&cs->lock, flags);
			return(0);
		case CARD_RELEASE:
			release_io_teles3(cs);
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

#ifdef __ISAPNP__

static struct isapnp_device_id teles_ids[] __devinitdata = {
	{ ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x2110),
	  ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x2110), 
	  (unsigned long) "Teles 16.3 PnP" },
	{ ISAPNP_VENDOR('C', 'T', 'X'), ISAPNP_FUNCTION(0x0),
	  ISAPNP_VENDOR('C', 'T', 'X'), ISAPNP_FUNCTION(0x0), 
	  (unsigned long) "Creatix 16.3 PnP" },
	{ ISAPNP_VENDOR('C', 'P', 'Q'), ISAPNP_FUNCTION(0x1002),
	  ISAPNP_VENDOR('C', 'P', 'Q'), ISAPNP_FUNCTION(0x1002), 
	  (unsigned long) "Compaq ISDN S0" },
	{ 0, }
};

static struct isapnp_device_id *ipid __devinitdata = &teles_ids[0];
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif

int __devinit
setup_teles3(struct IsdnCard *card)
{
	u_char val;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, teles3_revision);
	printk(KERN_INFO "HiSax: Teles IO driver Rev. %s\n", HiSax_getrev(tmp));
	if ((cs->typ != ISDN_CTYPE_16_3) && (cs->typ != ISDN_CTYPE_PNP)
	    && (cs->typ != ISDN_CTYPE_TELESPCMCIA) && (cs->typ != ISDN_CTYPE_COMPAQ_ISA))
		return (0);

#ifdef __ISAPNP__
	if (!card->para[1] && isapnp_present()) {
		struct pnp_dev *pnp_d;
		while(ipid->card_vendor) {
			if ((pnp_c = pnp_find_card(ipid->card_vendor,
				ipid->card_device, pnp_c))) {
				pnp_d = NULL;
				if ((pnp_d = pnp_find_dev(pnp_c,
					ipid->vendor, ipid->function, pnp_d))) {
					int err;

					printk(KERN_INFO "HiSax: %s detected\n",
						(char *)ipid->driver_data);
					pnp_disable_dev(pnp_d);
					err = pnp_activate_dev(pnp_d);
					if (err<0) {
						printk(KERN_WARNING "%s: pnp_activate_dev ret(%d)\n",
							__func__, err);
						return(0);
					}
					card->para[3] = pnp_port_start(pnp_d, 2);
					card->para[2] = pnp_port_start(pnp_d, 1);
					card->para[1] = pnp_port_start(pnp_d, 0);
					card->para[0] = pnp_irq(pnp_d, 0);
					if (!card->para[0] || !card->para[1] || !card->para[2]) {
						printk(KERN_ERR "Teles PnP:some resources are missing %ld/%lx/%lx\n",
							card->para[0], card->para[1], card->para[2]);
						pnp_disable_dev(pnp_d);
						return(0);
					}
					break;
				} else {
					printk(KERN_ERR "Teles PnP: PnP error card found, no device\n");
				}
			}
			ipid++;
			pnp_c = NULL;
		} 
		if (!ipid->card_vendor) {
			printk(KERN_INFO "Teles PnP: no ISAPnP card found\n");
			return(0);
		}
	}
#endif
	if (cs->typ == ISDN_CTYPE_16_3) {
		cs->hw.teles3.cfg_reg = card->para[1];
		switch (cs->hw.teles3.cfg_reg) {
			case 0x180:
			case 0x280:
			case 0x380:
				cs->hw.teles3.cfg_reg |= 0xc00;
				break;
		}
		cs->hw.teles3.isac = cs->hw.teles3.cfg_reg - 0x420;
		cs->hw.teles3.hscx[0] = cs->hw.teles3.cfg_reg - 0xc20;
		cs->hw.teles3.hscx[1] = cs->hw.teles3.cfg_reg - 0x820;
	} else if (cs->typ == ISDN_CTYPE_TELESPCMCIA) {
		cs->hw.teles3.cfg_reg = 0;
		cs->hw.teles3.hscx[0] = card->para[1] - 0x20;
		cs->hw.teles3.hscx[1] = card->para[1];
		cs->hw.teles3.isac = card->para[1] + 0x20;
	} else if (cs->typ == ISDN_CTYPE_COMPAQ_ISA) {
		cs->hw.teles3.cfg_reg = card->para[3];
		cs->hw.teles3.isac = card->para[2] - 32;
		cs->hw.teles3.hscx[0] = card->para[1] - 32;
		cs->hw.teles3.hscx[1] = card->para[1];
	} else {	/* PNP */
		cs->hw.teles3.cfg_reg = 0;
		cs->hw.teles3.isac = card->para[1] - 32;
		cs->hw.teles3.hscx[0] = card->para[2] - 32;
		cs->hw.teles3.hscx[1] = card->para[2];
	}
	cs->irq = card->para[0];
	cs->hw.teles3.isacfifo = cs->hw.teles3.isac + 0x3e;
	cs->hw.teles3.hscxfifo[0] = cs->hw.teles3.hscx[0] + 0x3e;
	cs->hw.teles3.hscxfifo[1] = cs->hw.teles3.hscx[1] + 0x3e;
	if (cs->typ == ISDN_CTYPE_TELESPCMCIA) {
		if (!request_region(cs->hw.teles3.hscx[1], 96, "HiSax Teles PCMCIA")) {
			printk(KERN_WARNING
			       "HiSax: %s ports %x-%x already in use\n",
			       CardType[cs->typ],
			       cs->hw.teles3.hscx[1],
			       cs->hw.teles3.hscx[1] + 96);
			return (0);
		}
		cs->irq_flags |= IRQF_SHARED; /* cardbus can share */
	} else {
		if (cs->hw.teles3.cfg_reg) {
			if (cs->typ == ISDN_CTYPE_COMPAQ_ISA) {
				if (!request_region(cs->hw.teles3.cfg_reg, 1, "teles3 cfg")) {
					printk(KERN_WARNING
						"HiSax: %s config port %x already in use\n",
						CardType[card->typ],
						cs->hw.teles3.cfg_reg);
					return (0);
				}
			} else {
				if (!request_region(cs->hw.teles3.cfg_reg, 8, "teles3 cfg")) {
					printk(KERN_WARNING
					       "HiSax: %s config port %x-%x already in use\n",
					       CardType[card->typ],
					       cs->hw.teles3.cfg_reg,
						cs->hw.teles3.cfg_reg + 8);
					return (0);
				}
			}
		}
		if (!request_region(cs->hw.teles3.isac + 32, 32, "HiSax isac")) {
			printk(KERN_WARNING
			   "HiSax: %s isac ports %x-%x already in use\n",
			       CardType[cs->typ],
			       cs->hw.teles3.isac + 32,
			       cs->hw.teles3.isac + 64);
			if (cs->hw.teles3.cfg_reg) {
				if (cs->typ == ISDN_CTYPE_COMPAQ_ISA) {
					release_region(cs->hw.teles3.cfg_reg, 1);
				} else {
					release_region(cs->hw.teles3.cfg_reg, 8);
				}
			}
			return (0);
		}
		if (!request_region(cs->hw.teles3.hscx[0] + 32, 32, "HiSax hscx A")) {
			printk(KERN_WARNING
			 "HiSax: %s hscx A ports %x-%x already in use\n",
			       CardType[cs->typ],
			       cs->hw.teles3.hscx[0] + 32,
			       cs->hw.teles3.hscx[0] + 64);
			if (cs->hw.teles3.cfg_reg) {
				if (cs->typ == ISDN_CTYPE_COMPAQ_ISA) {
					release_region(cs->hw.teles3.cfg_reg, 1);
				} else {
					release_region(cs->hw.teles3.cfg_reg, 8);
				}
			}
			release_ioregs(cs, 1);
			return (0);
		}
		if (!request_region(cs->hw.teles3.hscx[1] + 32, 32, "HiSax hscx B")) {
			printk(KERN_WARNING
			 "HiSax: %s hscx B ports %x-%x already in use\n",
			       CardType[cs->typ],
			       cs->hw.teles3.hscx[1] + 32,
			       cs->hw.teles3.hscx[1] + 64);
			if (cs->hw.teles3.cfg_reg) {
				if (cs->typ == ISDN_CTYPE_COMPAQ_ISA) {
					release_region(cs->hw.teles3.cfg_reg, 1);
				} else {
					release_region(cs->hw.teles3.cfg_reg, 8);
				}
			}
			release_ioregs(cs, 3);
			return (0);
		}
	}
	if ((cs->hw.teles3.cfg_reg) && (cs->typ != ISDN_CTYPE_COMPAQ_ISA)) {
		if ((val = bytein(cs->hw.teles3.cfg_reg + 0)) != 0x51) {
			printk(KERN_WARNING "Teles: 16.3 Byte at %x is %x\n",
			       cs->hw.teles3.cfg_reg + 0, val);
			release_io_teles3(cs);
			return (0);
		}
		if ((val = bytein(cs->hw.teles3.cfg_reg + 1)) != 0x93) {
			printk(KERN_WARNING "Teles: 16.3 Byte at %x is %x\n",
			       cs->hw.teles3.cfg_reg + 1, val);
			release_io_teles3(cs);
			return (0);
		}
		val = bytein(cs->hw.teles3.cfg_reg + 2);/* 0x1e=without AB
							 * 0x1f=with AB
							 * 0x1c 16.3 ???
							 * 0x39 16.3 1.1
							 * 0x38 16.3 1.3
							 * 0x46 16.3 with AB + Video (Teles-Vision)
							 */
		if (val != 0x46 && val != 0x39 && val != 0x38 && val != 0x1c && val != 0x1e && val != 0x1f) {
			printk(KERN_WARNING "Teles: 16.3 Byte at %x is %x\n",
			       cs->hw.teles3.cfg_reg + 2, val);
			release_io_teles3(cs);
			return (0);
		}
	}
	printk(KERN_INFO
	       "HiSax: %s config irq:%d isac:0x%X  cfg:0x%X\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.teles3.isac + 32, cs->hw.teles3.cfg_reg);
	printk(KERN_INFO
	       "HiSax: hscx A:0x%X  hscx B:0x%X\n",
	       cs->hw.teles3.hscx[0] + 32, cs->hw.teles3.hscx[1] + 32);

	setup_isac(cs);
	if (reset_teles3(cs)) {
		printk(KERN_WARNING "Teles3: wrong IRQ\n");
		release_io_teles3(cs);
		return (0);
	}
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Teles_card_msg;
	cs->irq_func = &teles3_interrupt;
	ISACVersion(cs, "Teles3:");
	if (HscxVersion(cs, "Teles3:")) {
		printk(KERN_WARNING
		       "Teles3: wrong HSCX versions check IO address\n");
		release_io_teles3(cs);
		return (0);
	}
	return (1);
}
