/* $Id: nj_s.c,v 2.13.2.4 2004/01/16 01:53:48 keil Exp $
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/ppp_defs.h>
#include "netjet.h"

static const char *NETjet_S_revision = "$Revision: 2.13.2.4 $";

static u_char dummyrr(struct IsdnCardState *cs, int chan, u_char off)
{
	return(5);
}

static void dummywr(struct IsdnCardState *cs, int chan, u_char off, u_char value)
{
}

static irqreturn_t
netjet_s_interrupt(int intno, void *dev_id)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, s1val, s0val;
	u_long flags;

	spin_lock_irqsave(&cs->lock, flags);
	s1val = bytein(cs->hw.njet.base + NETJET_IRQSTAT1);
	if (!(s1val & NETJET_ISACIRQ)) {
		val = NETjet_ReadIC(cs, ISAC_ISTA);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "tiger: i1 %x %x", s1val, val);
		if (val) {
			isac_interrupt(cs, val);
			NETjet_WriteIC(cs, ISAC_MASK, 0xFF);
			NETjet_WriteIC(cs, ISAC_MASK, 0x0);
		}
		s1val = 1;
	} else
		s1val = 0;
	/* 
	 * read/write stat0 is better, because lower IRQ rate
	 * Note the IRQ is on for 125 us if a condition match
	 * thats long on modern CPU and so the IRQ is reentered
	 * all the time.
	 */
	s0val = bytein(cs->hw.njet.base + NETJET_IRQSTAT0);
	if ((s0val | s1val)==0) { // shared IRQ
		spin_unlock_irqrestore(&cs->lock, flags);
		return IRQ_NONE;
	} 
	if (s0val)
		byteout(cs->hw.njet.base + NETJET_IRQSTAT0, s0val);
	/* start new code 13/07/00 GE */
	/* set bits in sval to indicate which page is free */
	if (inl(cs->hw.njet.base + NETJET_DMA_WRITE_ADR) <
		inl(cs->hw.njet.base + NETJET_DMA_WRITE_IRQ))
		/* the 2nd write page is free */
		s0val = 0x08;
	else	/* the 1st write page is free */
		s0val = 0x04;	
	if (inl(cs->hw.njet.base + NETJET_DMA_READ_ADR) <
		inl(cs->hw.njet.base + NETJET_DMA_READ_IRQ))
		/* the 2nd read page is free */
		s0val |= 0x02;
	else	/* the 1st read page is free */
		s0val |= 0x01;	
	if (s0val != cs->hw.njet.last_is0) /* we have a DMA interrupt */
	{
		if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
			printk(KERN_WARNING "nj LOCK_ATOMIC s0val %x->%x\n",
				cs->hw.njet.last_is0, s0val);
			spin_unlock_irqrestore(&cs->lock, flags);
			return IRQ_HANDLED;
		}
		cs->hw.njet.irqstat0 = s0val;
		if ((cs->hw.njet.irqstat0 & NETJET_IRQM0_READ) != 
			(cs->hw.njet.last_is0 & NETJET_IRQM0_READ))
			/* we have a read dma int */
			read_tiger(cs);
		if ((cs->hw.njet.irqstat0 & NETJET_IRQM0_WRITE) !=
			(cs->hw.njet.last_is0 & NETJET_IRQM0_WRITE))
			/* we have a write dma int */
			write_tiger(cs);
		/* end new code 13/07/00 GE */
		test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

static void
reset_netjet_s(struct IsdnCardState *cs)
{
	cs->hw.njet.ctrl_reg = 0xff;  /* Reset On */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	mdelay(10);
	/* now edge triggered for TJ320 GE 13/07/00 */
	/* see comment in IRQ function */
	if (cs->subtyp) /* TJ320 */
		cs->hw.njet.ctrl_reg = 0x40;  /* Reset Off and status read clear */
	else
		cs->hw.njet.ctrl_reg = 0x00;  /* Reset Off and status read clear */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	mdelay(10);
	cs->hw.njet.auxd = 0;
	cs->hw.njet.dmactrl = 0;
	byteout(cs->hw.njet.base + NETJET_AUXCTRL, ~NETJET_ISACIRQ);
	byteout(cs->hw.njet.base + NETJET_IRQMASK1, NETJET_ISACIRQ);
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
}

static int
NETjet_S_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
		case CARD_RESET:
			spin_lock_irqsave(&cs->lock, flags);
			reset_netjet_s(cs);
			spin_unlock_irqrestore(&cs->lock, flags);
			return(0);
		case CARD_RELEASE:
			release_io_netjet(cs);
			return(0);
		case CARD_INIT:
			reset_netjet_s(cs);
			inittiger(cs);
			spin_lock_irqsave(&cs->lock, flags);
			clear_pending_isac_ints(cs);
			initisac(cs);
			/* Reenable all IRQ */
			cs->writeisac(cs, ISAC_MASK, 0);
			spin_unlock_irqrestore(&cs->lock, flags);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

static int __devinit njs_pci_probe(struct pci_dev *dev_netjet,
				   struct IsdnCardState *cs)
{
	u32 cfg;

