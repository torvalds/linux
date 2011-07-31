/*
 * avm_fritz.c    low level stuff for AVM FRITZ!CARD PCI ISDN cards
 *                Thanks to AVM, Berlin for informations
 *
 * Author       Karsten Keil <keil@isdn4linux.de>
 *
 * Copyright 2009  by Karsten Keil <keil@isdn4linux.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/mISDNhw.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include "ipac.h"


#define AVMFRITZ_REV	"2.1"

static int AVM_cnt;
static int debug;

enum {
	AVM_FRITZ_PCI,
	AVM_FRITZ_PCIV2,
};

#define HDLC_FIFO		0x0
#define HDLC_STATUS		0x4
#define CHIP_WINDOW		0x10

#define CHIP_INDEX		0x4
#define AVM_HDLC_1		0x00
#define AVM_HDLC_2		0x01
#define AVM_ISAC_FIFO		0x02
#define AVM_ISAC_REG_LOW	0x04
#define AVM_ISAC_REG_HIGH	0x06

#define AVM_STATUS0_IRQ_ISAC	0x01
#define AVM_STATUS0_IRQ_HDLC	0x02
#define AVM_STATUS0_IRQ_TIMER	0x04
#define AVM_STATUS0_IRQ_MASK	0x07

#define AVM_STATUS0_RESET	0x01
#define AVM_STATUS0_DIS_TIMER	0x02
#define AVM_STATUS0_RES_TIMER	0x04
#define AVM_STATUS0_ENA_IRQ	0x08
#define AVM_STATUS0_TESTBIT	0x10

#define AVM_STATUS1_INT_SEL	0x0f
#define AVM_STATUS1_ENA_IOM	0x80

#define HDLC_MODE_ITF_FLG	0x01
#define HDLC_MODE_TRANS		0x02
#define HDLC_MODE_CCR_7		0x04
#define HDLC_MODE_CCR_16	0x08
#define HDLC_MODE_TESTLOOP	0x80

#define HDLC_INT_XPR		0x80
#define HDLC_INT_XDU		0x40
#define HDLC_INT_RPR		0x20
#define HDLC_INT_MASK		0xE0

#define HDLC_STAT_RME		0x01
#define HDLC_STAT_RDO		0x10
#define HDLC_STAT_CRCVFRRAB	0x0E
#define HDLC_STAT_CRCVFR	0x06
#define HDLC_STAT_RML_MASK	0x3f00

#define HDLC_CMD_XRS		0x80
#define HDLC_CMD_XME		0x01
#define HDLC_CMD_RRS		0x20
#define HDLC_CMD_XML_MASK	0x3f00
#define HDLC_FIFO_SIZE		32

/* Fritz PCI v2.0 */

#define AVM_HDLC_FIFO_1		0x10
#define AVM_HDLC_FIFO_2		0x18

#define AVM_HDLC_STATUS_1	0x14
#define AVM_HDLC_STATUS_2	0x1c

#define AVM_ISACX_INDEX		0x04
#define AVM_ISACX_DATA		0x08

/* data struct */
#define LOG_SIZE		63

struct hdlc_stat_reg {
#ifdef __BIG_ENDIAN
	u8 fill;
	u8 mode;
	u8 xml;
	u8 cmd;
#else
	u8 cmd;
	u8 xml;
	u8 mode;
	u8 fill;
#endif
} __attribute__((packed));

struct hdlc_hw {
	union {
		u32 ctrl;
		struct hdlc_stat_reg sr;
	} ctrl;
	u32 stat;
};

struct fritzcard {
	struct list_head	list;
	struct pci_dev		*pdev;
	char			name[MISDN_MAX_IDLEN];
	u8			type;
	u8			ctrlreg;
	u16			irq;
	u32			irqcnt;
	u32			addr;
	spinlock_t		lock; /* hw lock */
	struct isac_hw		isac;
	struct hdlc_hw		hdlc[2];
	struct bchannel		bch[2];
	char			log[LOG_SIZE + 1];
};

static LIST_HEAD(Cards);
static DEFINE_RWLOCK(card_lock); /* protect Cards */

static void
_set_debug(struct fritzcard *card)
{
	card->isac.dch.debug = debug;
	card->bch[0].debug = debug;
	card->bch[1].debug = debug;
}

static int
set_debug(const char *val, struct kernel_param *kp)
{
	int ret;
	struct fritzcard *card;

	ret = param_set_uint(val, kp);
	if (!ret) {
		read_lock(&card_lock);
		list_for_each_entry(card, &Cards, list)
			_set_debug(card);
		read_unlock(&card_lock);
	}
	return ret;
}

MODULE_AUTHOR("Karsten Keil");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(AVMFRITZ_REV);
module_param_call(debug, set_debug, param_get_uint, &debug, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "avmfritz debug mask");

/* Interface functions */

