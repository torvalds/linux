/* enternow_pci.c,v 0.99 2001/10/02
 *
 * enternow_pci.c       Card-specific routines for
 *                      Formula-n enter:now ISDN PCI ab
 *                      Gerdes AG Power ISDN PCI
 *                      Woerltronic SA 16 PCI
 *                      (based on HiSax driver by Karsten Keil)
 *
 * Author               Christoph Ersfeld <info@formula-n.de>
 *                      Formula-n Europe AG (www.formula-n.com)
 *                      previously Gerdes AG
 *
 *
 *                      This file is (c) under GNU PUBLIC LICENSE
 *
 * Notes:
 * This driver interfaces to netjet.c which performs B-channel
 * processing.
 *
 * Version 0.99 is the first release of this driver and there are
 * certainly a few bugs.
 * It isn't testet on linux 2.4 yet, so consider this code to be
 * beta.
 *
 * Please don't report me any malfunction without sending
 * (compressed) debug-logs.
 * It would be nearly impossible to retrace it.
 *
 * Log D-channel-processing as follows:
 *
 * 1. Load hisax with card-specific parameters, this example ist for
 *    Formula-n enter:now ISDN PCI and compatible
 *    (f.e. Gerdes Power ISDN PCI)
 *
 *    modprobe hisax type=41 protocol=2 id=gerdes
 *
 *    if you chose an other value for id, you need to modify the
 *    code below, too.
 *
 * 2. set debug-level
 *
 *    hisaxctrl gerdes 1 0x3ff
 *    hisaxctrl gerdes 11 0x4f
 *    cat /dev/isdnctrl >> ~/log &
 *
 * Please take also a look into /var/log/messages if there is
 * anything importand concerning HISAX.
 *
 *
 * Credits:
 * Programming the driver for Formula-n enter:now ISDN PCI and
 * necessary the driver for the used Amd 7930 D-channel-controller
 * was spnsored by Formula-n Europe AG.
 * Thanks to Karsten Keil and Petr Novak, who gave me support in
 * Hisax-specific questions.
 * I want so say special thanks to Carl-Friedrich Braun, who had to
 * answer a lot of questions about generally ISDN and about handling
 * of the Amd-Chip.
 *
 */


#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "isdnl1.h"
#include "amd7930_fn.h"
#include "enternow.h"
#include <linux/interrupt.h>
#include <linux/ppp_defs.h>
#include <linux/pci.h>
#include <linux/init.h>
#include "netjet.h"



const char *enternow_pci_rev = "$Revision: 1.1.4.5 $";


/* *************************** I/O-Interface functions ************************************* */


/* cs->readisac, macro rByteAMD */
BYTE
ReadByteAmd7930(struct IsdnCardState *cs, BYTE offset)
{
	/* direktes Register */
	if(offset < 8)
		return (InByte(cs->hw.njet.isac + 4*offset));

	/* indirektes Register */
	else {
		OutByte(cs->hw.njet.isac + 4*AMD_CR, offset);
		return(InByte(cs->hw.njet.isac + 4*AMD_DR));
	}
}

/* cs->writeisac, macro wByteAMD */
void
WriteByteAmd7930(struct IsdnCardState *cs, BYTE offset, BYTE value)
{
	/* direktes Register */
	if(offset < 8)
		OutByte(cs->hw.njet.isac + 4*offset, value);

	/* indirektes Register */
	else {
		OutByte(cs->hw.njet.isac + 4*AMD_CR, offset);
		OutByte(cs->hw.njet.isac + 4*AMD_DR, value);
	}
}


void
enpci_setIrqMask(struct IsdnCardState *cs, BYTE val) {
        if (!val)
	        OutByte(cs->hw.njet.base+NETJET_IRQMASK1, 0x00);
        else
	        OutByte(cs->hw.njet.base+NETJET_IRQMASK1, TJ_AMD_IRQ);
}


static BYTE dummyrr(struct IsdnCardState *cs, int chan, BYTE off)
{
        return(5);
}

static void dummywr(struct IsdnCardState *cs, int chan, BYTE off, BYTE value)
{

}


/* ******************************************************************************** */