	if (pci_enable_device(dev_netjet))
		return(0);
	pci_set_master(dev_netjet);
	cs->irq = dev_netjet->irq;
	if (!cs->irq) {
		printk(KERN_WARNING "NETjet-S: No IRQ for PCI card found\n");
		return(0);
	}
	cs->hw.njet.base = pci_resource_start(dev_netjet, 0);
	if (!cs->hw.njet.base) {
		printk(KERN_WARNING "NETjet-S: No IO-Adr for PCI card found\n");
		return(0);
	}
	/* the TJ300 and TJ320 must be detected, the IRQ handling is different
	 * unfortunatly the chips use the same device ID, but the TJ320 has
	 * the bit20 in status PCI cfg register set
	 */
	pci_read_config_dword(dev_netjet, 0x04, &cfg);
	if (cfg & 0x00100000)
		cs->subtyp = 1; /* TJ320 */
	else
		cs->subtyp = 0; /* TJ300 */
	/* 2001/10/04 Christoph Ersfeld, Formula-n Europe AG www.formula-n.com */
	if ((dev_netjet->subsystem_vendor == 0x55) &&
		(dev_netjet->subsystem_device == 0x02)) {
		printk(KERN_WARNING "Netjet: You tried to load this driver with an incompatible TigerJet-card\n");
		printk(KERN_WARNING "Use type=41 for Formula-n enter:now ISDN PCI and compatible\n");
		return(0);
	}
	/* end new code */

	return(1);
}

static int __devinit njs_cs_init(struct IsdnCard *card,
				 struct IsdnCardState *cs)
{

	cs->hw.njet.auxa = cs->hw.njet.base + NETJET_AUXDATA;
	cs->hw.njet.isac = cs->hw.njet.base | NETJET_ISAC_OFF;

	cs->hw.njet.ctrl_reg = 0xff;  /* Reset On */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	mdelay(10);

	cs->hw.njet.ctrl_reg = 0x00;  /* Reset Off and status read clear */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	mdelay(10);

	cs->hw.njet.auxd = 0xC0;
	cs->hw.njet.dmactrl = 0;

	byteout(cs->hw.njet.base + NETJET_AUXCTRL, ~NETJET_ISACIRQ);
	byteout(cs->hw.njet.base + NETJET_IRQMASK1, NETJET_ISACIRQ);
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);

	switch ( ( ( NETjet_ReadIC( cs, ISAC_RBCH ) >> 5 ) & 3 ) )
	{
		case 0 :
			return 1;	/* end loop */

		case 3 :
			printk( KERN_WARNING "NETjet-S: NETspider-U PCI card found\n" );
			return -1;	/* continue looping */

		default :
			printk( KERN_WARNING "NETjet-S: No PCI card found\n" );
			return 0;	/* end loop & function */
	}
	return 1;			/* end loop */
}

static int __devinit njs_cs_init_rest(struct IsdnCard *card,
				      struct IsdnCardState *cs)
{
	const int bytecnt = 256;

	printk(KERN_INFO
		"NETjet-S: %s card configured at %#lx IRQ %d\n",
		cs->subtyp ? "TJ320" : "TJ300", cs->hw.njet.base, cs->irq);
	if (!request_region(cs->hw.njet.base, bytecnt, "netjet-s isdn")) {
		printk(KERN_WARNING
		       "HiSax: NETjet-S config port %#lx-%#lx already in use\n",
		       cs->hw.njet.base,
		       cs->hw.njet.base + bytecnt);
		return (0);
	}
	cs->readisac  = &NETjet_ReadIC;
	cs->writeisac = &NETjet_WriteIC;
	cs->readisacfifo  = &NETjet_ReadICfifo;
	cs->writeisacfifo = &NETjet_WriteICfifo;
	cs->BC_Read_Reg  = &dummyrr;
	cs->BC_Write_Reg = &dummywr;
	cs->BC_Send_Data = &netjet_fill_dma;
	setup_isac(cs);
	cs->cardmsg = &NETjet_S_card_msg;
	cs->irq_func = &netjet_s_interrupt;
	cs->irq_flags |= IRQF_SHARED;
	ISACVersion(cs, "NETjet-S:");

	return (1);
}

static struct pci_dev *dev_netjet __devinitdata = NULL;

int __devinit
setup_netjet_s(struct IsdnCard *card)
{
	int ret;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

#ifdef __BIG_ENDIAN
#error "not running on big endian machines now"
#endif
	strcpy(tmp, NETjet_S_revision);
	printk(KERN_INFO "HiSax: Traverse Tech. NETjet-S driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_NETJET_S)
		return(0);
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);

	for ( ;; )
	{
		if ((dev_netjet = hisax_find_pci_device(PCI_VENDOR_ID_TIGERJET,
			PCI_DEVICE_ID_TIGERJET_300,  dev_netjet))) {
			ret = njs_pci_probe(dev_netjet, cs);
			if (!ret)
				return(0);
		} else {
			printk(KERN_WARNING "NETjet-S: No PCI card found\n");
			return(0);
		}

		ret = njs_cs_init(card, cs);
		if (!ret)
			return(0);
		if (ret > 0)
			break;
		/* otherwise, ret < 0, continue looping */
	}

	return njs_cs_init_rest(card, cs);
}
