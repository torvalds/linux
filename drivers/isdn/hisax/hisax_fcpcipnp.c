/*
 * Driver for AVM Fritz!PCI, Fritz!PCI v2, Fritz!PnP ISDN cards
 *
 * Author       Kai Germaschewski
 * Copyright    2001 by Kai Germaschewski  <kai.germaschewski@gmx.de>
 *              2001 by Karsten Keil       <keil@isdn4linux.de>
 *
 * based upon Karsten Keil's original avm_pci.c driver
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to Wizard Computersysteme GmbH, Bremervoerde and
 *           SoHaNet Technology GmbH, Berlin
 * for supporting the development of this driver
 */


/* TODO:
 *
 * o POWER PC
 * o clean up debugging
 * o tx_skb at PH_DEACTIVATE time
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/isapnp.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/delay.h>

#include <asm/io.h>

#include "hisax_fcpcipnp.h"

// debugging cruft
#define __debug_variable debug
#include "hisax_debug.h"

#ifdef CONFIG_HISAX_DEBUG
static int debug = 0;
/* static int hdlcfifosize = 32; */
module_param(debug, int, 0);
/* module_param(hdlcfifosize, int, 0); */
#endif

MODULE_AUTHOR("Kai Germaschewski <kai.germaschewski@gmx.de>/Karsten Keil <kkeil@suse.de>");
MODULE_DESCRIPTION("AVM Fritz!PCI/PnP ISDN driver");

static const struct pci_device_id fcpci_ids[] = {
	{ .vendor      = PCI_VENDOR_ID_AVM,
	  .device      = PCI_DEVICE_ID_AVM_A1,
	  .subvendor   = PCI_ANY_ID,
	  .subdevice   = PCI_ANY_ID,
	  .driver_data = (unsigned long) "Fritz!Card PCI",
	},
	{ .vendor      = PCI_VENDOR_ID_AVM,
	  .device      = PCI_DEVICE_ID_AVM_A1_V2,
	  .subvendor   = PCI_ANY_ID,
	  .subdevice   = PCI_ANY_ID,
	  .driver_data = (unsigned long) "Fritz!Card PCI v2" },
	{}
};

MODULE_DEVICE_TABLE(pci, fcpci_ids);

#ifdef CONFIG_PNP
static struct pnp_device_id fcpnp_ids[] = {
	{
		.id		= "AVM0900",
		.driver_data	= (unsigned long) "Fritz!Card PnP",
	},
	{ .id = "" }
};

MODULE_DEVICE_TABLE(pnp, fcpnp_ids);
#endif

static int protocol = 2;       /* EURO-ISDN Default */
module_param(protocol, int, 0);
MODULE_LICENSE("GPL");

// ----------------------------------------------------------------------

#define  AVM_INDEX              0x04
#define  AVM_DATA               0x10

#define	 AVM_IDX_HDLC_1		0x00
#define	 AVM_IDX_HDLC_2		0x01
#define	 AVM_IDX_ISAC_FIFO	0x02
#define	 AVM_IDX_ISAC_REG_LOW	0x04
#define	 AVM_IDX_ISAC_REG_HIGH	0x06

#define  AVM_STATUS0            0x02

#define  AVM_STATUS0_IRQ_ISAC	0x01
#define  AVM_STATUS0_IRQ_HDLC	0x02
#define  AVM_STATUS0_IRQ_TIMER	0x04
#define  AVM_STATUS0_IRQ_MASK	0x07

#define  AVM_STATUS0_RESET	0x01
#define  AVM_STATUS0_DIS_TIMER	0x02
#define  AVM_STATUS0_RES_TIMER	0x04
#define  AVM_STATUS0_ENA_IRQ	0x08
#define  AVM_STATUS0_TESTBIT	0x10

#define  AVM_STATUS1            0x03
#define  AVM_STATUS1_ENA_IOM	0x80

#define  HDLC_FIFO		0x0
#define  HDLC_STATUS		0x4
#define  HDLC_CTRL		0x4

#define  HDLC_MODE_ITF_FLG	0x01
#define  HDLC_MODE_TRANS	0x02
#define  HDLC_MODE_CCR_7	0x04
#define  HDLC_MODE_CCR_16	0x08
#define  HDLC_MODE_TESTLOOP	0x80

