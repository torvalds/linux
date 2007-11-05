/* $Id: gazel.c,v 2.19.2.4 2004/01/14 16:04:48 keil Exp $
 *
 * low level stuff for Gazel isdn cards
 *
 * Author       BeWan Systems
 *              based on source code from Karsten Keil
 * Copyright    by BeWan Systems
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
#include "ipac.h"
#include <linux/pci.h>

extern const char *CardType[];
static const char *gazel_revision = "$Revision: 2.19.2.4 $";

#define R647      1
#define R685      2
#define R753      3
#define R742      4

#define PLX_CNTRL    0x50	/* registre de controle PLX */
#define RESET_GAZEL  0x4
#define RESET_9050   0x40000000
#define PLX_INCSR    0x4C	/* registre d'IT du 9050 */
#define INT_ISAC_EN  0x8	/* 1 = enable IT isac */
#define INT_ISAC     0x20	/* 1 = IT isac en cours */
#define INT_HSCX_EN  0x1	/* 1 = enable IT hscx */
#define INT_HSCX     0x4	/* 1 = IT hscx en cours */
#define INT_PCI_EN   0x40	/* 1 = enable IT PCI */
#define INT_IPAC_EN  0x3	/* enable IT ipac */


#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

static inline u_char
readreg(unsigned int adr, u_short off)
{
	return bytein(adr + off);
}

static inline void
writereg(unsigned int adr, u_short off, u_char data)
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

static inline u_char
readreg_ipac(unsigned int adr, u_short off)
{
	register u_char ret;

	byteout(adr, off);
	ret = bytein(adr + 4);
	return ret;
}

static inline void
writereg_ipac(unsigned int adr, u_short off, u_char data)
{
	byteout(adr, off);
	byteout(adr + 4, data);
}


static inline void
read_fifo_ipac(unsigned int adr, u_short off, u_char * data, int size)
{
	byteout(adr, off);
	insb(adr + 4, data, size);
}

static void
write_fifo_ipac(unsigned int adr, u_short off, u_char * data, int size)
{
	byteout(adr, off);
	outsb(adr + 4, data, size);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	u_short off2 = offset;

	switch (cs->subtyp) {
		case R647:
			off2 = ((off2 << 8 & 0xf000) | (off2 & 0xf));
		case R685:
			return (readreg(cs->hw.gazel.isac, off2));
		case R753:
		case R742:
			return (readreg_ipac(cs->hw.gazel.ipac, 0x80 + off2));
	}
	return 0;
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	u_short off2 = offset;

	switch (cs->subtyp) {
		case R647:
			off2 = ((off2 << 8 & 0xf000) | (off2 & 0xf));
		case R685:
			writereg(cs->hw.gazel.isac, off2, value);
			break;
		case R753:
		case R742:
			writereg_ipac(cs->hw.gazel.ipac, 0x80 + off2, value);
			break;
	}
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	switch (cs->subtyp) {
		case R647:
		case R685:
			read_fifo(cs->hw.gazel.isacfifo, data, size);
			break;
		case R753:
		case R742:
			read_fifo_ipac(cs->hw.gazel.ipac, 0x80, data, size);
			break;
	}
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	switch (cs->subtyp) {
		case R647:
		case R685:
			write_fifo(cs->hw.gazel.isacfifo, data, size);
			break;
		case R753:
		case R742:
			write_fifo_ipac(cs->hw.gazel.ipac, 0x80, data, size);
			break;
	}
}

static void
ReadHSCXfifo(struct IsdnCardState *cs, int hscx, u_char * data, int size)
{
	switch (cs->subtyp) {
		case R647:
		case R685:
			read_fifo(cs->hw.gazel.hscxfifo[hscx], data, size);
			break;
		case R753:
		case R742:
			read_fifo_ipac(cs->hw.gazel.ipac, hscx * 0x40, data, size);
			break;
	}
}

