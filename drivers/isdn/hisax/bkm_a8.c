/* $Id: bkm_a8.c,v 1.22.2.4 2004/01/15 14:02:34 keil Exp $
 *
 * low level stuff for Scitel Quadro (4*S0, passive)
 *
 * Author       Roland Klabunde
 * Copyright    by Roland Klabunde   <R.Klabunde@Berkom.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */


#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "ipac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include "bkm_ax.h"

#define	ATTEMPT_PCI_REMAPPING	/* Required for PLX rev 1 */

static const char sct_quadro_revision[] = "$Revision: 1.22.2.4 $";

static const char *sct_quadro_subtypes[] =
{
	"",
	"#1",
	"#2",
	"#3",
	"#4"
};


#define wordout(addr,val) outw(val,addr)
#define wordin(addr) inw(addr)

static inline u_char
readreg(unsigned int ale, unsigned int adr, u_char off)
{
	register u_char ret;
	wordout(ale, off);
	ret = wordin(adr) & 0xFF;
	return (ret);
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	int i;
	wordout(ale, off);
	for (i = 0; i < size; i++)
		data[i] = wordin(adr) & 0xFF;
}


static inline void
writereg(unsigned int ale, unsigned int adr, u_char off, u_char data)
{
	wordout(ale, off);
	wordout(adr, data);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	int i;
	wordout(ale, off);
	for (i = 0; i < size; i++)
		wordout(adr, data[i]);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.ax.base, cs->hw.ax.data_adr, offset | 0x80));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.ax.base, cs->hw.ax.data_adr, offset | 0x80, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.ax.base, cs->hw.ax.data_adr, 0x80, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.ax.base, cs->hw.ax.data_adr, 0x80, data, size);
}


static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.ax.base, cs->hw.ax.data_adr, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.ax.base, cs->hw.ax.data_adr, offset + (hscx ? 0x40 : 0), value);
}

/* Set the specific ipac to active */
static void
set_ipac_active(struct IsdnCardState *cs, u_int active)
{
	/* set irq mask */
	writereg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_MASK,
		active ? 0xc0 : 0xff);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.ax.base, \
	cs->hw.ax.data_adr, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.ax.base, \
	cs->hw.ax.data_adr, reg + (nr ? 0x40 : 0), data)
#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.ax.base, \
	cs->hw.ax.data_adr, (nr ? 0x40 : 0), ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.ax.base, \
	cs->hw.ax.data_adr, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static irqreturn_t
bkm_interrupt_ipac(int intno, void *dev_id)
{
	struct IsdnCardState *cs = dev_id;
	u_char ista, val, icnt = 5;
	u_long flags;

	spin_lock_irqsave(&cs->lock, flags);
	ista = readreg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_ISTA);
	if (!(ista & 0x3f)) { /* not this IPAC */
		spin_unlock_irqrestore(&cs->lock, flags);
		return IRQ_NONE;
	}
      Start_IPAC:
	if (cs->debug & L1_DEB_IPAC)
		debugl1(cs, "IPAC ISTA %02X", ista);
	if (ista & 0x0f) {
		val = readreg(cs->hw.ax.base, cs->hw.ax.data_adr, HSCX_ISTA + 0x40);
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
		val = 0xfe & readreg(cs->hw.ax.base, cs->hw.ax.data_adr, ISAC_ISTA | 0x80);
		if (val) {
			isac_interrupt(cs, val);
		}
	}
	if (ista & 0x10) {
		val = 0x01;
		isac_interrupt(cs, val);
	}
	ista = readreg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_ISTA);
	if ((ista & 0x3f) && icnt) {
		icnt--;
		goto Start_IPAC;
	}
	if (!icnt)
		printk(KERN_WARNING "HiSax: Scitel Quadro (%s) IRQ LOOP\n",
		       sct_quadro_subtypes[cs->subtyp]);
	writereg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_MASK, 0xFF);
	writereg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_MASK, 0xC0);
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

static void
release_io_sct_quadro(struct IsdnCardState *cs)
{
	release_region(cs->hw.ax.base & 0xffffffc0, 128);
	if (cs->subtyp == SCT_1)
		release_region(cs->hw.ax.plx_adr, 64);
}

static void
enable_bkm_int(struct IsdnCardState *cs, unsigned bEnable)
{
	if (cs->typ == ISDN_CTYPE_SCT_QUADRO) {
		if (bEnable)
			wordout(cs->hw.ax.plx_adr + 0x4C, (wordin(cs->hw.ax.plx_adr + 0x4C) | 0x41));
		else
			wordout(cs->hw.ax.plx_adr + 0x4C, (wordin(cs->hw.ax.plx_adr + 0x4C) & ~0x41));
	}
}