#define  HDLC_INT_XPR		0x80
#define  HDLC_INT_XDU		0x40
#define  HDLC_INT_RPR		0x20
#define  HDLC_INT_MASK		0xE0

#define  HDLC_STAT_RME		0x01
#define  HDLC_STAT_RDO		0x10
#define  HDLC_STAT_CRCVFRRAB	0x0E
#define  HDLC_STAT_CRCVFR	0x06
#define  HDLC_STAT_RML_MASK	0xff00

#define  HDLC_CMD_XRS		0x80
#define  HDLC_CMD_XME		0x01
#define  HDLC_CMD_RRS		0x20
#define  HDLC_CMD_XML_MASK	0xff00

#define  AVM_HDLC_FIFO_1        0x10
#define  AVM_HDLC_FIFO_2        0x18

#define  AVM_HDLC_STATUS_1      0x14
#define  AVM_HDLC_STATUS_2      0x1c

#define  AVM_ISACSX_INDEX       0x04
#define  AVM_ISACSX_DATA        0x08

// ----------------------------------------------------------------------
// Fritz!PCI

static unsigned char fcpci_read_isac(struct isac *isac, unsigned char offset)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned char idx = (offset > 0x2f) ?
		AVM_IDX_ISAC_REG_HIGH : AVM_IDX_ISAC_REG_LOW;
	unsigned char val;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(idx, adapter->io + AVM_INDEX);
	val = inb(adapter->io + AVM_DATA + (offset & 0xf));
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	DBG(0x1000, " port %#x, value %#x",
	    offset, val);
	return val;
}

static void fcpci_write_isac(struct isac *isac, unsigned char offset,
			     unsigned char value)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned char idx = (offset > 0x2f) ?
		AVM_IDX_ISAC_REG_HIGH : AVM_IDX_ISAC_REG_LOW;
	unsigned long flags;

	DBG(0x1000, " port %#x, value %#x",
	    offset, value);
	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(idx, adapter->io + AVM_INDEX);
	outb(value, adapter->io + AVM_DATA + (offset & 0xf));
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static void fcpci_read_isac_fifo(struct isac *isac, unsigned char *data,
				 int size)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(AVM_IDX_ISAC_FIFO, adapter->io + AVM_INDEX);
	insb(adapter->io + AVM_DATA, data, size);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static void fcpci_write_isac_fifo(struct isac *isac, unsigned char *data,
				  int size)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(AVM_IDX_ISAC_FIFO, adapter->io + AVM_INDEX);
	outsb(adapter->io + AVM_DATA, data, size);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static u32 fcpci_read_hdlc_status(struct fritz_adapter *adapter, int nr)
{
	u32 val;
	int idx = nr ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(idx, adapter->io + AVM_INDEX);
	val = inl(adapter->io + AVM_DATA + HDLC_STATUS);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	return val;
}

static void __fcpci_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	int idx = bcs->channel ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;

	DBG(0x40, "hdlc %c wr%x ctrl %x",
	    'A' + bcs->channel, which, bcs->ctrl.ctrl);

	outl(idx, adapter->io + AVM_INDEX);
	outl(bcs->ctrl.ctrl, adapter->io + AVM_DATA + HDLC_CTRL);
}

static void fcpci_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	__fcpci_write_ctrl(bcs, which);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

// ----------------------------------------------------------------------
// Fritz!PCI v2

static unsigned char fcpci2_read_isac(struct isac *isac, unsigned char offset)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned char val;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(offset, adapter->io + AVM_ISACSX_INDEX);
	val = inl(adapter->io + AVM_ISACSX_DATA);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	DBG(0x1000, " port %#x, value %#x",
	    offset, val);

	return val;
}

