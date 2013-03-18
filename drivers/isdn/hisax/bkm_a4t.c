/* $Id: bkm_a4t.c,v 1.22.2.4 2004/01/14 16:04:48 keil Exp $
 *
 * low level stuff for T-Berkom A4T
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
#include "hscx.h"
#include "jade.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include "bkm_ax.h"

static const char *bkm_a4t_revision = "$Revision: 1.22.2.4 $";


static inline u_char
readreg(unsigned int ale, unsigned long adr, u_char off)
{
	register u_int ret;
	unsigned int *po = (unsigned int *) adr;	/* Postoffice */

	*po = (GCS_2 | PO_WRITE | off);
	__WAITI20__(po);
	*po = (ale | PO_READ);
	__WAITI20__(po);
	ret = *po;
	return ((unsigned char) ret);
}


static inline void
readfifo(unsigned int ale, unsigned long adr, u_char off, u_char *data, int size)
{
	int i;
	for (i = 0; i < size; i++)
		*data++ = readreg(ale, adr, off);
}


static inline void
writereg(unsigned int ale, unsigned long adr, u_char off, u_char data)
{
	unsigned int *po = (unsigned int *) adr;	/* Postoffice */
	*po = (GCS_2 | PO_WRITE | off);
	__WAITI20__(po);
	*po = (ale | PO_WRITE | data);
	__WAITI20__(po);
}


static inline void
writefifo(unsigned int ale, unsigned long adr, u_char off, u_char *data, int size)
{
	int i;

	for (i = 0; i < size; i++)
		writereg(ale, adr, off, *data++);
}


/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	readfifo(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	writefifo(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, 0, data, size);
}

static u_char
ReadJADE(struct IsdnCardState *cs, int jade, u_char offset)
{
	return (readreg(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr, offset + (jade == -1 ? 0 : (jade ? 0xC0 : 0x80))));
}

static void
WriteJADE(struct IsdnCardState *cs, int jade, u_char offset, u_char value)
{
	writereg(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr, offset + (jade == -1 ? 0 : (jade ? 0xC0 : 0x80)), value);
}

/*
 * fast interrupt JADE stuff goes here
 */

#define READJADE(cs, nr, reg) readreg(cs->hw.ax.jade_ale,		\
				      cs->hw.ax.jade_adr, reg + (nr == -1 ? 0 : (nr ? 0xC0 : 0x80)))
#define WRITEJADE(cs, nr, reg, data) writereg(cs->hw.ax.jade_ale,	\
					      cs->hw.ax.jade_adr, reg + (nr == -1 ? 0 : (nr ? 0xC0 : 0x80)), data)

#define READJADEFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.ax.jade_ale,	\
						cs->hw.ax.jade_adr, (nr == -1 ? 0 : (nr ? 0xC0 : 0x80)), ptr, cnt)
#define WRITEJADEFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.ax.jade_ale,	\
						  cs->hw.ax.jade_adr, (nr == -1 ? 0 : (nr ? 0xC0 : 0x80)), ptr, cnt)

#include "jade_irq.c"

static irqreturn_t
bkm_interrupt(int intno, void *dev_id)
{
	struct IsdnCardState *cs = dev_id;
	u_char val = 0;
	u_long flags;
	I20_REGISTER_FILE *pI20_Regs;

	spin_lock_irqsave(&cs->lock, flags);
	pI20_Regs = (I20_REGISTER_FILE *) (cs->hw.ax.base);

	/* ISDN interrupt pending? */
	if (pI20_Regs->i20IntStatus & intISDN) {
		/* Reset the ISDN interrupt     */
		pI20_Regs->i20IntStatus = intISDN;
		/* Disable ISDN interrupt */
		pI20_Regs->i20IntCtrl &= ~intISDN;
		/* Channel A first */
		val = readreg(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr, jade_HDLC_ISR + 0x80);
		if (val) {
			jade_int_main(cs, val, 0);
		}
		/* Channel B  */
		val = readreg(cs->hw.ax.jade_ale, cs->hw.ax.jade_adr, jade_HDLC_ISR + 0xC0);
		if (val) {
			jade_int_main(cs, val, 1);
		}
		/* D-Channel */
		val = readreg(cs->hw.ax.isac_ale, cs->hw.ax.isac_adr, ISAC_ISTA);
		if (val) {
			isac_interrupt(cs, val);
		}
		/* Reenable ISDN interrupt */
		pI20_Regs->i20IntCtrl |= intISDN;
		spin_unlock_irqrestore(&cs->lock, flags);
		return IRQ_HANDLED;
	} else {
		spin_unlock_irqrestore(&cs->lock, flags);
		return IRQ_NONE;
	}
}

static void
release_io_bkm(struct IsdnCardState *cs)
{
	if (cs->hw.ax.base) {
		iounmap((void *) cs->hw.ax.base);
		cs->hw.ax.base = 0;
	}
}

static void
enable_bkm_int(struct IsdnCardState *cs, unsigned bEnable)
{
	if (cs->typ == ISDN_CTYPE_BKM_A4T) {
		I20_REGISTER_FILE *pI20_Regs = (I20_REGISTER_FILE *) (cs->hw.ax.base);
		if (bEnable)
			pI20_Regs->i20IntCtrl |= (intISDN | intPCI);
		else
			/* CAUTION: This disables the video capture driver too */
			pI20_Regs->i20IntCtrl &= ~(intISDN | intPCI);
	}
}