static void
reset_bkm(struct IsdnCardState *cs)
{
	if (cs->subtyp == SCT_1) {
		wordout(cs->hw.ax.plx_adr + 0x50, (wordin(cs->hw.ax.plx_adr + 0x50) & ~4));
		mdelay(10);
		/* Remove the soft reset */
		wordout(cs->hw.ax.plx_adr + 0x50, (wordin(cs->hw.ax.plx_adr + 0x50) | 4));
		mdelay(10);
	}
}

static int
BKM_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
		case CARD_RESET:
			spin_lock_irqsave(&cs->lock, flags);
			/* Disable ints */
			set_ipac_active(cs, 0);
			enable_bkm_int(cs, 0);
			reset_bkm(cs);
			spin_unlock_irqrestore(&cs->lock, flags);
			return (0);
		case CARD_RELEASE:
			/* Sanity */
			spin_lock_irqsave(&cs->lock, flags);
			set_ipac_active(cs, 0);
			enable_bkm_int(cs, 0);
			spin_unlock_irqrestore(&cs->lock, flags);
			release_io_sct_quadro(cs);
			return (0);
		case CARD_INIT:
			spin_lock_irqsave(&cs->lock, flags);
			cs->debug |= L1_DEB_IPAC;
			set_ipac_active(cs, 1);
			inithscxisac(cs, 3);
			/* Enable ints */
			enable_bkm_int(cs, 1);
			spin_unlock_irqrestore(&cs->lock, flags);
			return (0);
		case CARD_TEST:
			return (0);
	}
	return (0);
}

static int __devinit
sct_alloc_io(u_int adr, u_int len)
{
	if (!request_region(adr, len, "scitel")) {
		printk(KERN_WARNING
			"HiSax: Scitel port %#x-%#x already in use\n",
			adr, adr + len);
		return (1);
	}
	return(0);
}

static struct pci_dev *dev_a8 __devinitdata = NULL;
static u16  sub_vendor_id __devinitdata = 0;
static u16  sub_sys_id __devinitdata = 0;
static u_char pci_bus __devinitdata = 0;
static u_char pci_device_fn __devinitdata = 0;
static u_char pci_irq __devinitdata = 0;