static void fcpci2_write_isac(struct isac *isac, unsigned char offset,
			      unsigned char value)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned long flags;

	DBG(0x1000, " port %#x, value %#x",
	    offset, value);
	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(offset, adapter->io + AVM_ISACSX_INDEX);
	outl(value, adapter->io + AVM_ISACSX_DATA);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static void fcpci2_read_isac_fifo(struct isac *isac, unsigned char *data,
				  int size)
{
	struct fritz_adapter *adapter = isac->priv;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(0, adapter->io + AVM_ISACSX_INDEX);
	for (i = 0; i < size; i++)
		data[i] = inl(adapter->io + AVM_ISACSX_DATA);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static void fcpci2_write_isac_fifo(struct isac *isac, unsigned char *data,
				   int size)
{
	struct fritz_adapter *adapter = isac->priv;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(0, adapter->io + AVM_ISACSX_INDEX);
	for (i = 0; i < size; i++)
		outl(data[i], adapter->io + AVM_ISACSX_DATA);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static u32 fcpci2_read_hdlc_status(struct fritz_adapter *adapter, int nr)
{
	int offset = nr ? AVM_HDLC_STATUS_2 : AVM_HDLC_STATUS_1;

	return inl(adapter->io + offset);
}

static void fcpci2_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	int offset = bcs->channel ? AVM_HDLC_STATUS_2 : AVM_HDLC_STATUS_1;

	DBG(0x40, "hdlc %c wr%x ctrl %x",
	    'A' + bcs->channel, which, bcs->ctrl.ctrl);

	outl(bcs->ctrl.ctrl, adapter->io + offset);
}

// ----------------------------------------------------------------------
// Fritz!PnP (ISAC access as for Fritz!PCI)

static u32 fcpnp_read_hdlc_status(struct fritz_adapter *adapter, int nr)
{
	unsigned char idx = nr ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(idx, adapter->io + AVM_INDEX);
	val = inb(adapter->io + AVM_DATA + HDLC_STATUS);
	if (val & HDLC_INT_RPR)
		val |= inb(adapter->io + AVM_DATA + HDLC_STATUS + 1) << 8;
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	return val;
}

static void __fcpnp_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	unsigned char idx = bcs->channel ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;

	DBG(0x40, "hdlc %c wr%x ctrl %x",
	    'A' + bcs->channel, which, bcs->ctrl.ctrl);

	outb(idx, adapter->io + AVM_INDEX);
	if (which & 4)
		outb(bcs->ctrl.sr.mode,
		     adapter->io + AVM_DATA + HDLC_STATUS + 2);
	if (which & 2)
		outb(bcs->ctrl.sr.xml,
		     adapter->io + AVM_DATA + HDLC_STATUS + 1);
	if (which & 1)
		outb(bcs->ctrl.sr.cmd,
		     adapter->io + AVM_DATA + HDLC_STATUS + 0);
}

static void fcpnp_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	__fcpnp_write_ctrl(bcs, which);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

// ----------------------------------------------------------------------

static inline void B_L1L2(struct fritz_bcs *bcs, int pr, void *arg)
{
	struct hisax_if *ifc = (struct hisax_if *) &bcs->b_if;

	DBG(2, "pr %#x", pr);
	ifc->l1l2(ifc, pr, arg);
}

static void hdlc_fill_fifo(struct fritz_bcs *bcs)
{
	struct fritz_adapter *adapter = bcs->adapter;
	struct sk_buff *skb = bcs->tx_skb;
	int count;
	unsigned long flags;
	unsigned char *p;

	DBG(0x40, "hdlc_fill_fifo");

	BUG_ON(skb->len == 0);

	bcs->ctrl.sr.cmd &= ~HDLC_CMD_XME;
	if (bcs->tx_skb->len > bcs->fifo_size) {
		count = bcs->fifo_size;
	} else {
		count = bcs->tx_skb->len;
		if (bcs->mode != L1_MODE_TRANS)
			bcs->ctrl.sr.cmd |= HDLC_CMD_XME;
	}
	DBG(0x40, "hdlc_fill_fifo %d/%d", count, bcs->tx_skb->len);
	p = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt += count;
	bcs->ctrl.sr.xml = ((count == bcs->fifo_size) ? 0 : count);

	switch (adapter->type) {
	case AVM_FRITZ_PCI:
		spin_lock_irqsave(&adapter->hw_lock, flags);
		// sets the correct AVM_INDEX, too
		__fcpci_write_ctrl(bcs, 3);
		outsl(adapter->io + AVM_DATA + HDLC_FIFO,
		      p, (count + 3) / 4);
		spin_unlock_irqrestore(&adapter->hw_lock, flags);
		break;
	case AVM_FRITZ_PCIV2:
		fcpci2_write_ctrl(bcs, 3);
		outsl(adapter->io +
		      (bcs->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1),
		      p, (count + 3) / 4);
		break;
	case AVM_FRITZ_PNP:
		spin_lock_irqsave(&adapter->hw_lock, flags);
		// sets the correct AVM_INDEX, too
		__fcpnp_write_ctrl(bcs, 3);
		outsb(adapter->io + AVM_DATA, p, count);
		spin_unlock_irqrestore(&adapter->hw_lock, flags);
		break;
	}
}