static u8
ReadISAC_V1(void *p, u8 offset)
{
	struct fritzcard *fc = p;
	u8 idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;

	outb(idx, fc->addr + CHIP_INDEX);
	return inb(fc->addr + CHIP_WINDOW + (offset & 0xf));
}

static void
WriteISAC_V1(void *p, u8 offset, u8 value)
{
	struct fritzcard *fc = p;
	u8 idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;

	outb(idx, fc->addr + CHIP_INDEX);
	outb(value, fc->addr + CHIP_WINDOW + (offset & 0xf));
}

static void
ReadFiFoISAC_V1(void *p, u8 off, u8 *data, int size)
{
	struct fritzcard *fc = p;

	outb(AVM_ISAC_FIFO, fc->addr + CHIP_INDEX);
	insb(fc->addr + CHIP_WINDOW, data, size);
}

static void
WriteFiFoISAC_V1(void *p, u8 off, u8 *data, int size)
{
	struct fritzcard *fc = p;

	outb(AVM_ISAC_FIFO, fc->addr + CHIP_INDEX);
	outsb(fc->addr + CHIP_WINDOW, data, size);
}

static u8
ReadISAC_V2(void *p, u8 offset)
{
	struct fritzcard *fc = p;

	outl(offset, fc->addr + AVM_ISACX_INDEX);
	return 0xff & inl(fc->addr + AVM_ISACX_DATA);
}

static void
WriteISAC_V2(void *p, u8 offset, u8 value)
{
	struct fritzcard *fc = p;

	outl(offset, fc->addr + AVM_ISACX_INDEX);
	outl(value, fc->addr + AVM_ISACX_DATA);
}

static void
ReadFiFoISAC_V2(void *p, u8 off, u8 *data, int size)
{
	struct fritzcard *fc = p;
	int i;

	outl(off, fc->addr + AVM_ISACX_INDEX);
	for (i = 0; i < size; i++)
		data[i] = 0xff & inl(fc->addr + AVM_ISACX_DATA);
}

static void
WriteFiFoISAC_V2(void *p, u8 off, u8 *data, int size)
{
	struct fritzcard *fc = p;
	int i;

	outl(off, fc->addr + AVM_ISACX_INDEX);
	for (i = 0; i < size; i++)
		outl(data[i], fc->addr + AVM_ISACX_DATA);
}

static struct bchannel *
Sel_BCS(struct fritzcard *fc, u32 channel)
{
	if (test_bit(FLG_ACTIVE, &fc->bch[0].Flags) &&
		(fc->bch[0].nr & channel))
		return &fc->bch[0];
	else if (test_bit(FLG_ACTIVE, &fc->bch[1].Flags) &&
		(fc->bch[1].nr & channel))
		return &fc->bch[1];
	else
		return NULL;
}

static inline void
__write_ctrl_pci(struct fritzcard *fc, struct hdlc_hw *hdlc, u32 channel) {
	u32 idx = channel == 2 ? AVM_HDLC_2 : AVM_HDLC_1;

	outl(idx, fc->addr + CHIP_INDEX);
	outl(hdlc->ctrl.ctrl, fc->addr + CHIP_WINDOW + HDLC_STATUS);
}

static inline void
__write_ctrl_pciv2(struct fritzcard *fc, struct hdlc_hw *hdlc, u32 channel) {
	outl(hdlc->ctrl.ctrl, fc->addr + (channel == 2 ? AVM_HDLC_STATUS_2 :
		AVM_HDLC_STATUS_1));
}

void
write_ctrl(struct bchannel *bch, int which) {
	struct fritzcard *fc = bch->hw;
	struct hdlc_hw *hdlc;

	hdlc = &fc->hdlc[(bch->nr - 1) & 1];
	pr_debug("%s: hdlc %c wr%x ctrl %x\n", fc->name, '@' + bch->nr,
		which, hdlc->ctrl.ctrl);
	switch (fc->type) {
	case AVM_FRITZ_PCIV2:
		__write_ctrl_pciv2(fc, hdlc, bch->nr);
		break;
	case AVM_FRITZ_PCI:
		__write_ctrl_pci(fc, hdlc, bch->nr);
		break;
	}
}


static inline u32
__read_status_pci(u_long addr, u32 channel)
{
	outl(channel == 2 ? AVM_HDLC_2 : AVM_HDLC_1, addr + CHIP_INDEX);
	return inl(addr + CHIP_WINDOW + HDLC_STATUS);
}

static inline u32
__read_status_pciv2(u_long addr, u32 channel)
{
	return inl(addr + (channel == 2 ? AVM_HDLC_STATUS_2 :
		AVM_HDLC_STATUS_1));
}


static u32
read_status(struct fritzcard *fc, u32 channel)
{
	switch (fc->type) {
	case AVM_FRITZ_PCIV2:
		return __read_status_pciv2(fc->addr, channel);
	case AVM_FRITZ_PCI:
		return __read_status_pci(fc->addr, channel);
	}
	/* dummy */
	return 0;
}

