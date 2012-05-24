/* $Id: niccy.c,v 1.21.2.4 2004/01/13 23:48:39 keil Exp $
 *
 * low level stuff for Dr. Neuhaus NICCY PnP and NICCY PCI and
 * compatible (SAGEM cybermodem)
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to Dr. Neuhaus and SAGEM for information
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/isapnp.h>

static const char *niccy_revision = "$Revision: 1.21.2.4 $";

#define byteout(addr, val) outb(val, addr)
#define bytein(addr) inb(addr)

#define ISAC_PCI_DATA	0
#define HSCX_PCI_DATA	1
#define ISAC_PCI_ADDR	2
#define HSCX_PCI_ADDR	3
#define ISAC_PNP	0
#define HSCX_PNP	1

/* SUB Types */
#define NICCY_PNP	1
#define NICCY_PCI	2

/* PCI stuff */
#define PCI_IRQ_CTRL_REG	0x38
#define PCI_IRQ_ENABLE		0x1f00
#define PCI_IRQ_DISABLE		0xff0000
#define PCI_IRQ_ASSERT		0x800000

static inline u_char readreg(unsigned int ale, unsigned int adr, u_char off)
{
	register u_char ret;

	byteout(ale, off);
	ret = bytein(adr);
	return ret;
}

static inline void readfifo(unsigned int ale, unsigned int adr, u_char off,
			    u_char *data, int size)
{
	byteout(ale, off);
	insb(adr, data, size);
}

static inline void writereg(unsigned int ale, unsigned int adr, u_char off,
			    u_char data)
{
	byteout(ale, off);
	byteout(adr, data);
}

static inline void writefifo(unsigned int ale, unsigned int adr, u_char off,
			     u_char *data, int size)
{
	byteout(ale, off);
	outsb(adr, data, size);
}

/* Interface functions */

static u_char ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return readreg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, offset);
}

static void WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, offset, value);
}

static void ReadISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	readfifo(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, 0, data, size);
}

static void WriteISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	writefifo(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, 0, data, size);
}

static u_char ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return readreg(cs->hw.niccy.hscx_ale,
		       cs->hw.niccy.hscx, offset + (hscx ? 0x40 : 0));
}

static void WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset,
		      u_char value)
{
	writereg(cs->hw.niccy.hscx_ale,
		 cs->hw.niccy.hscx, offset + (hscx ? 0x40 : 0), value);
}

#define READHSCX(cs, nr, reg) readreg(cs->hw.niccy.hscx_ale,		\
				      cs->hw.niccy.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.niccy.hscx_ale,	\
					      cs->hw.niccy.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.niccy.hscx_ale,	\
						cs->hw.niccy.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.niccy.hscx_ale, \
						  cs->hw.niccy.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static irqreturn_t niccy_interrupt(int intno, void *dev_id)
{
	struct IsdnCardState *cs = dev_id;
	u_char val;
	u_long flags;

	spin_lock_irqsave(&cs->lock, flags);
	if (cs->subtyp == NICCY_PCI) {
		int ival;
		ival = inl(cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
		if (!(ival & PCI_IRQ_ASSERT)) {	/* IRQ not for us (shared) */
			spin_unlock_irqrestore(&cs->lock, flags);
			return IRQ_NONE;
		}
		outl(ival, cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
	}
	val = readreg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx,
		      HSCX_ISTA + 0x40);
Start_HSCX:
	if (val)
		hscx_int_main(cs, val);
	val = readreg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, ISAC_ISTA);
Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	val = readreg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx,
		      HSCX_ISTA + 0x40);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	writereg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_MASK, 0xFF);
	writereg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_MASK + 0x40,
		 0xFF);
	writereg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, ISAC_MASK, 0xFF);
	writereg(cs->hw.niccy.isac_ale, cs->hw.niccy.isac, ISAC_MASK, 0);
	writereg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_MASK, 0);
	writereg(cs->hw.niccy.hscx_ale, cs->hw.niccy.hscx, HSCX_MASK + 0x40, 0);
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