static inline void hdlc_empty_fifo(struct fritz_bcs *bcs, int count)
{
	struct fritz_adapter *adapter = bcs->adapter;
	unsigned char *p;
	unsigned char idx = bcs->channel ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;

	DBG(0x10, "hdlc_empty_fifo %d", count);
	if (bcs->rcvidx + count > HSCX_BUFMAX) {
		DBG(0x10, "hdlc_empty_fifo: incoming packet too large");
		return;
	}
	p = bcs->rcvbuf + bcs->rcvidx;
	bcs->rcvidx += count;
	switch (adapter->type) {
	case AVM_FRITZ_PCI:
		spin_lock(&adapter->hw_lock);
		outl(idx, adapter->io + AVM_INDEX);
		insl(adapter->io + AVM_DATA + HDLC_FIFO,
		     p, (count + 3) / 4);
		spin_unlock(&adapter->hw_lock);
		break;
	case AVM_FRITZ_PCIV2:
		insl(adapter->io +
		     (bcs->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1),
		     p, (count + 3) / 4);
		break;
	case AVM_FRITZ_PNP:
		spin_lock(&adapter->hw_lock);
		outb(idx, adapter->io + AVM_INDEX);
		insb(adapter->io + AVM_DATA, p, count);
		spin_unlock(&adapter->hw_lock);
		break;
	}
}

static inline void hdlc_rpr_irq(struct fritz_bcs *bcs, u32 stat)
{
	struct fritz_adapter *adapter = bcs->adapter;
	struct sk_buff *skb;
	int len;

	if (stat & HDLC_STAT_RDO) {
		DBG(0x10, "RDO");
		bcs->ctrl.sr.xml = 0;
		bcs->ctrl.sr.cmd |= HDLC_CMD_RRS;
		adapter->write_ctrl(bcs, 1);
		bcs->ctrl.sr.cmd &= ~HDLC_CMD_RRS;
		adapter->write_ctrl(bcs, 1);
		bcs->rcvidx = 0;
		return;
	}

	len = (stat & HDLC_STAT_RML_MASK) >> 8;
	if (len == 0)
		len = bcs->fifo_size;

	hdlc_empty_fifo(bcs, len);

	if ((stat & HDLC_STAT_RME) || (bcs->mode == L1_MODE_TRANS)) {
		if (((stat & HDLC_STAT_CRCVFRRAB) == HDLC_STAT_CRCVFR) ||
		    (bcs->mode == L1_MODE_TRANS)) {
			skb = dev_alloc_skb(bcs->rcvidx);
			if (!skb) {
				printk(KERN_WARNING "HDLC: receive out of memory\n");
			} else {
				skb_put_data(skb, bcs->rcvbuf, bcs->rcvidx);
				DBG_SKB(1, skb);
				B_L1L2(bcs, PH_DATA | INDICATION, skb);
			}
			bcs->rcvidx = 0;
		} else {
			DBG(0x10, "ch%d invalid frame %#x",
			    bcs->channel, stat);
			bcs->rcvidx = 0;
		}
	}
}