static void
enable_hwirq(struct fritzcard *fc)
{
	fc->ctrlreg |= AVM_STATUS0_ENA_IRQ;
	outb(fc->ctrlreg, fc->addr + 2);
}

static void
disable_hwirq(struct fritzcard *fc)
{
	fc->ctrlreg &= ~AVM_STATUS0_ENA_IRQ;
	outb(fc->ctrlreg, fc->addr + 2);
}

static int
modehdlc(struct bchannel *bch, int protocol)
{
	struct fritzcard *fc = bch->hw;
	struct hdlc_hw *hdlc;

	hdlc = &fc->hdlc[(bch->nr - 1) & 1];
	pr_debug("%s: hdlc %c protocol %x-->%x ch %d\n", fc->name,
		'@' + bch->nr, bch->state, protocol, bch->nr);
	hdlc->ctrl.ctrl = 0;
	switch (protocol) {
	case -1: /* used for init */
		bch->state = -1;
	case ISDN_P_NONE:
		if (bch->state == ISDN_P_NONE)
			break;
		hdlc->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
		hdlc->ctrl.sr.mode = HDLC_MODE_TRANS;
		write_ctrl(bch, 5);
		bch->state = ISDN_P_NONE;
		test_and_clear_bit(FLG_HDLC, &bch->Flags);
		test_and_clear_bit(FLG_TRANSPARENT, &bch->Flags);
		break;
	case ISDN_P_B_RAW:
		bch->state = protocol;
		hdlc->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
		hdlc->ctrl.sr.mode = HDLC_MODE_TRANS;
		write_ctrl(bch, 5);
		hdlc->ctrl.sr.cmd = HDLC_CMD_XRS;
		write_ctrl(bch, 1);
		hdlc->ctrl.sr.cmd = 0;
		test_and_set_bit(FLG_TRANSPARENT, &bch->Flags);
		break;
	case ISDN_P_B_HDLC:
		bch->state = protocol;
		hdlc->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
		hdlc->ctrl.sr.mode = HDLC_MODE_ITF_FLG;
		write_ctrl(bch, 5);
		hdlc->ctrl.sr.cmd = HDLC_CMD_XRS;
		write_ctrl(bch, 1);
		hdlc->ctrl.sr.cmd = 0;
		test_and_set_bit(FLG_HDLC, &bch->Flags);
		break;
	default:
		pr_info("%s: protocol not known %x\n", fc->name, protocol);
		return -ENOPROTOOPT;
	}
	return 0;
}

static void
hdlc_empty_fifo(struct bchannel *bch, int count)
{
	u32 *ptr;
	u8 *p;
	u32  val, addr;
	int cnt = 0;
	struct fritzcard *fc = bch->hw;

	pr_debug("%s: %s %d\n", fc->name, __func__, count);
	if (!bch->rx_skb) {
		bch->rx_skb = mI_alloc_skb(bch->maxlen, GFP_ATOMIC);
		if (!bch->rx_skb) {
			pr_info("%s: B receive out of memory\n",
				fc->name);
			return;
		}
	}
	if ((bch->rx_skb->len + count) > bch->maxlen) {
		pr_debug("%s: overrun %d\n", fc->name,
			bch->rx_skb->len + count);
		return;
	}
	p = skb_put(bch->rx_skb, count);
	ptr = (u32 *)p;
	if (AVM_FRITZ_PCIV2 == fc->type)
		addr = fc->addr + (bch->nr == 2 ?
			AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1);
	else {
		addr = fc->addr + CHIP_WINDOW;
		outl(bch->nr == 2 ? AVM_HDLC_2 : AVM_HDLC_1, fc->addr);
	}
	while (cnt < count) {
		val = le32_to_cpu(inl(addr));
		put_unaligned(val, ptr);
		ptr++;
		cnt += 4;
	}
	if (debug & DEBUG_HW_BFIFO) {
		snprintf(fc->log, LOG_SIZE, "B%1d-recv %s %d ",
			bch->nr, fc->name, count);
		print_hex_dump_bytes(fc->log, DUMP_PREFIX_OFFSET, p, count);
	}
}

