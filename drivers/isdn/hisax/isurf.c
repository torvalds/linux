/* $Id: isurf.c,v 1.12.2.4 2004/01/13 21:46:03 keil Exp $
 *
 * low level stuff for Siemens I-Surf/I-Talk cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "isar.h"
#include "isdnl1.h"
#include <linux/isapnp.h>

static const char *ISurf_revision = "$Revision: 1.12.2.4 $";

#define byteout(addr, val) outb(val, addr)
#define bytein(addr) inb(addr)

#define ISURF_ISAR_RESET	1
#define ISURF_ISAC_RESET	2
#define ISURF_ISAR_EA		4
#define ISURF_ARCOFI_RESET	8
#define ISURF_RESET (ISURF_ISAR_RESET | ISURF_ISAC_RESET | ISURF_ARCOFI_RESET)

#define ISURF_ISAR_OFFSET	0
#define ISURF_ISAC_OFFSET	0x100
#define ISURF_IOMEM_SIZE	0x400
/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readb(cs->hw.isurf.isac + offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writeb(value, cs->hw.isurf.isac + offset); mb();
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	register int i;
	for (i = 0; i < size; i++)
		data[i] = readb(cs->hw.isurf.isac);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	register int i;
	for (i = 0; i < size; i++) {
		writeb(data[i], cs->hw.isurf.isac); mb();
	}
}

/* ISAR access routines
 * mode = 0 access with IRQ on
 * mode = 1 access with IRQ off
 * mode = 2 access with IRQ off and using last offset
 */

static u_char
ReadISAR(struct IsdnCardState *cs, int mode, u_char offset)
{
	return (readb(cs->hw.isurf.isar + offset));
}

static void
WriteISAR(struct IsdnCardState *cs, int mode, u_char offset, u_char value)
{
	writeb(value, cs->hw.isurf.isar + offset); mb();
}

static irqreturn_t
isurf_interrupt(int intno, void *dev_id)
{
	struct IsdnCardState *cs = dev_id;
	u_char val;
	int cnt = 5;
	u_long flags;

	spin_lock_irqsave(&cs->lock, flags);
	val = readb(cs->hw.isurf.isar + ISAR_IRQBIT);
Start_ISAR:
	if (val & ISAR_IRQSTA)
		isar_int_main(cs);
	val = readb(cs->hw.isurf.isac + ISAC_ISTA);
Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	val = readb(cs->hw.isurf.isar + ISAR_IRQBIT);
	if ((val & ISAR_IRQSTA) && --cnt) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "ISAR IntStat after IntRoutine");
		goto Start_ISAR;
	}
	val = readb(cs->hw.isurf.isac + ISAC_ISTA);
	if (val && --cnt) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (!cnt)
		printk(KERN_WARNING "ISurf IRQ LOOP\n");

	writeb(0, cs->hw.isurf.isar + ISAR_IRQBIT); mb();
	writeb(0xFF, cs->hw.isurf.isac + ISAC_MASK); mb();
	writeb(0, cs->hw.isurf.isac + ISAC_MASK); mb();
	writeb(ISAR_IRQMSK, cs->hw.isurf.isar + ISAR_IRQBIT); mb();
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

static void
release_io_isurf(struct IsdnCardState *cs)
{
	release_region(cs->hw.isurf.reset, 1);
	iounmap(cs->hw.isurf.isar);
	release_mem_region(cs->hw.isurf.phymem, ISURF_IOMEM_SIZE);
}

static void
reset_isurf(struct IsdnCardState *cs, u_char chips)
{
	printk(KERN_INFO "ISurf: resetting card\n");

	byteout(cs->hw.isurf.reset, chips); /* Reset On */
	mdelay(10);
	byteout(cs->hw.isurf.reset, ISURF_ISAR_EA); /* Reset Off */
	mdelay(10);
}

static int
ISurf_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
	case CARD_RESET:
		spin_lock_irqsave(&cs->lock, flags);
		reset_isurf(cs, ISURF_RESET);
		spin_unlock_irqrestore(&cs->lock, flags);
		return (0);
	case CARD_RELEASE:
		release_io_isurf(cs);
		return (0);
	case CARD_INIT:
		spin_lock_irqsave(&cs->lock, flags);
		reset_isurf(cs, ISURF_RESET);
		clear_pending_isac_ints(cs);
		writeb(0, cs->hw.isurf.isar + ISAR_IRQBIT); mb();
		initisac(cs);
		initisar(cs);
		/* Reenable ISAC IRQ */
		cs->writeisac(cs, ISAC_MASK, 0);
		/* RESET Receiver and Transmitter */
		cs->writeisac(cs, ISAC_CMDR, 0x41);
		spin_unlock_irqrestore(&cs->lock, flags);
		return (0);
	case CARD_TEST:
		return (0);
	}
	return (0);
}