int __devinit
setup_sct_quadro(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	u_int found = 0;
	u_int pci_ioaddr1, pci_ioaddr2, pci_ioaddr3, pci_ioaddr4, pci_ioaddr5;

	strcpy(tmp, sct_quadro_revision);
	printk(KERN_INFO "HiSax: T-Berkom driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ == ISDN_CTYPE_SCT_QUADRO) {
		cs->subtyp = SCT_1;	/* Preset */
	} else
		return (0);

	/* Identify subtype by para[0] */
	if (card->para[0] >= SCT_1 && card->para[0] <= SCT_4)
		cs->subtyp = card->para[0];
	else {
		printk(KERN_WARNING "HiSax: Scitel Quadro: Invalid "
		       "subcontroller in configuration, default to 1\n");
		return (0);
	}
	if ((cs->subtyp != SCT_1) && ((sub_sys_id != PCI_DEVICE_ID_BERKOM_SCITEL_QUADRO) ||
		(sub_vendor_id != PCI_VENDOR_ID_BERKOM)))
		return (0);
	if (cs->subtyp == SCT_1) {
		while ((dev_a8 = hisax_find_pci_device(PCI_VENDOR_ID_PLX,
			PCI_DEVICE_ID_PLX_9050, dev_a8))) {
			
			sub_vendor_id = dev_a8->subsystem_vendor;
			sub_sys_id = dev_a8->subsystem_device;
			if ((sub_sys_id == PCI_DEVICE_ID_BERKOM_SCITEL_QUADRO) &&
				(sub_vendor_id == PCI_VENDOR_ID_BERKOM)) {
				if (pci_enable_device(dev_a8))
					return(0);
				pci_ioaddr1 = pci_resource_start(dev_a8, 1);
				pci_irq = dev_a8->irq;
				pci_bus = dev_a8->bus->number;
				pci_device_fn = dev_a8->devfn;
				found = 1;
				break;
			}
		}
		if (!found) {
			printk(KERN_WARNING "HiSax: Scitel Quadro (%s): "
				"Card not found\n",
				sct_quadro_subtypes[cs->subtyp]);
			return (0);
		}
#ifdef ATTEMPT_PCI_REMAPPING
/* HACK: PLX revision 1 bug: PLX address bit 7 must not be set */
		if ((pci_ioaddr1 & 0x80) && (dev_a8->revision == 1)) {
			printk(KERN_WARNING "HiSax: Scitel Quadro (%s): "
				"PLX rev 1, remapping required!\n",
				sct_quadro_subtypes[cs->subtyp]);
			/* Restart PCI negotiation */
			pci_write_config_dword(dev_a8, PCI_BASE_ADDRESS_1, (u_int) - 1);
			/* Move up by 0x80 byte */
			pci_ioaddr1 += 0x80;
			pci_ioaddr1 &= PCI_BASE_ADDRESS_IO_MASK;
			pci_write_config_dword(dev_a8, PCI_BASE_ADDRESS_1, pci_ioaddr1);
			dev_a8->resource[ 1].start = pci_ioaddr1;
		}
#endif /* End HACK */
	}
	if (!pci_irq) {		/* IRQ range check ?? */
		printk(KERN_WARNING "HiSax: Scitel Quadro (%s): No IRQ\n",
		       sct_quadro_subtypes[cs->subtyp]);
		return (0);
	}
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_1, &pci_ioaddr1);
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_2, &pci_ioaddr2);
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_3, &pci_ioaddr3);
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_4, &pci_ioaddr4);
	pci_read_config_dword(dev_a8, PCI_BASE_ADDRESS_5, &pci_ioaddr5);
	if (!pci_ioaddr1 || !pci_ioaddr2 || !pci_ioaddr3 || !pci_ioaddr4 || !pci_ioaddr5) {
		printk(KERN_WARNING "HiSax: Scitel Quadro (%s): "
		       "No IO base address(es)\n",
		       sct_quadro_subtypes[cs->subtyp]);
		return (0);
	}
	pci_ioaddr1 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr2 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr3 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr4 &= PCI_BASE_ADDRESS_IO_MASK;
	pci_ioaddr5 &= PCI_BASE_ADDRESS_IO_MASK;
	/* Take over */
	cs->irq = pci_irq;
	cs->irq_flags |= IRQF_SHARED;
	/* pci_ioaddr1 is unique to all subdevices */
	/* pci_ioaddr2 is for the fourth subdevice only */
	/* pci_ioaddr3 is for the third subdevice only */
	/* pci_ioaddr4 is for the second subdevice only */
	/* pci_ioaddr5 is for the first subdevice only */
	cs->hw.ax.plx_adr = pci_ioaddr1;
	/* Enter all ipac_base addresses */
	switch(cs->subtyp) {
		case 1:
			cs->hw.ax.base = pci_ioaddr5 + 0x00;
			if (sct_alloc_io(pci_ioaddr1, 128))
				return(0);
			if (sct_alloc_io(pci_ioaddr5, 64))
				return(0);
			/* disable all IPAC */
			writereg(pci_ioaddr5, pci_ioaddr5 + 4,
				IPAC_MASK, 0xFF);
			writereg(pci_ioaddr4 + 0x08, pci_ioaddr4 + 0x0c,
				IPAC_MASK, 0xFF);
			writereg(pci_ioaddr3 + 0x10, pci_ioaddr3 + 0x14,
				IPAC_MASK, 0xFF);
			writereg(pci_ioaddr2 + 0x20, pci_ioaddr2 + 0x24,
				IPAC_MASK, 0xFF);
			break;
		case 2:
			cs->hw.ax.base = pci_ioaddr4 + 0x08;
			if (sct_alloc_io(pci_ioaddr4, 64))
				return(0);
			break;
		case 3:
			cs->hw.ax.base = pci_ioaddr3 + 0x10;
			if (sct_alloc_io(pci_ioaddr3, 64))
				return(0);
			break;
		case 4:
			cs->hw.ax.base = pci_ioaddr2 + 0x20;
			if (sct_alloc_io(pci_ioaddr2, 64))
				return(0);
			break;
	}	
	/* For isac and hscx data path */
	cs->hw.ax.data_adr = cs->hw.ax.base + 4;

	printk(KERN_INFO "HiSax: Scitel Quadro (%s) configured at "
	       "0x%.4lX, 0x%.4lX, 0x%.4lX and IRQ %d\n",
	       sct_quadro_subtypes[cs->subtyp],
	       cs->hw.ax.plx_adr,
	       cs->hw.ax.base,
	       cs->hw.ax.data_adr,
	       cs->irq);

	test_and_set_bit(HW_IPAC, &cs->HW_Flags);

	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;

	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &BKM_card_msg;
	cs->irq_func = &bkm_interrupt_ipac;

	printk(KERN_INFO "HiSax: Scitel Quadro (%s): IPAC Version %d\n",
		sct_quadro_subtypes[cs->subtyp],
		readreg(cs->hw.ax.base, cs->hw.ax.data_adr, IPAC_ID));
	return (1);
}