static void
WriteHSCXfifo(struct IsdnCardState *cs, int hscx, u_char * data, int size)
{
	switch (cs->subtyp) {
		case R647:
		case R685:
			write_fifo(cs->hw.gazel.hscxfifo[hscx], data, size);
			break;
		case R753:
		case R742:
			write_fifo_ipac(cs->hw.gazel.ipac, hscx * 0x40, data, size);
			break;
	}
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	u_short off2 = offset;

	switch (cs->subtyp) {
		case R647:
			off2 = ((off2 << 8 & 0xf000) | (off2 & 0xf));
		case R685:
			return (readreg(cs->hw.gazel.hscx[hscx], off2));
		case R753:
		case R742:
			return (readreg_ipac(cs->hw.gazel.ipac, hscx * 0x40 + off2));
	}
	return 0;
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	u_short off2 = offset;

	switch (cs->subtyp) {
		case R647:
			off2 = ((off2 << 8 & 0xf000) | (off2 & 0xf));
		case R685:
			writereg(cs->hw.gazel.hscx[hscx], off2, value);
			break;
		case R753:
		case R742:
			writereg_ipac(cs->hw.gazel.ipac, hscx * 0x40 + off2, value);
			break;
	}
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) ReadHSCX(cs, nr, reg)
#define WRITEHSCX(cs, nr, reg, data) WriteHSCX(cs, nr, reg, data)
#define READHSCXFIFO(cs, nr, ptr, cnt) ReadHSCXfifo(cs, nr, ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) WriteHSCXfifo(cs, nr, ptr, cnt)

#include "hscx_irq.c"

static irqreturn_t
gazel_interrupt(int intno, void *dev_id)
{
#define MAXCOUNT 5
	struct IsdnCardState *cs = dev_id;
	u_char valisac, valhscx;
	int count = 0;
	u_long flags;

	spin_lock_irqsave(&cs->lock, flags);
	do {
		valhscx = ReadHSCX(cs, 1, HSCX_ISTA);
		if (valhscx)
			hscx_int_main(cs, valhscx);
		valisac = ReadISAC(cs, ISAC_ISTA);
		if (valisac)
			isac_interrupt(cs, valisac);
		count++;
	} while ((valhscx || valisac) && (count < MAXCOUNT));

	WriteHSCX(cs, 0, HSCX_MASK, 0xFF);
	WriteHSCX(cs, 1, HSCX_MASK, 0xFF);
	WriteISAC(cs, ISAC_MASK, 0xFF);
	WriteISAC(cs, ISAC_MASK, 0x0);
	WriteHSCX(cs, 0, HSCX_MASK, 0x0);
	WriteHSCX(cs, 1, HSCX_MASK, 0x0);
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}