static int
isurf_auxcmd(struct IsdnCardState *cs, isdn_ctrl *ic) {
	int ret;
	u_long flags;

	if ((ic->command == ISDN_CMD_IOCTL) && (ic->arg == 9)) {
		ret = isar_auxcmd(cs, ic);
		spin_lock_irqsave(&cs->lock, flags);
		if (!ret) {
			reset_isurf(cs, ISURF_ISAR_EA | ISURF_ISAC_RESET |
				    ISURF_ARCOFI_RESET);
			initisac(cs);
			cs->writeisac(cs, ISAC_MASK, 0);
			cs->writeisac(cs, ISAC_CMDR, 0x41);
		}
		spin_unlock_irqrestore(&cs->lock, flags);
		return (ret);
	}
	return (isar_auxcmd(cs, ic));
}

#ifdef __ISAPNP__
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif

int __devinit
setup_isurf(struct IsdnCard *card)
{
	int ver;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, ISurf_revision);
	printk(KERN_INFO "HiSax: ISurf driver Rev. %s\n", HiSax_getrev(tmp));

	if (cs->typ != ISDN_CTYPE_ISURF)
		return (0);
	if (card->para[1] && card->para[2]) {
		cs->hw.isurf.reset = card->para[1];
		cs->hw.isurf.phymem = card->para[2];
		cs->irq = card->para[0];
	} else {
#ifdef __ISAPNP__
		if (isapnp_present()) {
			struct pnp_dev *pnp_d = NULL;
			int err;

			cs->subtyp = 0;
			if ((pnp_c = pnp_find_card(
				     ISAPNP_VENDOR('S', 'I', 'E'),
				     ISAPNP_FUNCTION(0x0010), pnp_c))) {
				if (!(pnp_d = pnp_find_dev(pnp_c,
							   ISAPNP_VENDOR('S', 'I', 'E'),
							   ISAPNP_FUNCTION(0x0010), pnp_d))) {
					printk(KERN_ERR "ISurfPnP: PnP error card found, no device\n");
					return (0);
				}
				pnp_disable_dev(pnp_d);
				err = pnp_activate_dev(pnp_d);
				cs->hw.isurf.reset = pnp_port_start(pnp_d, 0);
				cs->hw.isurf.phymem = pnp_mem_start(pnp_d, 1);
				cs->irq = pnp_irq(pnp_d, 0);
				if (!cs->irq || !cs->hw.isurf.reset || !cs->hw.isurf.phymem) {
					printk(KERN_ERR "ISurfPnP:some resources are missing %d/%x/%lx\n",
					       cs->irq, cs->hw.isurf.reset, cs->hw.isurf.phymem);
					pnp_disable_dev(pnp_d);
					return (0);
				}
			} else {
				printk(KERN_INFO "ISurfPnP: no ISAPnP card found\n");
				return (0);
			}
		} else {
			printk(KERN_INFO "ISurfPnP: no ISAPnP bus found\n");
			return (0);
		}
#else
		printk(KERN_WARNING "HiSax: Siemens I-Surf port/mem not set\n");
		return (0);
#endif
	}
	if (!request_region(cs->hw.isurf.reset, 1, "isurf isdn")) {
		printk(KERN_WARNING
		       "HiSax: Siemens I-Surf config port %x already in use\n",
		       cs->hw.isurf.reset);
		return (0);
	}
	if (!request_region(cs->hw.isurf.phymem, ISURF_IOMEM_SIZE, "isurf iomem")) {
		printk(KERN_WARNING "HiSax: Siemens I-Surf memory region "
		       "%lx-%lx already in use\n",
		       cs->hw.isurf.phymem,
		       cs->hw.isurf.phymem + ISURF_IOMEM_SIZE);
		release_region(cs->hw.isurf.reset, 1);
		return (0);
	}
	cs->hw.isurf.isar = ioremap(cs->hw.isurf.phymem, ISURF_IOMEM_SIZE);
	cs->hw.isurf.isac = cs->hw.isurf.isar + ISURF_ISAC_OFFSET;
	printk(KERN_INFO
	       "ISurf: defined at 0x%x 0x%lx IRQ %d\n",
	       cs->hw.isurf.reset,
	       cs->hw.isurf.phymem,
	       cs->irq);

	setup_isac(cs);
	cs->cardmsg = &ISurf_card_msg;
	cs->irq_func = &isurf_interrupt;
	cs->auxcmd = &isurf_auxcmd;
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->bcs[0].hw.isar.reg = &cs->hw.isurf.isar_r;
	cs->bcs[1].hw.isar.reg = &cs->hw.isurf.isar_r;
	test_and_set_bit(HW_ISAR, &cs->HW_Flags);
	ISACVersion(cs, "ISurf:");
	cs->BC_Read_Reg = &ReadISAR;
	cs->BC_Write_Reg = &WriteISAR;
	cs->BC_Send_Data = &isar_fill_fifo;
	ver = ISARVersion(cs, "ISurf:");
	if (ver < 0) {
		printk(KERN_WARNING
		       "ISurf: wrong ISAR version (ret = %d)\n", ver);
		release_io_isurf(cs);
		return (0);
	}
	return (1);
}