static void
hdlc_fill_fifo(struct bchannel *bch)
{
	struct fritzcard *fc = bch->hw;
	struct hdlc_hw *hdlc;
	int count, cnt = 0;
	u8 *p;
	u32 *ptr, val, addr;

	hdlc = &fc->hdlc[(bch->nr - 1) & 1];
	if (!bch->tx_skb)
		return;
	count = bch->tx_skb->len - bch->tx_idx;
	if (count <= 0)
		return;
	p = bch->tx_skb->data + bch->tx_idx;
	hdlc->ctrl.sr.cmd &= ~HDLC_CMD_XME;
	if (count > HDLC_FIFO_SIZE) {
		count = HDLC_FIFO_SIZE;
	} else {
		if (test_bit(FLG_HDLC, &bch->Flags))
			hdlc->ctrl.sr.cmd |= HDLC_CMD_XME;
	}
	pr_debug("%s: %s %d/%d/%d", fc->name, __func__, count,
		bch->tx_idx, bch->tx_skb->len);
	ptr = (u32 *)p;
	bch->tx_idx += count;
	hdlc->ctrl.sr.xml = ((count == HDLC_FIFO_SIZE) ? 0 : count);
	if (AVM_FRITZ_PCIV2 == fc->type) {
		__write_ctrl_pciv2(fc, hdlc, bch->nr);
		addr = fc->addr + (bch->nr == 2 ?
			AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1);
	} else {
		__write_ctrl_pci(fc, hdlc, bch->nr);
		addr = fc->addr + CHIP_WINDOW;
	}
	while (cnt < count) {
		val = get_unaligned(ptr);
		outl(cpu_to_le32(val), addr);
		ptr++;
		cnt += 4;
	}
	if (debug & DEBUG_HW_BFIFO) {
		snprintf(fc->log, LOG_SIZE, "B%1d-send %s %d ",
			bch->nr, fc->name, count);
		print_hex_dump_bytes(fc->log, DUMP_PREFIX_OFFSET, p, count);
	}
}

static void
HDLC_irq_xpr(struct bchannel *bch)
{
	if (bch->tx_skb && bch->tx_idx < bch->tx_skb->len)
		hdlc_fill_fifo(bch);
	else {
		if (bch->tx_skb) {
			/* send confirm, on trans, free on hdlc. */
			if (test_bit(FLG_TRANSPARENT, &bch->Flags))
				confirm_Bsend(bch);
			dev_kfree_skb(bch->tx_skb);
		}
		if (get_next_bframe(bch))
			hdlc_fill_fifo(bch);
	}
}

static void
HDLC_irq(struct bchannel *bch, u32 stat)
{
	struct fritzcard *fc = bch->hw;
	int		len;
	struct hdlc_hw	*hdlc;

	hdlc = &fc->hdlc[(bch->nr - 1) & 1];
	pr_debug("%s: ch%d stat %#x\n", fc->name, bch->nr, stat);
	if (stat & HDLC_INT_RPR) {
		if (stat & HDLC_STAT_RDO) {
			hdlc->ctrl.sr.xml = 0;
			hdlc->ctrl.sr.cmd |= HDLC_CMD_RRS;
			write_ctrl(bch, 1);
			hdlc->ctrl.sr.cmd &= ~HDLC_CMD_RRS;
			write_ctrl(bch, 1);
			if (bch->rx_skb)
				skb_trim(bch->rx_skb, 0);
		} else {
			len = (stat & HDLC_STAT_RML_MASK) >> 8;
			if (!len)
				len = 32;
			hdlc_empty_fifo(bch, len);
			if (!bch->rx_skb)
				goto handle_tx;
			if ((stat & HDLC_STAT_RME) || test_bit(FLG_TRANSPARENT,
			    &bch->Flags)) {
				if (((stat & HDLC_STAT_CRCVFRRAB) ==
				    HDLC_STAT_CRCVFR) ||
				    test_bit(FLG_TRANSPARENT, &bch->Flags)) {
					recv_Bchannel(bch, 0);
				} else {
					pr_debug("%s: got invalid frame\n",
						fc->name);
					skb_trim(bch->rx_skb, 0);
				}
			}
		}
	}
handle_tx:
	if (stat & HDLC_INT_XDU) {
		/* Here we lost an TX interrupt, so
		 * restart transmitting the whole frame on HDLC
		 * in transparent mode we send the next data
		 */
		if (bch->tx_skb)
			pr_debug("%s: ch%d XDU len(%d) idx(%d) Flags(%lx)\n",
				fc->name, bch->nr, bch->tx_skb->len,
				bch->tx_idx, bch->Flags);
		else
			pr_debug("%s: ch%d XDU no tx_skb Flags(%lx)\n",
				fc->name, bch->nr, bch->Flags);
		if (bch->tx_skb && bch->tx_skb->len) {
			if (!test_bit(FLG_TRANSPARENT, &bch->Flags))
				bch->tx_idx = 0;
		}
		hdlc->ctrl.sr.xml = 0;
		hdlc->ctrl.sr.cmd |= HDLC_CMD_XRS;
		write_ctrl(bch, 1);
		hdlc->ctrl.sr.cmd &= ~HDLC_CMD_XRS;
		HDLC_irq_xpr(bch);
		return;
	} else if (stat & HDLC_INT_XPR)
		HDLC_irq_xpr(bch);
}