static inline void hdlc_xdu_irq(struct fritz_bcs *bcs)
{
	struct fritz_adapter *adapter = bcs->adapter;


	/* Here we lost an TX interrupt, so
	 * restart transmitting the whole frame.
	 */
	bcs->ctrl.sr.xml = 0;
	bcs->ctrl.sr.cmd |= HDLC_CMD_XRS;
	adapter->write_ctrl(bcs, 1);
	bcs->ctrl.sr.cmd &= ~HDLC_CMD_XRS;

	if (!bcs->tx_skb) {
		DBG(0x10, "XDU without skb");
		adapter->write_ctrl(bcs, 1);
		return;
	}
	/* only hdlc restarts the frame, transparent mode must continue */
	if (bcs->mode == L1_MODE_HDLC) {
		skb_push(bcs->tx_skb, bcs->tx_cnt);
		bcs->tx_cnt = 0;
	}
}

static inline void hdlc_xpr_irq(struct fritz_bcs *bcs)
{
	struct sk_buff *skb;

	skb = bcs->tx_skb;
	if (!skb)
		return;

	if (skb->len) {
		hdlc_fill_fifo(bcs);
		return;
	}
	bcs->tx_cnt = 0;
	bcs->tx_skb = NULL;
	B_L1L2(bcs, PH_DATA | CONFIRM, (void *)(unsigned long)skb->truesize);
	dev_kfree_skb_irq(skb);
}

static void hdlc_irq_one(struct fritz_bcs *bcs, u32 stat)
{
	DBG(0x10, "ch%d stat %#x", bcs->channel, stat);
	if (stat & HDLC_INT_RPR) {
		DBG(0x10, "RPR");
		hdlc_rpr_irq(bcs, stat);
	}
	if (stat & HDLC_INT_XDU) {
		DBG(0x10, "XDU");
		hdlc_xdu_irq(bcs);
		hdlc_xpr_irq(bcs);
		return;
	}
	if (stat & HDLC_INT_XPR) {
		DBG(0x10, "XPR");
		hdlc_xpr_irq(bcs);
	}
}

static inline void hdlc_irq(struct fritz_adapter *adapter)
{
	int nr;
	u32 stat;

	for (nr = 0; nr < 2; nr++) {
		stat = adapter->read_hdlc_status(adapter, nr);
		DBG(0x10, "HDLC %c stat %#x", 'A' + nr, stat);
		if (stat & HDLC_INT_MASK)
			hdlc_irq_one(&adapter->bcs[nr], stat);
	}
}

static void modehdlc(struct fritz_bcs *bcs, int mode)
{
	struct fritz_adapter *adapter = bcs->adapter;

	DBG(0x40, "hdlc %c mode %d --> %d",
	    'A' + bcs->channel, bcs->mode, mode);

	if (bcs->mode == mode)
		return;

	bcs->fifo_size = 32;
	bcs->ctrl.ctrl = 0;
	bcs->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
	switch (mode) {
	case L1_MODE_NULL:
		bcs->ctrl.sr.mode = HDLC_MODE_TRANS;
		adapter->write_ctrl(bcs, 5);
		break;
	case L1_MODE_TRANS:
	case L1_MODE_HDLC:
		bcs->rcvidx = 0;
		bcs->tx_cnt = 0;
		bcs->tx_skb = NULL;
		if (mode == L1_MODE_TRANS) {
			bcs->ctrl.sr.mode = HDLC_MODE_TRANS;
		} else {
			bcs->ctrl.sr.mode = HDLC_MODE_ITF_FLG;
		}
		adapter->write_ctrl(bcs, 5);
		bcs->ctrl.sr.cmd = HDLC_CMD_XRS;
		adapter->write_ctrl(bcs, 1);
		bcs->ctrl.sr.cmd = 0;
		break;
	}
	bcs->mode = mode;
}

static void fritz_b_l2l1(struct hisax_if *ifc, int pr, void *arg)
{
	struct fritz_bcs *bcs = ifc->priv;
	struct sk_buff *skb = arg;
	int mode;

	DBG(0x10, "pr %#x", pr);

	switch (pr) {
	case PH_DATA | REQUEST:
		BUG_ON(bcs->tx_skb);
		bcs->tx_skb = skb;
		DBG_SKB(1, skb);
		hdlc_fill_fifo(bcs);
		break;
	case PH_ACTIVATE | REQUEST:
		mode = (long) arg;
		DBG(4, "B%d,PH_ACTIVATE_REQUEST %d", bcs->channel + 1, mode);
		modehdlc(bcs, mode);
		B_L1L2(bcs, PH_ACTIVATE | INDICATION, NULL);
		break;
	case PH_DEACTIVATE | REQUEST:
		DBG(4, "B%d,PH_DEACTIVATE_REQUEST", bcs->channel + 1);
		modehdlc(bcs, L1_MODE_NULL);
		B_L1L2(bcs, PH_DEACTIVATE | INDICATION, NULL);
		break;
	}
}