static void
reset_enpci(struct IsdnCardState *cs)
{
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "enter:now PCI: reset");

	/* Reset on, (also for AMD) */
	cs->hw.njet.ctrl_reg = 0x07;
	OutByte(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	mdelay(20);
	/* Reset off */
	cs->hw.njet.ctrl_reg = 0x30;
	OutByte(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	/* 20ms delay */
	mdelay(20);
	cs->hw.njet.auxd = 0;  // LED-status
	cs->hw.njet.dmactrl = 0;
	OutByte(cs->hw.njet.base + NETJET_AUXCTRL, ~TJ_AMD_IRQ);
	OutByte(cs->hw.njet.base + NETJET_IRQMASK1, TJ_AMD_IRQ);
	OutByte(cs->hw.njet.auxa, cs->hw.njet.auxd); // LED off
}


static int
enpci_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;
        BYTE *chan;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "enter:now PCI: card_msg: 0x%04X", mt);

        switch (mt) {
		case CARD_RESET:
			spin_lock_irqsave(&cs->lock, flags);
			reset_enpci(cs);
                        Amd7930_init(cs);
                        spin_unlock_irqrestore(&cs->lock, flags);
			break;
		case CARD_RELEASE:
			release_io_netjet(cs);
			break;
		case CARD_INIT:
			reset_enpci(cs);
			inittiger(cs);
			/* irq must be on here */
			Amd7930_init(cs);
			break;
		case CARD_TEST:
			break;
                case MDL_ASSIGN:
                        /* TEI assigned, LED1 on */
                        cs->hw.njet.auxd = TJ_AMD_IRQ << 1;
                        OutByte(cs->hw.njet.base + NETJET_AUXDATA, cs->hw.njet.auxd);
                        break;
                case MDL_REMOVE:
                        /* TEI removed, LEDs off */
	                cs->hw.njet.auxd = 0;
                        OutByte(cs->hw.njet.base + NETJET_AUXDATA, 0x00);
                        break;
                case MDL_BC_ASSIGN:
                        /* activate B-channel */
                        chan = (BYTE *)arg;

                        if (cs->debug & L1_DEB_ISAC)
		                debugl1(cs, "enter:now PCI: assign phys. BC %d in AMD LMR1", *chan);

                        cs->dc.amd7930.ph_command(cs, (cs->dc.amd7930.lmr1 | (*chan + 1)), "MDL_BC_ASSIGN");
                        /* at least one b-channel in use, LED 2 on */
                        cs->hw.njet.auxd |= TJ_AMD_IRQ << 2;
                        OutByte(cs->hw.njet.base + NETJET_AUXDATA, cs->hw.njet.auxd);
                        break;
                case MDL_BC_RELEASE:
                        /* deactivate B-channel */
                        chan = (BYTE *)arg;

                        if (cs->debug & L1_DEB_ISAC)
		                debugl1(cs, "enter:now PCI: release phys. BC %d in Amd LMR1", *chan);

                        cs->dc.amd7930.ph_command(cs, (cs->dc.amd7930.lmr1 & ~(*chan + 1)), "MDL_BC_RELEASE");
                        /* no b-channel active -> LED2 off */
                        if (!(cs->dc.amd7930.lmr1 & 3)) {
                                cs->hw.njet.auxd &= ~(TJ_AMD_IRQ << 2);
                                OutByte(cs->hw.njet.base + NETJET_AUXDATA, cs->hw.njet.auxd);
                        }
                        break;
                default:
                        break;

	}
	return(0);
}

static irqreturn_t
enpci_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	BYTE s0val, s1val, ir;
	u_long flags;

	spin_lock_irqsave(&cs->lock, flags);
	s1val = InByte(cs->hw.njet.base + NETJET_IRQSTAT1);

        /* AMD threw an interrupt */
	if (!(s1val & TJ_AMD_IRQ)) {
                /* read and clear interrupt-register */
		ir = ReadByteAmd7930(cs, 0x00);
		Amd7930_interrupt(cs, ir);
		s1val = 1;
	} else
		s1val = 0;
	s0val = InByte(cs->hw.njet.base + NETJET_IRQSTAT0);
	if ((s0val | s1val)==0) { // shared IRQ
		spin_unlock_irqrestore(&cs->lock, flags);
		return IRQ_NONE;
	} 
	if (s0val)
		OutByte(cs->hw.njet.base + NETJET_IRQSTAT0, s0val);

	/* DMA-Interrupt: B-channel-stuff */
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
		s0val = s0val | 0x02;
	else	/* the 1st read page is free */
		s0val = s0val | 0x01;
	if (s0val != cs->hw.njet.last_is0) /* we have a DMA interrupt */
	{
		if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
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
		test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}