static inline void
HDLC_irq_main(struct fritzcard *fc)
{
	u32 stat;
	struct bchannel *bch;

	stat = read_status(fc, 1);
	if (stat & HDLC_INT_MASK) {
		bch = Sel_BCS(fc, 1);
		if (bch)
			HDLC_irq(bch, stat);
		else
			pr_debug("%s: spurious ch1 IRQ\n", fc->name);
	}
	stat = read_status(fc, 2);
	if (stat & HDLC_INT_MASK) {
		bch = Sel_BCS(fc, 2);
		if (bch)
			HDLC_irq(bch, stat);
		else
			pr_debug("%s: spurious ch2 IRQ\n", fc->name);
	}
}

static irqreturn_t
avm_fritz_interrupt(int intno, void *dev_id)
{
	struct fritzcard *fc = dev_id;
	u8 val;
	u8 sval;

	spin_lock(&fc->lock);
	sval = inb(fc->addr + 2);
	pr_debug("%s: irq stat0 %x\n", fc->name, sval);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK) {
		/* shared  IRQ from other HW */
		spin_unlock(&fc->lock);
		return IRQ_NONE;
	}
	fc->irqcnt++;

	if (!(sval & AVM_STATUS0_IRQ_ISAC)) {
		val = ReadISAC_V1(fc, ISAC_ISTA);
		mISDNisac_irq(&fc->isac, val);
	}
	if (!(sval & AVM_STATUS0_IRQ_HDLC))
		HDLC_irq_main(fc);
	spin_unlock(&fc->lock);
	return IRQ_HANDLED;
}

static irqreturn_t
avm_fritzv2_interrupt(int intno, void *dev_id)
{
	struct fritzcard *fc = dev_id;
	u8 val;
	u8 sval;

	spin_lock(&fc->lock);
	sval = inb(fc->addr + 2);
	pr_debug("%s: irq stat0 %x\n", fc->name, sval);
	if (!(sval & AVM_STATUS0_IRQ_MASK)) {
		/* shared  IRQ from other HW */
		spin_unlock(&fc->lock);
		return IRQ_NONE;
	}
	fc->irqcnt++;

	if (sval & AVM_STATUS0_IRQ_HDLC)
		HDLC_irq_main(fc);
	if (sval & AVM_STATUS0_IRQ_ISAC) {
		val = ReadISAC_V2(fc, ISACX_ISTA);
		mISDNisac_irq(&fc->isac, val);
	}
	if (sval & AVM_STATUS0_IRQ_TIMER) {
		pr_debug("%s: timer irq\n", fc->name);
		outb(fc->ctrlreg | AVM_STATUS0_RES_TIMER, fc->addr + 2);
		udelay(1);
		outb(fc->ctrlreg, fc->addr + 2);
	}
	spin_unlock(&fc->lock);
	return IRQ_HANDLED;
}

static int
avm_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	struct fritzcard *fc = bch->hw;
	int ret = -EINVAL;
	struct mISDNhead *hh = mISDN_HEAD_P(skb);
	u32 id;
	u_long flags;

	switch (hh->prim) {
	case PH_DATA_REQ:
		spin_lock_irqsave(&fc->lock, flags);
		ret = bchannel_senddata(bch, skb);
		if (ret > 0) { /* direct TX */
			id = hh->id; /* skb can be freed */
			hdlc_fill_fifo(bch);
			ret = 0;
			spin_unlock_irqrestore(&fc->lock, flags);
			if (!test_bit(FLG_TRANSPARENT, &bch->Flags))
				queue_ch_frame(ch, PH_DATA_CNF, id, NULL);
		} else
			spin_unlock_irqrestore(&fc->lock, flags);
		return ret;
	case PH_ACTIVATE_REQ:
		spin_lock_irqsave(&fc->lock, flags);
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags))
			ret = modehdlc(bch, ch->protocol);
		else
			ret = 0;
		spin_unlock_irqrestore(&fc->lock, flags);
		if (!ret)
			_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY, 0,
				NULL, GFP_KERNEL);
		break;
	case PH_DEACTIVATE_REQ:
		spin_lock_irqsave(&fc->lock, flags);
		mISDN_clear_bchannel(bch);
		modehdlc(bch, ISDN_P_NONE);
		spin_unlock_irqrestore(&fc->lock, flags);
		_queue_data(ch, PH_DEACTIVATE_IND, MISDN_ID_ANY, 0,
			NULL, GFP_KERNEL);
		ret = 0;
		break;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

static void
inithdlc(struct fritzcard *fc)
{
	modehdlc(&fc->bch[0], -1);
	modehdlc(&fc->bch[1], -1);
}

void
clear_pending_hdlc_ints(struct fritzcard *fc)
{
	u32 val;

	val = read_status(fc, 1);
	pr_debug("%s: HDLC 1 STA %x\n", fc->name, val);
	val = read_status(fc, 2);
	pr_debug("%s: HDLC 2 STA %x\n", fc->name, val);
}