// ----------------------------------------------------------------------

static irqreturn_t
fcpci2_irq(int intno, void *dev)
{
	struct fritz_adapter *adapter = dev;
	unsigned char val;

	val = inb(adapter->io + AVM_STATUS0);
	if (!(val & AVM_STATUS0_IRQ_MASK))
		/* hopefully a shared  IRQ reqest */
		return IRQ_NONE;
	DBG(2, "STATUS0 %#x", val);
	if (val & AVM_STATUS0_IRQ_ISAC)
		isacsx_irq(&adapter->isac);
	if (val & AVM_STATUS0_IRQ_HDLC)
		hdlc_irq(adapter);
	if (val & AVM_STATUS0_IRQ_ISAC)
		isacsx_irq(&adapter->isac);
	return IRQ_HANDLED;
}

static irqreturn_t
fcpci_irq(int intno, void *dev)
{
	struct fritz_adapter *adapter = dev;
	unsigned char sval;

	sval = inb(adapter->io + 2);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK)
		/* possibly a shared  IRQ reqest */
		return IRQ_NONE;
	DBG(2, "sval %#x", sval);
	if (!(sval & AVM_STATUS0_IRQ_ISAC))
		isac_irq(&adapter->isac);

	if (!(sval & AVM_STATUS0_IRQ_HDLC))
		hdlc_irq(adapter);
	return IRQ_HANDLED;
}

// ----------------------------------------------------------------------

static inline void fcpci2_init(struct fritz_adapter *adapter)
{
	outb(AVM_STATUS0_RES_TIMER, adapter->io + AVM_STATUS0);
	outb(AVM_STATUS0_ENA_IRQ, adapter->io + AVM_STATUS0);

}

static inline void fcpci_init(struct fritz_adapter *adapter)
{
	outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER |
	     AVM_STATUS0_ENA_IRQ, adapter->io + AVM_STATUS0);

	outb(AVM_STATUS1_ENA_IOM | adapter->irq,
	     adapter->io + AVM_STATUS1);
	mdelay(10);
}

// ----------------------------------------------------------------------