static irqreturn_t
gazel_interrupt_ipac(int intno, void *dev_id)
{
	struct IsdnCardState *cs = dev_id;
	u_char ista, val;
	int count = 0;
	u_long flags;
	
	spin_lock_irqsave(&cs->lock, flags);
	ista = ReadISAC(cs, IPAC_ISTA - 0x80);
	do {
		if (ista & 0x0f) {
			val = ReadHSCX(cs, 1, HSCX_ISTA);
			if (ista & 0x01)
				val |= 0x01;
			if (ista & 0x04)
				val |= 0x02;
			if (ista & 0x08)
				val |= 0x04;
			if (val) {
				hscx_int_main(cs, val);
			}
		}
		if (ista & 0x20) {
			val = 0xfe & ReadISAC(cs, ISAC_ISTA);
			if (val) {
				isac_interrupt(cs, val);
			}
		}
		if (ista & 0x10) {
			val = 0x01;
			isac_interrupt(cs, val);
		}
		ista = ReadISAC(cs, IPAC_ISTA - 0x80);
		count++;
	}
	while ((ista & 0x3f) && (count < MAXCOUNT));

	WriteISAC(cs, IPAC_MASK - 0x80, 0xFF);
	WriteISAC(cs, IPAC_MASK - 0x80, 0xC0);
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

static void
release_io_gazel(struct IsdnCardState *cs)
{
	unsigned int i;

	switch (cs->subtyp) {
		case R647:
			for (i = 0x0000; i < 0xC000; i += 0x1000)
				release_region(i + cs->hw.gazel.hscx[0], 16);
			release_region(0xC000 + cs->hw.gazel.hscx[0], 1);
			break;

		case R685:
			release_region(cs->hw.gazel.hscx[0], 0x100);
			release_region(cs->hw.gazel.cfg_reg, 0x80);
			break;

		case R753:
			release_region(cs->hw.gazel.ipac, 0x8);
			release_region(cs->hw.gazel.cfg_reg, 0x80);
			break;

		case R742:
			release_region(cs->hw.gazel.ipac, 8);
			break;
	}
}

static int
reset_gazel(struct IsdnCardState *cs)
{
	unsigned long plxcntrl, addr = cs->hw.gazel.cfg_reg;

	switch (cs->subtyp) {
		case R647:
			writereg(addr, 0, 0);
			HZDELAY(10);
			writereg(addr, 0, 1);
			HZDELAY(2);
			break;
		case R685:
			plxcntrl = inl(addr + PLX_CNTRL);
			plxcntrl |= (RESET_9050 + RESET_GAZEL);
			outl(plxcntrl, addr + PLX_CNTRL);
			plxcntrl &= ~(RESET_9050 + RESET_GAZEL);
			HZDELAY(4);
			outl(plxcntrl, addr + PLX_CNTRL);
			HZDELAY(10);
			outb(INT_ISAC_EN + INT_HSCX_EN + INT_PCI_EN, addr + PLX_INCSR);
			break;
		case R753:
			plxcntrl = inl(addr + PLX_CNTRL);
			plxcntrl |= (RESET_9050 + RESET_GAZEL);
			outl(plxcntrl, addr + PLX_CNTRL);
			plxcntrl &= ~(RESET_9050 + RESET_GAZEL);
			WriteISAC(cs, IPAC_POTA2 - 0x80, 0x20);
			HZDELAY(4);
			outl(plxcntrl, addr + PLX_CNTRL);
			HZDELAY(10);
			WriteISAC(cs, IPAC_POTA2 - 0x80, 0x00);
			WriteISAC(cs, IPAC_ACFG - 0x80, 0xff);
			WriteISAC(cs, IPAC_AOE - 0x80, 0x0);
			WriteISAC(cs, IPAC_MASK - 0x80, 0xff);
			WriteISAC(cs, IPAC_CONF - 0x80, 0x1);
			outb(INT_IPAC_EN + INT_PCI_EN, addr + PLX_INCSR);
			WriteISAC(cs, IPAC_MASK - 0x80, 0xc0);
			break;
		case R742:
			WriteISAC(cs, IPAC_POTA2 - 0x80, 0x20);
			HZDELAY(4);
			WriteISAC(cs, IPAC_POTA2 - 0x80, 0x00);
			WriteISAC(cs, IPAC_ACFG - 0x80, 0xff);
			WriteISAC(cs, IPAC_AOE - 0x80, 0x0);
			WriteISAC(cs, IPAC_MASK - 0x80, 0xff);
			WriteISAC(cs, IPAC_CONF - 0x80, 0x1);
			WriteISAC(cs, IPAC_MASK - 0x80, 0xc0);
			break;
	}
	return (0);
}

static int
Gazel_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
		case CARD_RESET:
			spin_lock_irqsave(&cs->lock, flags);
			reset_gazel(cs);
			spin_unlock_irqrestore(&cs->lock, flags);
			return (0);
		case CARD_RELEASE:
			release_io_gazel(cs);
			return (0);
		case CARD_INIT:
			spin_lock_irqsave(&cs->lock, flags);
			inithscxisac(cs, 1);
			if ((cs->subtyp==R647)||(cs->subtyp==R685)) {
				int i;
				for (i=0;i<(2+MAX_WAITING_CALLS);i++) {
					cs->bcs[i].hw.hscx.tsaxr0 = 0x1f;
					cs->bcs[i].hw.hscx.tsaxr1 = 0x23;
				}
			}
			spin_unlock_irqrestore(&cs->lock, flags);
			return (0);
		case CARD_TEST:
			return (0);
	}
	return (0);
}

static int
reserve_regions(struct IsdnCard *card, struct IsdnCardState *cs)
{
	unsigned int i, j, base = 0, adr = 0, len = 0;

	switch (cs->subtyp) {
		case R647:
			base = cs->hw.gazel.hscx[0];
			if (!request_region(adr = (0xC000 + base), len = 1, "gazel"))
				goto error;
			for (i = 0x0000; i < 0xC000; i += 0x1000) {
				if (!request_region(adr = (i + base), len = 16, "gazel"))
					goto error;
			}
			if (i != 0xC000) {
				for (j = 0; j < i; j+= 0x1000)
					release_region(j + base, 16);
				release_region(0xC000 + base, 1);
				goto error;
			}
			break;

		case R685:
			if (!request_region(adr = cs->hw.gazel.hscx[0], len = 0x100, "gazel"))
				goto error;
			if (!request_region(adr = cs->hw.gazel.cfg_reg, len = 0x80, "gazel")) {
				release_region(cs->hw.gazel.hscx[0],0x100);
				goto error;
			}
			break;

		case R753:
			if (!request_region(adr = cs->hw.gazel.ipac, len = 0x8, "gazel"))
				goto error;
			if (!request_region(adr = cs->hw.gazel.cfg_reg, len = 0x80, "gazel")) {
				release_region(cs->hw.gazel.ipac, 8);
				goto error;
			}
			break;

		case R742:
			if (!request_region(adr = cs->hw.gazel.ipac, len = 0x8, "gazel"))
				goto error;
			break;
	}

	return 0;

      error:
	printk(KERN_WARNING "Gazel: %s io ports 0x%x-0x%x already in use\n",
	       CardType[cs->typ], adr, adr + len);
	return 1;
}