static void release_io_niccy(struct IsdnCardState *cs)
{
	if (cs->subtyp == NICCY_PCI) {
		int val;

		val = inl(cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
		val &= PCI_IRQ_DISABLE;
		outl(val, cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
		release_region(cs->hw.niccy.cfg_reg, 0x40);
		release_region(cs->hw.niccy.isac, 4);
	} else {
		release_region(cs->hw.niccy.isac, 2);
		release_region(cs->hw.niccy.isac_ale, 2);
	}
}

static void niccy_reset(struct IsdnCardState *cs)
{
	if (cs->subtyp == NICCY_PCI) {
		int val;

		val = inl(cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
		val |= PCI_IRQ_ENABLE;
		outl(val, cs->hw.niccy.cfg_reg + PCI_IRQ_CTRL_REG);
	}
	inithscxisac(cs, 3);
}

static int niccy_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
	case CARD_RESET:
		spin_lock_irqsave(&cs->lock, flags);
		niccy_reset(cs);
		spin_unlock_irqrestore(&cs->lock, flags);
		return 0;
	case CARD_RELEASE:
		release_io_niccy(cs);
		return 0;
	case CARD_INIT:
		spin_lock_irqsave(&cs->lock, flags);
		niccy_reset(cs);
		spin_unlock_irqrestore(&cs->lock, flags);
		return 0;
	case CARD_TEST:
		return 0;
	}
	return 0;
}

#ifdef __ISAPNP__
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif

int __devinit setup_niccy(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, niccy_revision);
	printk(KERN_INFO "HiSax: Niccy driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_NICCY)
		return 0;
#ifdef __ISAPNP__
	if (!card->para[1] && isapnp_present()) {
		struct pnp_dev *pnp_d = NULL;
		int err;

		pnp_c = pnp_find_card(ISAPNP_VENDOR('S', 'D', 'A'),
				      ISAPNP_FUNCTION(0x0150), pnp_c);
		if (pnp_c) {
			pnp_d = pnp_find_dev(pnp_c,
					     ISAPNP_VENDOR('S', 'D', 'A'),
					     ISAPNP_FUNCTION(0x0150), pnp_d);
			if (!pnp_d) {
				printk(KERN_ERR "NiccyPnP: PnP error card "
				       "found, no device\n");
				return 0;
			}
			pnp_disable_dev(pnp_d);
			err = pnp_activate_dev(pnp_d);
			if (err < 0) {
				printk(KERN_WARNING "%s: pnp_activate_dev "
				       "ret(%d)\n", __func__, err);
				return 0;
			}
			card->para[1] = pnp_port_start(pnp_d, 0);
			card->para[2] = pnp_port_start(pnp_d, 1);
			card->para[0] = pnp_irq(pnp_d, 0);
			if (!card->para[0] || !card->para[1] ||
			    !card->para[2]) {
				printk(KERN_ERR "NiccyPnP:some resources are "
				       "missing %ld/%lx/%lx\n",
				       card->para[0], card->para[1],
				       card->para[2]);
				pnp_disable_dev(pnp_d);
				return 0;
			}
		} else
			printk(KERN_INFO "NiccyPnP: no ISAPnP card found\n");
	}
#endif
	if (card->para[1]) {
		cs->hw.niccy.isac = card->para[1] + ISAC_PNP;
		cs->hw.niccy.hscx = card->para[1] + HSCX_PNP;
		cs->hw.niccy.isac_ale = card->para[2] + ISAC_PNP;
		cs->hw.niccy.hscx_ale = card->para[2] + HSCX_PNP;
		cs->hw.niccy.cfg_reg = 0;
		cs->subtyp = NICCY_PNP;
		cs->irq = card->para[0];
		if (!request_region(cs->hw.niccy.isac, 2, "niccy data")) {
			printk(KERN_WARNING "HiSax: NICCY data port %x-%x "
			       "already in use\n",
			       cs->hw.niccy.isac, cs->hw.niccy.isac + 1);
			return 0;
		}
		if (!request_region(cs->hw.niccy.isac_ale, 2, "niccy addr")) {
			printk(KERN_WARNING "HiSax: NICCY address port %x-%x "
			       "already in use\n",
			       cs->hw.niccy.isac_ale,
			       cs->hw.niccy.isac_ale + 1);
			release_region(cs->hw.niccy.isac, 2);
			return 0;
		}
	} else {
#ifdef CONFIG_PCI
		static struct pci_dev *niccy_dev __devinitdata;

		u_int pci_ioaddr;
		cs->subtyp = 0;
		if ((niccy_dev = hisax_find_pci_device(PCI_VENDOR_ID_SATSAGEM,
						       PCI_DEVICE_ID_SATSAGEM_NICCY,
						       niccy_dev))) {
			if (pci_enable_device(niccy_dev))
				return 0;
			/* get IRQ */
			if (!niccy_dev->irq) {
				printk(KERN_WARNING
				       "Niccy: No IRQ for PCI card found\n");
				return 0;
			}
			cs->irq = niccy_dev->irq;
			cs->hw.niccy.cfg_reg = pci_resource_start(niccy_dev, 0);
			if (!cs->hw.niccy.cfg_reg) {
				printk(KERN_WARNING
				       "Niccy: No IO-Adr for PCI cfg found\n");
				return 0;
			}
			pci_ioaddr = pci_resource_start(niccy_dev, 1);
			if (!pci_ioaddr) {
				printk(KERN_WARNING
				       "Niccy: No IO-Adr for PCI card found\n");
				return 0;
			}
			cs->subtyp = NICCY_PCI;
		} else {
			printk(KERN_WARNING "Niccy: No PCI card found\n");
			return 0;
		}
		cs->irq_flags |= IRQF_SHARED;
		cs->hw.niccy.isac = pci_ioaddr + ISAC_PCI_DATA;
		cs->hw.niccy.isac_ale = pci_ioaddr + ISAC_PCI_ADDR;
		cs->hw.niccy.hscx = pci_ioaddr + HSCX_PCI_DATA;
		cs->hw.niccy.hscx_ale = pci_ioaddr + HSCX_PCI_ADDR;
		if (!request_region(cs->hw.niccy.isac, 4, "niccy")) {
			printk(KERN_WARNING
			       "HiSax: NICCY data port %x-%x already in use\n",
			       cs->hw.niccy.isac, cs->hw.niccy.isac + 4);
			return 0;
		}
		if (!request_region(cs->hw.niccy.cfg_reg, 0x40, "niccy pci")) {
			printk(KERN_WARNING
			       "HiSax: NICCY pci port %x-%x already in use\n",
			       cs->hw.niccy.cfg_reg,
			       cs->hw.niccy.cfg_reg + 0x40);
			release_region(cs->hw.niccy.isac, 4);
			return 0;
		}
#else
		printk(KERN_WARNING "Niccy: io0 0 and NO_PCI_BIOS\n");
		printk(KERN_WARNING "Niccy: unable to config NICCY PCI\n");
		return 0;
#endif				/* CONFIG_PCI */
	}
	printk(KERN_INFO "HiSax: NICCY %s config irq:%d data:0x%X ale:0x%X\n",
	       (cs->subtyp == 1) ? "PnP" : "PCI",
	       cs->irq, cs->hw.niccy.isac, cs->hw.niccy.isac_ale);
	setup_isac(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &niccy_card_msg;
	cs->irq_func = &niccy_interrupt;
	ISACVersion(cs, "Niccy:");
	if (HscxVersion(cs, "Niccy:")) {
		printk(KERN_WARNING "Niccy: wrong HSCX versions check IO "
		       "address\n");
		release_io_niccy(cs);
		return 0;
	}
	return 1;
}