static int fcpcipnp_setup(struct fritz_adapter *adapter)
{
	u32 val = 0;
	int retval;

	DBG(1, "");

	isac_init(&adapter->isac); // FIXME is this okay now

	retval = -EBUSY;
	if (!request_region(adapter->io, 32, "fcpcipnp"))
		goto err;

	switch (adapter->type) {
	case AVM_FRITZ_PCIV2:
	case AVM_FRITZ_PCI:
		val = inl(adapter->io);
		break;
	case AVM_FRITZ_PNP:
		val = inb(adapter->io);
		val |= inb(adapter->io + 1) << 8;
		break;
	}

	DBG(1, "stat %#x Class %X Rev %d",
	    val, val & 0xff, (val >> 8) & 0xff);

	spin_lock_init(&adapter->hw_lock);
	adapter->isac.priv = adapter;
	switch (adapter->type) {
	case AVM_FRITZ_PCIV2:
		adapter->isac.read_isac       = &fcpci2_read_isac;
		adapter->isac.write_isac      = &fcpci2_write_isac;
		adapter->isac.read_isac_fifo  = &fcpci2_read_isac_fifo;
		adapter->isac.write_isac_fifo = &fcpci2_write_isac_fifo;

		adapter->read_hdlc_status     = &fcpci2_read_hdlc_status;
		adapter->write_ctrl           = &fcpci2_write_ctrl;
		break;
	case AVM_FRITZ_PCI:
		adapter->isac.read_isac       = &fcpci_read_isac;
		adapter->isac.write_isac      = &fcpci_write_isac;
		adapter->isac.read_isac_fifo  = &fcpci_read_isac_fifo;
		adapter->isac.write_isac_fifo = &fcpci_write_isac_fifo;

		adapter->read_hdlc_status     = &fcpci_read_hdlc_status;
		adapter->write_ctrl           = &fcpci_write_ctrl;
		break;
	case AVM_FRITZ_PNP:
		adapter->isac.read_isac       = &fcpci_read_isac;
		adapter->isac.write_isac      = &fcpci_write_isac;
		adapter->isac.read_isac_fifo  = &fcpci_read_isac_fifo;
		adapter->isac.write_isac_fifo = &fcpci_write_isac_fifo;

		adapter->read_hdlc_status     = &fcpnp_read_hdlc_status;
		adapter->write_ctrl           = &fcpnp_write_ctrl;
		break;
	}

	// Reset
	outb(0, adapter->io + AVM_STATUS0);
	mdelay(10);
	outb(AVM_STATUS0_RESET, adapter->io + AVM_STATUS0);
	mdelay(10);
	outb(0, adapter->io + AVM_STATUS0);
	mdelay(10);

	switch (adapter->type) {
	case AVM_FRITZ_PCIV2:
		retval = request_irq(adapter->irq, fcpci2_irq, IRQF_SHARED,
				     "fcpcipnp", adapter);
		break;
	case AVM_FRITZ_PCI:
		retval = request_irq(adapter->irq, fcpci_irq, IRQF_SHARED,
				     "fcpcipnp", adapter);
		break;
	case AVM_FRITZ_PNP:
		retval = request_irq(adapter->irq, fcpci_irq, 0,
				     "fcpcipnp", adapter);
		break;
	}
	if (retval)
		goto err_region;

	switch (adapter->type) {
	case AVM_FRITZ_PCIV2:
		fcpci2_init(adapter);
		isacsx_setup(&adapter->isac);
		break;
	case AVM_FRITZ_PCI:
	case AVM_FRITZ_PNP:
		fcpci_init(adapter);
		isac_setup(&adapter->isac);
		break;
	}
	val = adapter->read_hdlc_status(adapter, 0);
	DBG(0x20, "HDLC A STA %x", val);
	val = adapter->read_hdlc_status(adapter, 1);
	DBG(0x20, "HDLC B STA %x", val);

	adapter->bcs[0].mode = -1;
	adapter->bcs[1].mode = -1;
	modehdlc(&adapter->bcs[0], L1_MODE_NULL);
	modehdlc(&adapter->bcs[1], L1_MODE_NULL);

	return 0;

err_region:
	release_region(adapter->io, 32);
err:
	return retval;
}

static void fcpcipnp_release(struct fritz_adapter *adapter)
{
	DBG(1, "");

	outb(0, adapter->io + AVM_STATUS0);
	free_irq(adapter->irq, adapter);
	release_region(adapter->io, 32);
}

// ----------------------------------------------------------------------

static struct fritz_adapter *new_adapter(void)
{
	struct fritz_adapter *adapter;
	struct hisax_b_if *b_if[2];
	int i;

	adapter = kzalloc(sizeof(struct fritz_adapter), GFP_KERNEL);
	if (!adapter)
		return NULL;

	adapter->isac.hisax_d_if.owner = THIS_MODULE;
	adapter->isac.hisax_d_if.ifc.priv = &adapter->isac;
	adapter->isac.hisax_d_if.ifc.l2l1 = isac_d_l2l1;

	for (i = 0; i < 2; i++) {
		adapter->bcs[i].adapter = adapter;
		adapter->bcs[i].channel = i;
		adapter->bcs[i].b_if.ifc.priv = &adapter->bcs[i];
		adapter->bcs[i].b_if.ifc.l2l1 = fritz_b_l2l1;
	}

	for (i = 0; i < 2; i++)
		b_if[i] = &adapter->bcs[i].b_if;

	if (hisax_register(&adapter->isac.hisax_d_if, b_if, "fcpcipnp",
			   protocol) != 0) {
		kfree(adapter);
		adapter = NULL;
	}

	return adapter;
}

static void delete_adapter(struct fritz_adapter *adapter)
{
	hisax_unregister(&adapter->isac.hisax_d_if);
	kfree(adapter);
}