static int __devinit
setup_gazelisa(struct IsdnCard *card, struct IsdnCardState *cs)
{
	printk(KERN_INFO "Gazel: ISA PnP card automatic recognition\n");
	// we got an irq parameter, assume it is an ISA card
	// R742 decodes address even in not started...
	// R647 returns FF if not present or not started
	// eventually needs improvment
	if (readreg_ipac(card->para[1], IPAC_ID) == 1)
		cs->subtyp = R742;
	else
		cs->subtyp = R647;

	setup_isac(cs);
	cs->hw.gazel.cfg_reg = card->para[1] + 0xC000;
	cs->hw.gazel.ipac = card->para[1];
	cs->hw.gazel.isac = card->para[1] + 0x8000;
	cs->hw.gazel.hscx[0] = card->para[1];
	cs->hw.gazel.hscx[1] = card->para[1] + 0x4000;
	cs->irq = card->para[0];
	cs->hw.gazel.isacfifo = cs->hw.gazel.isac;
	cs->hw.gazel.hscxfifo[0] = cs->hw.gazel.hscx[0];
	cs->hw.gazel.hscxfifo[1] = cs->hw.gazel.hscx[1];

	switch (cs->subtyp) {
		case R647:
			printk(KERN_INFO "Gazel: Card ISA R647/R648 found\n");
			cs->dc.isac.adf2 = 0x87;
			printk(KERN_INFO
				"Gazel: config irq:%d isac:0x%X  cfg:0x%X\n",
				cs->irq, cs->hw.gazel.isac, cs->hw.gazel.cfg_reg);
			printk(KERN_INFO
				"Gazel: hscx A:0x%X  hscx B:0x%X\n",
				cs->hw.gazel.hscx[0], cs->hw.gazel.hscx[1]);

			break;
		case R742:
			printk(KERN_INFO "Gazel: Card ISA R742 found\n");
			test_and_set_bit(HW_IPAC, &cs->HW_Flags);
			printk(KERN_INFO
			       "Gazel: config irq:%d ipac:0x%X\n",
			       cs->irq, cs->hw.gazel.ipac);
			break;
	}

	return (0);
}

#ifdef CONFIG_PCI_LEGACY
static struct pci_dev *dev_tel __devinitdata = NULL;