static void
reset_bkm(struct IsdnCardState *cs)
{
	if (cs->typ == ISDN_CTYPE_BKM_A4T) {
		I20_REGISTER_FILE *pI20_Regs = (I20_REGISTER_FILE *) (cs->hw.ax.base);
		/* Issue the I20 soft reset     */
		pI20_Regs->i20SysControl = 0xFF;	/* all in */
		mdelay(10);
		/* Remove the soft reset */
		pI20_Regs->i20SysControl = sysRESET | 0xFF;
		mdelay(10);
		/* Set our configuration */
		pI20_Regs->i20SysControl = sysRESET | sysCFG;
		/* Issue ISDN reset     */
		pI20_Regs->i20GuestControl = guestWAIT_CFG |
			g_A4T_JADE_RES |
			g_A4T_ISAR_RES |
			g_A4T_ISAC_RES |
			g_A4T_JADE_BOOTR |
			g_A4T_ISAR_BOOTR;
		mdelay(10);

		/* Remove RESET state from ISDN */
		pI20_Regs->i20GuestControl &= ~(g_A4T_ISAC_RES |
						g_A4T_JADE_RES |
						g_A4T_ISAR_RES);
		mdelay(10);
	}
}

static int
BKM_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
	case CARD_RESET:
		/* Disable ints */
		spin_lock_irqsave(&cs->lock, flags);
		enable_bkm_int(cs, 0);
		reset_bkm(cs);
		spin_unlock_irqrestore(&cs->lock, flags);
		return (0);
	case CARD_RELEASE:
		/* Sanity */
		spin_lock_irqsave(&cs->lock, flags);
		enable_bkm_int(cs, 0);
		reset_bkm(cs);
		spin_unlock_irqrestore(&cs->lock, flags);
		release_io_bkm(cs);
		return (0);
	case CARD_INIT:
		spin_lock_irqsave(&cs->lock, flags);
		clear_pending_isac_ints(cs);
		clear_pending_jade_ints(cs);
		initisac(cs);
		initjade(cs);
		/* Enable ints */
		enable_bkm_int(cs, 1);
		spin_unlock_irqrestore(&cs->lock, flags);
		return (0);
	case CARD_TEST:
		return (0);
	}
	return (0);
}

static int a4t_pci_probe(struct pci_dev *dev_a4t, struct IsdnCardState *cs,
			 u_int *found, u_int *pci_memaddr)
{
	u16 sub_sys;
	u16 sub_vendor;

	sub_vendor = dev_a4t->subsystem_vendor;
	sub_sys = dev_a4t->subsystem_device;
	if ((sub_sys == PCI_DEVICE_ID_BERKOM_A4T) && (sub_vendor == PCI_VENDOR_ID_BERKOM)) {
		if (pci_enable_device(dev_a4t))
			return (0);	/* end loop & function */
		*found = 1;
		*pci_memaddr = pci_resource_start(dev_a4t, 0);
		cs->irq = dev_a4t->irq;
		return (1);		/* end loop */
	}

	return (-1);			/* continue looping */
}

static int a4t_cs_init(struct IsdnCard *card, struct IsdnCardState *cs,
		       u_int pci_memaddr)
{
	I20_REGISTER_FILE *pI20_Regs;

	if (!cs->irq) {		/* IRQ range check ?? */
		printk(KERN_WARNING "HiSax: Telekom A4T: No IRQ\n");
		return (0);
	}
	cs->hw.ax.base = (long) ioremap(pci_memaddr, 4096);
	/* Check suspecious address */
	pI20_Regs = (I20_REGISTER_FILE *) (cs->hw.ax.base);
	if ((pI20_Regs->i20IntStatus & 0x8EFFFFFF) != 0) {
		printk(KERN_WARNING "HiSax: Telekom A4T address "
		       "%lx-%lx suspicious\n",
		       cs->hw.ax.base, cs->hw.ax.base + 4096);
		iounmap((void *) cs->hw.ax.base);
		cs->hw.ax.base = 0;
		return (0);
	}
	cs->hw.ax.isac_adr = cs->hw.ax.base + PO_OFFSET;
	cs->hw.ax.jade_adr = cs->hw.ax.base + PO_OFFSET;
	cs->hw.ax.isac_ale = GCS_1;
	cs->hw.ax.jade_ale = GCS_3;

	printk(KERN_INFO "HiSax: Telekom A4T: Card configured at "
	       "0x%lX IRQ %d\n",
	       cs->hw.ax.base, cs->irq);

	setup_isac(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadJADE;
	cs->BC_Write_Reg = &WriteJADE;
	cs->BC_Send_Data = &jade_fill_fifo;
	cs->cardmsg = &BKM_card_msg;
	cs->irq_func = &bkm_interrupt;
	cs->irq_flags |= IRQF_SHARED;
	ISACVersion(cs, "Telekom A4T:");
	/* Jade version */
	JadeVersion(cs, "Telekom A4T:");

	return (1);
}

static struct pci_dev *dev_a4t = NULL;

int setup_bkm_a4t(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	u_int pci_memaddr = 0, found = 0;
	int ret;

	strcpy(tmp, bkm_a4t_revision);
	printk(KERN_INFO "HiSax: T-Berkom driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ == ISDN_CTYPE_BKM_A4T) {
		cs->subtyp = BKM_A4T;
	} else
		return (0);

	while ((dev_a4t = hisax_find_pci_device(PCI_VENDOR_ID_ZORAN,
						PCI_DEVICE_ID_ZORAN_36120, dev_a4t))) {
		ret = a4t_pci_probe(dev_a4t, cs, &found, &pci_memaddr);
		if (!ret)
			return (0);
		if (ret > 0)
			break;
	}
	if (!found) {
		printk(KERN_WARNING "HiSax: Telekom A4T: Card not found\n");
		return (0);
	}
	if (!pci_memaddr) {
		printk(KERN_WARNING "HiSax: Telekom A4T: "
		       "No Memory base address\n");
		return (0);
	}

	return a4t_cs_init(card, cs, pci_memaddr);
}