static int fcpci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct fritz_adapter *adapter;
	int retval;

	retval = -ENOMEM;
	adapter = new_adapter();
	if (!adapter)
		goto err;

	pci_set_drvdata(pdev, adapter);

	if (pdev->device == PCI_DEVICE_ID_AVM_A1_V2)
		adapter->type = AVM_FRITZ_PCIV2;
	else
		adapter->type = AVM_FRITZ_PCI;

	retval = pci_enable_device(pdev);
	if (retval)
		goto err_free;

	adapter->io = pci_resource_start(pdev, 1);
	adapter->irq = pdev->irq;

	printk(KERN_INFO "hisax_fcpcipnp: found adapter %s at %s\n",
	       (char *) ent->driver_data, pci_name(pdev));

	retval = fcpcipnp_setup(adapter);
	if (retval)
		goto err_free;

	return 0;

err_free:
	delete_adapter(adapter);
err:
	return retval;
}

#ifdef CONFIG_PNP
static int fcpnp_probe(struct pnp_dev *pdev, const struct pnp_device_id *dev_id)
{
	struct fritz_adapter *adapter;
	int retval;

	if (!pdev)
		return (-ENODEV);

	retval = -ENOMEM;
	adapter = new_adapter();
	if (!adapter)
		goto err;

	pnp_set_drvdata(pdev, adapter);

	adapter->type = AVM_FRITZ_PNP;

	pnp_disable_dev(pdev);
	retval = pnp_activate_dev(pdev);
	if (retval < 0) {
		printk(KERN_WARNING "%s: pnp_activate_dev(%s) ret(%d)\n", __func__,
		       (char *)dev_id->driver_data, retval);
		goto err_free;
	}
	adapter->io = pnp_port_start(pdev, 0);
	adapter->irq = pnp_irq(pdev, 0);
	if (!adapter->io || adapter->irq == -1)
		goto err_free;

	printk(KERN_INFO "hisax_fcpcipnp: found adapter %s at IO %#x irq %d\n",
	       (char *) dev_id->driver_data, adapter->io, adapter->irq);

	retval = fcpcipnp_setup(adapter);
	if (retval)
		goto err_free;

	return 0;

err_free:
	delete_adapter(adapter);
err:
	return retval;
}

static void fcpnp_remove(struct pnp_dev *pdev)
{
	struct fritz_adapter *adapter = pnp_get_drvdata(pdev);

	if (adapter) {
		fcpcipnp_release(adapter);
		delete_adapter(adapter);
	}
	pnp_disable_dev(pdev);
}

static struct pnp_driver fcpnp_driver = {
	.name		= "fcpnp",
	.probe		= fcpnp_probe,
	.remove		= fcpnp_remove,
	.id_table	= fcpnp_ids,
};
#endif

static void fcpci_remove(struct pci_dev *pdev)
{
	struct fritz_adapter *adapter = pci_get_drvdata(pdev);

	fcpcipnp_release(adapter);
	pci_disable_device(pdev);
	delete_adapter(adapter);
}

static struct pci_driver fcpci_driver = {
	.name		= "fcpci",
	.probe		= fcpci_probe,
	.remove		= fcpci_remove,
	.id_table	= fcpci_ids,
};

static int __init hisax_fcpcipnp_init(void)
{
	int retval;

	printk(KERN_INFO "hisax_fcpcipnp: Fritz!Card PCI/PCIv2/PnP ISDN driver v0.0.1\n");

	retval = pci_register_driver(&fcpci_driver);
	if (retval)
		return retval;
#ifdef CONFIG_PNP
	retval = pnp_register_driver(&fcpnp_driver);
	if (retval < 0) {
		pci_unregister_driver(&fcpci_driver);
		return retval;
	}
#endif
	return 0;
}

static void __exit hisax_fcpcipnp_exit(void)
{
#ifdef CONFIG_PNP
	pnp_unregister_driver(&fcpnp_driver);
#endif
	pci_unregister_driver(&fcpci_driver);
}

module_init(hisax_fcpcipnp_init);
module_exit(hisax_fcpcipnp_exit);