static void
reset_avm(struct fritzcard *fc)
{
	switch (fc->type) {
	case AVM_FRITZ_PCI:
		fc->ctrlreg = AVM_STATUS0_RESET | AVM_STATUS0_DIS_TIMER;
		break;
	case AVM_FRITZ_PCIV2:
		fc->ctrlreg = AVM_STATUS0_RESET;
		break;
	}
	if (debug & DEBUG_HW)
		pr_notice("%s: reset\n", fc->name);
	disable_hwirq(fc);
	mdelay(5);
	switch (fc->type) {
	case AVM_FRITZ_PCI:
		fc->ctrlreg = AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER;
		disable_hwirq(fc);
		outb(AVM_STATUS1_ENA_IOM, fc->addr + 3);
		break;
	case AVM_FRITZ_PCIV2:
		fc->ctrlreg = 0;
		disable_hwirq(fc);
		break;
	}
	mdelay(1);
	if (debug & DEBUG_HW)
		pr_notice("%s: S0/S1 %x/%x\n", fc->name,
			inb(fc->addr + 2), inb(fc->addr + 3));
}

static int
init_card(struct fritzcard *fc)
{
	int		ret, cnt = 3;
	u_long		flags;

	reset_avm(fc); /* disable IRQ */
	if (fc->type == AVM_FRITZ_PCIV2)
		ret = request_irq(fc->irq, avm_fritzv2_interrupt,
			IRQF_SHARED, fc->name, fc);
	else
		ret = request_irq(fc->irq, avm_fritz_interrupt,
			IRQF_SHARED, fc->name, fc);
	if (ret) {
		pr_info("%s: couldn't get interrupt %d\n",
			fc->name, fc->irq);
		return ret;
	}
	while (cnt--) {
		spin_lock_irqsave(&fc->lock, flags);
		ret = fc->isac.init(&fc->isac);
		if (ret) {
			spin_unlock_irqrestore(&fc->lock, flags);
			pr_info("%s: ISAC init failed with %d\n",
				fc->name, ret);
			break;
		}
		clear_pending_hdlc_ints(fc);
		inithdlc(fc);
		enable_hwirq(fc);
		/* RESET Receiver and Transmitter */
		if (AVM_FRITZ_PCIV2 == fc->type) {
			WriteISAC_V2(fc, ISACX_MASK, 0);
			WriteISAC_V2(fc, ISACX_CMDRD, 0x41);
		} else {
			WriteISAC_V1(fc, ISAC_MASK, 0);
			WriteISAC_V1(fc, ISAC_CMDR, 0x41);
		}
		spin_unlock_irqrestore(&fc->lock, flags);
		/* Timeout 10ms */
		msleep_interruptible(10);
		if (debug & DEBUG_HW)
			pr_notice("%s: IRQ %d count %d\n", fc->name,
				fc->irq, fc->irqcnt);
		if (!fc->irqcnt) {
			pr_info("%s: IRQ(%d) getting no IRQs during init %d\n",
				fc->name, fc->irq, 3 - cnt);
			reset_avm(fc);
		} else
			return 0;
	}
	free_irq(fc->irq, fc);
	return -EIO;
}