static int __devinit
setup_gazelpci(struct IsdnCardState *cs)
{
	u_int pci_ioaddr0 = 0, pci_ioaddr1 = 0;
	u_char pci_irq = 0, found;
	u_int nbseek, seekcard;

	printk(KERN_WARNING "Gazel: PCI card automatic recognition\n");

	found = 0;
	seekcard = PCI_DEVICE_ID_PLX_R685;
	for (nbseek = 0; nbseek < 4; nbseek++) {
		if ((dev_tel = pci_find_device(PCI_VENDOR_ID_PLX,
					seekcard, dev_tel))) {
			if (pci_enable_device(dev_tel))
				return 1;
			pci_irq = dev_tel->irq;
			pci_ioaddr0 = pci_resource_start(dev_tel, 1);
			pci_ioaddr1 = pci_resource_start(dev_tel, 2);
			found = 1;
		}
		if (found)
			break;
		else {
			switch (seekcard) {
				case PCI_DEVICE_ID_PLX_R685:
					seekcard = PCI_DEVICE_ID_PLX_R753;
					break;
				case PCI_DEVICE_ID_PLX_R753:
					seekcard = PCI_DEVICE_ID_PLX_DJINN_ITOO;
					break;
				case PCI_DEVICE_ID_PLX_DJINN_ITOO:
					seekcard = PCI_DEVICE_ID_PLX_OLITEC;
					break;
			}
		}
	}
	if (!found) {
		printk(KERN_WARNING "Gazel: No PCI card found\n");
		return (1);
	}
	if (!pci_irq) {
		printk(KERN_WARNING "Gazel: No IRQ for PCI card found\n");
		return 1;
	}
	cs->hw.gazel.pciaddr[0] = pci_ioaddr0;
	cs->hw.gazel.pciaddr[1] = pci_ioaddr1;
	setup_isac(cs);
	pci_ioaddr1 &= 0xfffe;
	cs->hw.gazel.cfg_reg = pci_ioaddr0 & 0xfffe;
	cs->hw.gazel.ipac = pci_ioaddr1;
	cs->hw.gazel.isac = pci_ioaddr1 + 0x80;
	cs->hw.gazel.hscx[0] = pci_ioaddr1;
	cs->hw.gazel.hscx[1] = pci_ioaddr1 + 0x40;
	cs->hw.gazel.isacfifo = cs->hw.gazel.isac;
	cs->hw.gazel.hscxfifo[0] = cs->hw.gazel.hscx[0];
	cs->hw.gazel.hscxfifo[1] = cs->hw.gazel.hscx[1];
	cs->irq = pci_irq;
	cs->irq_flags |= IRQF_SHARED;

	switch (seekcard) {
		case PCI_DEVICE_ID_PLX_R685:
			printk(KERN_INFO "Gazel: Card PCI R685 found\n");
			cs->subtyp = R685;
			cs->dc.isac.adf2 = 0x87;
			printk(KERN_INFO
			    "Gazel: config irq:%d isac:0x%X  cfg:0x%X\n",
			cs->irq, cs->hw.gazel.isac, cs->hw.gazel.cfg_reg);
			printk(KERN_INFO
			       "Gazel: hscx A:0x%X  hscx B:0x%X\n",
			     cs->hw.gazel.hscx[0], cs->hw.gazel.hscx[1]);
			break;
		case PCI_DEVICE_ID_PLX_R753:
		case PCI_DEVICE_ID_PLX_DJINN_ITOO:
		case PCI_DEVICE_ID_PLX_OLITEC:
			printk(KERN_INFO "Gazel: Card PCI R753 found\n");
			cs->subtyp = R753;
			test_and_set_bit(HW_IPAC, &cs->HW_Flags);
			printk(KERN_INFO
			    "Gazel: config irq:%d ipac:0x%X  cfg:0x%X\n",
			cs->irq, cs->hw.gazel.ipac, cs->hw.gazel.cfg_reg);
			break;
	}

	return (0);
}
#endif /* CONFIG_PCI_LEGACY */

int __devinit
setup_gazel(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	u_char val;

	strcpy(tmp, gazel_revision);
	printk(KERN_INFO "Gazel: Driver Revision %s\n", HiSax_getrev(tmp));

	if (cs->typ != ISDN_CTYPE_GAZEL)
		return (0);

	if (card->para[0]) {
		if (setup_gazelisa(card, cs))
			return (0);
	} else {

#ifdef CONFIG_PCI_LEGACY
		if (setup_gazelpci(cs))
			return (0);
#else
		printk(KERN_WARNING "Gazel: Card PCI requested and NO_PCI_BIOS, unable to config\n");
		return (0);
#endif				/* CONFIG_PCI */
	}

	if (reserve_regions(card, cs)) {
		return (0);
	}
	if (reset_gazel(cs)) {
		printk(KERN_WARNING "Gazel: wrong IRQ\n");
		release_io_gazel(cs);
		return (0);
	}
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Gazel_card_msg;

	switch (cs->subtyp) {
		case R647:
		case R685:
			cs->irq_func = &gazel_interrupt;
			ISACVersion(cs, "Gazel:");
			if (HscxVersion(cs, "Gazel:")) {
				printk(KERN_WARNING
				       "Gazel: wrong HSCX versions check IO address\n");
				release_io_gazel(cs);
				return (0);
			}
			break;
		case R742:
		case R753:
			cs->irq_func = &gazel_interrupt_ipac;
			val = ReadISAC(cs, IPAC_ID - 0x80);
			printk(KERN_INFO "Gazel: IPAC version %x\n", val);
			break;
	}

	return (1);
}