static struct pci_dev *dev_netjet __initdata = NULL;

/* called by config.c */
int __init
setup_enternow_pci(struct IsdnCard *card)
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

#ifdef CONFIG_PCI
#ifdef __BIG_ENDIAN
#error "not running on big endian machines now"
#endif
        strcpy(tmp, enternow_pci_rev);
	printk(KERN_INFO "HiSax: Formula-n Europe AG enter:now ISDN PCI driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_ENTERNOW)
		return(0);
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);

	for ( ;; )
	{
		if ((dev_netjet = pci_find_device(PCI_VENDOR_ID_TIGERJET,
			PCI_DEVICE_ID_TIGERJET_300,  dev_netjet))) {
			if (pci_enable_device(dev_netjet))
				return(0);
			cs->irq = dev_netjet->irq;
			if (!cs->irq) {
				printk(KERN_WARNING "enter:now PCI: No IRQ for PCI card found\n");
				return(0);
			}
			cs->hw.njet.base = pci_resource_start(dev_netjet, 0);
			if (!cs->hw.njet.base) {
				printk(KERN_WARNING "enter:now PCI: No IO-Adr for PCI card found\n");
				return(0);
			}
                        /* checks Sub-Vendor ID because system crashes with Traverse-Card */
			if ((dev_netjet->subsystem_vendor != 0x55) ||
				(dev_netjet->subsystem_device != 0x02)) {
				printk(KERN_WARNING "enter:now: You tried to load this driver with an incompatible TigerJet-card\n");
                                printk(KERN_WARNING "Use type=20 for Traverse NetJet PCI Card.\n");
                                return(0);
                        }
		} else {
                        printk(KERN_WARNING "enter:now PCI: No PCI card found\n");
			return(0);
		}

		cs->hw.njet.auxa = cs->hw.njet.base + NETJET_AUXDATA;
		cs->hw.njet.isac = cs->hw.njet.base + 0xC0; // Fenster zum AMD

		/* Reset an */
		cs->hw.njet.ctrl_reg = 0x07;  // geändert von 0xff
		OutByte(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
		/* 20 ms Pause */
		mdelay(20);

		cs->hw.njet.ctrl_reg = 0x30;  /* Reset Off and status read clear */
		OutByte(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
		mdelay(10);

		cs->hw.njet.auxd = 0x00; // war 0xc0
		cs->hw.njet.dmactrl = 0;

		OutByte(cs->hw.njet.base + NETJET_AUXCTRL, ~TJ_AMD_IRQ);
		OutByte(cs->hw.njet.base + NETJET_IRQMASK1, TJ_AMD_IRQ);
		OutByte(cs->hw.njet.auxa, cs->hw.njet.auxd);

		break;
	}
#else

	printk(KERN_WARNING "enter:now PCI: NO_PCI_BIOS\n");
	printk(KERN_WARNING "enter:now PCI: unable to config Formula-n enter:now ISDN PCI ab\n");
	return (0);

#endif /* CONFIG_PCI */

	bytecnt = 256;

	printk(KERN_INFO
		"enter:now PCI: PCI card configured at 0x%lx IRQ %d\n",
		cs->hw.njet.base, cs->irq);
	if (!request_region(cs->hw.njet.base, bytecnt, "Fn_ISDN")) {
		printk(KERN_WARNING
			   "HiSax: %s config port %lx-%lx already in use\n",
			   CardType[card->typ],
			   cs->hw.njet.base,
			   cs->hw.njet.base + bytecnt);
		return (0);
	}
	setup_Amd7930(cs);
	cs->hw.njet.last_is0 = 0;
        /* macro rByteAMD */
        cs->readisac = &ReadByteAmd7930;
        /* macro wByteAMD */
        cs->writeisac = &WriteByteAmd7930;
        cs->dc.amd7930.setIrqMask = &enpci_setIrqMask;

        cs->BC_Read_Reg  = &dummyrr;
	cs->BC_Write_Reg = &dummywr;
	cs->BC_Send_Data = &netjet_fill_dma;
	cs->cardmsg = &enpci_card_msg;
	cs->irq_func = &enpci_interrupt;
	cs->irq_flags |= SA_SHIRQ;

        return (1);
}