static int
channel_bctrl(struct bchannel *bch, struct mISDN_ctrl_req *cq)
{
	int ret = 0;
	struct fritzcard *fc = bch->hw;

	switch (cq->op) {
	case MISDN_CTRL_GETOP:
		cq->op = 0;
		break;
	/* Nothing implemented yet */
	case MISDN_CTRL_FILL_EMPTY:
	default:
		pr_info("%s: %s unknown Op %x\n", fc->name, __func__, cq->op);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int
avm_bctrl(struct mISDNchannel *ch, u32 cmd, void *arg)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	struct fritzcard *fc = bch->hw;
	int ret = -EINVAL;
	u_long flags;

	pr_debug("%s: %s cmd:%x %p\n", fc->name, __func__, cmd, arg);
	switch (cmd) {
	case CLOSE_CHANNEL:
		test_and_clear_bit(FLG_OPEN, &bch->Flags);
		if (test_bit(FLG_ACTIVE, &bch->Flags)) {
			spin_lock_irqsave(&fc->lock, flags);
			mISDN_freebchannel(bch);
			test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
			test_and_clear_bit(FLG_ACTIVE, &bch->Flags);
			modehdlc(bch, ISDN_P_NONE);
			spin_unlock_irqrestore(&fc->lock, flags);
		}
		ch->protocol = ISDN_P_NONE;
		ch->peer = NULL;
		module_put(THIS_MODULE);
		ret = 0;
		break;
	case CONTROL_CHANNEL:
		ret = channel_bctrl(bch, arg);
		break;
	default:
		pr_info("%s: %s unknown prim(%x)\n", fc->name, __func__, cmd);
	}
	return ret;
}

static int
channel_ctrl(struct fritzcard  *fc, struct mISDN_ctrl_req *cq)
{
	int	ret = 0;

	switch (cq->op) {
	case MISDN_CTRL_GETOP:
		cq->op = MISDN_CTRL_LOOP;
		break;
	case MISDN_CTRL_LOOP:
		/* cq->channel: 0 disable, 1 B1 loop 2 B2 loop, 3 both */
		if (cq->channel < 0 || cq->channel > 3) {
			ret = -EINVAL;
			break;
		}
		ret = fc->isac.ctrl(&fc->isac, HW_TESTLOOP, cq->channel);
		break;
	default:
		pr_info("%s: %s unknown Op %x\n", fc->name, __func__, cq->op);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int
open_bchannel(struct fritzcard *fc, struct channel_req *rq)
{
	struct bchannel		*bch;

	if (rq->adr.channel > 2)
		return -EINVAL;
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;
	bch = &fc->bch[rq->adr.channel - 1];
	if (test_and_set_bit(FLG_OPEN, &bch->Flags))
		return -EBUSY; /* b-channel can be only open once */
	test_and_clear_bit(FLG_FILLEMPTY, &bch->Flags);
	bch->ch.protocol = rq->protocol;
	rq->ch = &bch->ch;
	return 0;
}

/*
 * device control function
 */
static int
avm_dctrl(struct mISDNchannel *ch, u32 cmd, void *arg)
{
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct fritzcard	*fc = dch->hw;
	struct channel_req	*rq;
	int			err = 0;

	pr_debug("%s: %s cmd:%x %p\n", fc->name, __func__, cmd, arg);
	switch (cmd) {
	case OPEN_CHANNEL:
		rq = arg;
		if (rq->protocol == ISDN_P_TE_S0)
			err = fc->isac.open(&fc->isac, rq);
		else
			err = open_bchannel(fc, rq);
		if (err)
			break;
		if (!try_module_get(THIS_MODULE))
			pr_info("%s: cannot get module\n", fc->name);
		break;
	case CLOSE_CHANNEL:
		pr_debug("%s: dev(%d) close from %p\n", fc->name, dch->dev.id,
			__builtin_return_address(0));
		module_put(THIS_MODULE);
		break;
	case CONTROL_CHANNEL:
		err = channel_ctrl(fc, arg);
		break;
	default:
		pr_debug("%s: %s unknown command %x\n",
			fc->name, __func__, cmd);
		return -EINVAL;
	}
	return err;
}

int
setup_fritz(struct fritzcard *fc)
{
	u32 val, ver;

	if (!request_region(fc->addr, 32, fc->name)) {
		pr_info("%s: AVM config port %x-%x already in use\n",
			fc->name, fc->addr, fc->addr + 31);
		return -EIO;
	}
	switch (fc->type) {
	case AVM_FRITZ_PCI:
		val = inl(fc->addr);
		outl(AVM_HDLC_1, fc->addr + CHIP_INDEX);
		ver = inl(fc->addr + CHIP_WINDOW + HDLC_STATUS) >> 24;
		if (debug & DEBUG_HW) {
			pr_notice("%s: PCI stat %#x\n", fc->name, val);
			pr_notice("%s: PCI Class %X Rev %d\n", fc->name,
				val & 0xff, (val >> 8) & 0xff);
			pr_notice("%s: HDLC version %x\n", fc->name, ver & 0xf);
		}
		ASSIGN_FUNC(V1, ISAC, fc->isac);
		fc->isac.type = IPAC_TYPE_ISAC;
		break;
	case AVM_FRITZ_PCIV2:
		val = inl(fc->addr);
		ver = inl(fc->addr + AVM_HDLC_STATUS_1) >> 24;
		if (debug & DEBUG_HW) {
			pr_notice("%s: PCI V2 stat %#x\n", fc->name, val);
			pr_notice("%s: PCI V2 Class %X Rev %d\n", fc->name,
				val & 0xff, (val>>8) & 0xff);
			pr_notice("%s: HDLC version %x\n", fc->name, ver & 0xf);
		}
		ASSIGN_FUNC(V2, ISAC, fc->isac);
		fc->isac.type = IPAC_TYPE_ISACX;
		break;
	default:
		release_region(fc->addr, 32);
		pr_info("%s: AVM unknown type %d\n", fc->name, fc->type);
		return -ENODEV;
	}
	pr_notice("%s: %s config irq:%d base:0x%X\n", fc->name,
		(fc->type == AVM_FRITZ_PCI) ? "AVM Fritz!CARD PCI" :
		"AVM Fritz!CARD PCIv2", fc->irq, fc->addr);
	return 0;
}

static void
release_card(struct fritzcard *card)
{
	u_long flags;

	disable_hwirq(card);
	spin_lock_irqsave(&card->lock, flags);
	modehdlc(&card->bch[0], ISDN_P_NONE);
	modehdlc(&card->bch[1], ISDN_P_NONE);
	spin_unlock_irqrestore(&card->lock, flags);
	card->isac.release(&card->isac);
	free_irq(card->irq, card);
	mISDN_freebchannel(&card->bch[1]);
	mISDN_freebchannel(&card->bch[0]);
	mISDN_unregister_device(&card->isac.dch.dev);
	release_region(card->addr, 32);
	pci_disable_device(card->pdev);
	pci_set_drvdata(card->pdev, NULL);
	write_lock_irqsave(&card_lock, flags);
	list_del(&card->list);
	write_unlock_irqrestore(&card_lock, flags);
	kfree(card);
	AVM_cnt--;
}

static int __devinit
setup_instance(struct fritzcard *card)
{
	int i, err;
	u_long flags;

	snprintf(card->name, MISDN_MAX_IDLEN - 1, "AVM.%d", AVM_cnt + 1);
	write_lock_irqsave(&card_lock, flags);
	list_add_tail(&card->list, &Cards);
	write_unlock_irqrestore(&card_lock, flags);

	_set_debug(card);
	card->isac.name = card->name;
	spin_lock_init(&card->lock);
	card->isac.hwlock = &card->lock;
	mISDNisac_init(&card->isac, card);

	card->isac.dch.dev.Bprotocols = (1 << (ISDN_P_B_RAW & ISDN_P_B_MASK)) |
	    (1 << (ISDN_P_B_HDLC & ISDN_P_B_MASK));
	card->isac.dch.dev.D.ctrl = avm_dctrl;
	for (i = 0; i < 2; i++) {
		card->bch[i].nr = i + 1;
		set_channelmap(i + 1, card->isac.dch.dev.channelmap);
		mISDN_initbchannel(&card->bch[i], MAX_DATA_MEM);
		card->bch[i].hw = card;
		card->bch[i].ch.send = avm_l2l1B;
		card->bch[i].ch.ctrl = avm_bctrl;
		card->bch[i].ch.nr = i + 1;
		list_add(&card->bch[i].ch.list, &card->isac.dch.dev.bchannels);
	}
	err = setup_fritz(card);
	if (err)
		goto error;
	err = mISDN_register_device(&card->isac.dch.dev, &card->pdev->dev,
		card->name);
	if (err)
		goto error_reg;
	err = init_card(card);
	if (!err)  {
		AVM_cnt++;
		pr_notice("AVM %d cards installed DEBUG\n", AVM_cnt);
		return 0;
	}
	mISDN_unregister_device(&card->isac.dch.dev);
error_reg:
	release_region(card->addr, 32);
error:
	card->isac.release(&card->isac);
	mISDN_freebchannel(&card->bch[1]);
	mISDN_freebchannel(&card->bch[0]);
	write_lock_irqsave(&card_lock, flags);
	list_del(&card->list);
	write_unlock_irqrestore(&card_lock, flags);
	kfree(card);
	return err;
}

static int __devinit
fritzpci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = -ENOMEM;
	struct fritzcard *card;

	card = kzalloc(sizeof(struct fritzcard), GFP_KERNEL);
	if (!card) {
		pr_info("No kmem for fritzcard\n");
		return err;
	}
	if (pdev->device == PCI_DEVICE_ID_AVM_A1_V2)
		card->type = AVM_FRITZ_PCIV2;
	else
		card->type = AVM_FRITZ_PCI;
	card->pdev = pdev;
	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return err;
	}

	pr_notice("mISDN: found adapter %s at %s\n",
	       (char *) ent->driver_data, pci_name(pdev));

	card->addr = pci_resource_start(pdev, 1);
	card->irq = pdev->irq;
	pci_set_drvdata(pdev, card);
	err = setup_instance(card);
	if (err)
		pci_set_drvdata(pdev, NULL);
	return err;
}

static void __devexit
fritz_remove_pci(struct pci_dev *pdev)
{
	struct fritzcard *card = pci_get_drvdata(pdev);

	if (card)
		release_card(card);
	else
		if (debug)
			pr_info("%s: drvdata already removed\n", __func__);
}

static struct pci_device_id fcpci_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_A1, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Fritz!Card PCI"},
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_A1_V2, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Fritz!Card PCI v2" },
	{ }
};
MODULE_DEVICE_TABLE(pci, fcpci_ids);

static struct pci_driver fcpci_driver = {
	.name = "fcpci",
	.probe = fritzpci_probe,
	.remove = __devexit_p(fritz_remove_pci),
	.id_table = fcpci_ids,
};

static int __init AVM_init(void)
{
	int err;

	pr_notice("AVM Fritz PCI driver Rev. %s\n", AVMFRITZ_REV);
	err = pci_register_driver(&fcpci_driver);
	return err;
}

static void __exit AVM_cleanup(void)
{
	pci_unregister_driver(&fcpci_driver);
}

module_init(AVM_init);
module_exit(AVM_cleanup);
