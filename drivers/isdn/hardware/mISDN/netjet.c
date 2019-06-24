// SPDX-License-Identifier: GPL-2.0-only
/*
 * NETJet mISDN driver
 *
 * Author       Karsten Keil <keil@isdn4linux.de>
 *
 * Copyright 2009  by Karsten Keil <keil@isdn4linux.de>
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/mISDNhw.h>
#include <linux/slab.h>
#include "ipac.h"
#include "iohelper.h"
#include "netjet.h"
#include <linux/isdn/hdlc.h>

#define NETJET_REV	"2.0"

enum nj_types {
	NETJET_S_TJ300,
	NETJET_S_TJ320,
	ENTERNOW__TJ320,
};

struct tiger_dma {
	size_t		size;
	u32		*start;
	int		idx;
	u32		dmastart;
	u32		dmairq;
	u32		dmaend;
	u32		dmacur;
};

struct tiger_hw;

struct tiger_ch {
	struct bchannel		bch;
	struct tiger_hw		*nj;
	int			idx;
	int			free;
	int			lastrx;
	u16			rxstate;
	u16			txstate;
	struct isdnhdlc_vars	hsend;
	struct isdnhdlc_vars	hrecv;
	u8			*hsbuf;
	u8			*hrbuf;
};

#define TX_INIT		0x0001
#define TX_IDLE		0x0002
#define TX_RUN		0x0004
#define TX_UNDERRUN	0x0100
#define RX_OVERRUN	0x0100

#define LOG_SIZE	64

struct tiger_hw {
	struct list_head	list;
	struct pci_dev		*pdev;
	char			name[MISDN_MAX_IDLEN];
	enum nj_types		typ;
	int			irq;
	u32			irqcnt;
	u32			base;
	size_t			base_s;
	dma_addr_t		dma;
	void			*dma_p;
	spinlock_t		lock;	/* lock HW */
	struct isac_hw		isac;
	struct tiger_dma	send;
	struct tiger_dma	recv;
	struct tiger_ch		bc[2];
	u8			ctrlreg;
	u8			dmactrl;
	u8			auxd;
	u8			last_is0;
	u8			irqmask0;
	char			log[LOG_SIZE];
};

static LIST_HEAD(Cards);
static DEFINE_RWLOCK(card_lock); /* protect Cards */
static u32 debug;
static int nj_cnt;

static void
_set_debug(struct tiger_hw *card)
{
	card->isac.dch.debug = debug;
	card->bc[0].bch.debug = debug;
	card->bc[1].bch.debug = debug;
}

static int
set_debug(const char *val, const struct kernel_param *kp)
{
	int ret;
	struct tiger_hw *card;

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
MODULE_VERSION(NETJET_REV);
module_param_call(debug, set_debug, param_get_uint, &debug, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Netjet debug mask");

static void
nj_disable_hwirq(struct tiger_hw *card)
{
	outb(0, card->base + NJ_IRQMASK0);
	outb(0, card->base + NJ_IRQMASK1);
}


static u8
ReadISAC_nj(void *p, u8 offset)
{
	struct tiger_hw *card = p;
	u8 ret;

	card->auxd &= 0xfc;
	card->auxd |= (offset >> 4) & 3;
	outb(card->auxd, card->base + NJ_AUXDATA);
	ret = inb(card->base + NJ_ISAC_OFF + ((offset & 0x0f) << 2));
	return ret;
}

static void
WriteISAC_nj(void *p, u8 offset, u8 value)
{
	struct tiger_hw *card = p;

	card->auxd &= 0xfc;
	card->auxd |= (offset >> 4) & 3;
	outb(card->auxd, card->base + NJ_AUXDATA);
	outb(value, card->base + NJ_ISAC_OFF + ((offset & 0x0f) << 2));
}

static void
ReadFiFoISAC_nj(void *p, u8 offset, u8 *data, int size)
{
	struct tiger_hw *card = p;

	card->auxd &= 0xfc;
	outb(card->auxd, card->base + NJ_AUXDATA);
	insb(card->base + NJ_ISAC_OFF, data, size);
}

static void
WriteFiFoISAC_nj(void *p, u8 offset, u8 *data, int size)
{
	struct tiger_hw *card = p;

	card->auxd &= 0xfc;
	outb(card->auxd, card->base + NJ_AUXDATA);
	outsb(card->base + NJ_ISAC_OFF, data, size);
}

static void
fill_mem(struct tiger_ch *bc, u32 idx, u32 cnt, u32 fill)
{
	struct tiger_hw *card = bc->bch.hw;
	u32 mask = 0xff, val;

	pr_debug("%s: B%1d fill %02x len %d idx %d/%d\n", card->name,
		 bc->bch.nr, fill, cnt, idx, card->send.idx);
	if (bc->bch.nr & 2) {
		fill  <<= 8;
		mask <<= 8;
	}
	mask ^= 0xffffffff;
	while (cnt--) {
		val = card->send.start[idx];
		val &= mask;
		val |= fill;
		card->send.start[idx++] = val;
		if (idx >= card->send.size)
			idx = 0;
	}
}

static int
mode_tiger(struct tiger_ch *bc, u32 protocol)
{
	struct tiger_hw *card = bc->bch.hw;

	pr_debug("%s: B%1d protocol %x-->%x\n", card->name,
		 bc->bch.nr, bc->bch.state, protocol);
	switch (protocol) {
	case ISDN_P_NONE:
		if (bc->bch.state == ISDN_P_NONE)
			break;
		fill_mem(bc, 0, card->send.size, 0xff);
		bc->bch.state = protocol;
		/* only stop dma and interrupts if both channels NULL */
		if ((card->bc[0].bch.state == ISDN_P_NONE) &&
		    (card->bc[1].bch.state == ISDN_P_NONE)) {
			card->dmactrl = 0;
			outb(card->dmactrl, card->base + NJ_DMACTRL);
			outb(0, card->base + NJ_IRQMASK0);
		}
		test_and_clear_bit(FLG_HDLC, &bc->bch.Flags);
		test_and_clear_bit(FLG_TRANSPARENT, &bc->bch.Flags);
		bc->txstate = 0;
		bc->rxstate = 0;
		bc->lastrx = -1;
		break;
	case ISDN_P_B_RAW:
		test_and_set_bit(FLG_TRANSPARENT, &bc->bch.Flags);
		bc->bch.state = protocol;
		bc->idx = 0;
		bc->free = card->send.size / 2;
		bc->rxstate = 0;
		bc->txstate = TX_INIT | TX_IDLE;
		bc->lastrx = -1;
		if (!card->dmactrl) {
			card->dmactrl = 1;
			outb(card->dmactrl, card->base + NJ_DMACTRL);
			outb(0x0f, card->base + NJ_IRQMASK0);
		}
		break;
	case ISDN_P_B_HDLC:
		test_and_set_bit(FLG_HDLC, &bc->bch.Flags);
		bc->bch.state = protocol;
		bc->idx = 0;
		bc->free = card->send.size / 2;
		bc->rxstate = 0;
		bc->txstate = TX_INIT | TX_IDLE;
		isdnhdlc_rcv_init(&bc->hrecv, 0);
		isdnhdlc_out_init(&bc->hsend, 0);
		bc->lastrx = -1;
		if (!card->dmactrl) {
			card->dmactrl = 1;
			outb(card->dmactrl, card->base + NJ_DMACTRL);
			outb(0x0f, card->base + NJ_IRQMASK0);
		}
		break;
	default:
		pr_info("%s: %s protocol %x not handled\n", card->name,
			__func__, protocol);
		return -ENOPROTOOPT;
	}
	card->send.dmacur = inl(card->base + NJ_DMA_READ_ADR);
	card->recv.dmacur = inl(card->base + NJ_DMA_WRITE_ADR);
	card->send.idx = (card->send.dmacur - card->send.dmastart) >> 2;
	card->recv.idx = (card->recv.dmacur - card->recv.dmastart) >> 2;
	pr_debug("%s: %s ctrl %x irq  %02x/%02x idx %d/%d\n",
		 card->name, __func__,
		 inb(card->base + NJ_DMACTRL),
		 inb(card->base + NJ_IRQMASK0),
		 inb(card->base + NJ_IRQSTAT0),
		 card->send.idx,
		 card->recv.idx);
	return 0;
}

static void
nj_reset(struct tiger_hw *card)
{
	outb(0xff, card->base + NJ_CTRL); /* Reset On */
	mdelay(1);

	/* now edge triggered for TJ320 GE 13/07/00 */
	/* see comment in IRQ function */
	if (card->typ == NETJET_S_TJ320) /* TJ320 */
		card->ctrlreg = 0x40;  /* Reset Off and status read clear */
	else
		card->ctrlreg = 0x00;  /* Reset Off and status read clear */
	outb(card->ctrlreg, card->base + NJ_CTRL);
	mdelay(10);

	/* configure AUX pins (all output except ISAC IRQ pin) */
	card->auxd = 0;
	card->dmactrl = 0;
	outb(~NJ_ISACIRQ, card->base + NJ_AUXCTRL);
	outb(NJ_ISACIRQ,  card->base + NJ_IRQMASK1);
	outb(card->auxd, card->base + NJ_AUXDATA);
}

static int
inittiger(struct tiger_hw *card)
{
	int i;

	card->dma_p = pci_alloc_consistent(card->pdev, NJ_DMA_SIZE,
					   &card->dma);
	if (!card->dma_p) {
		pr_info("%s: No DMA memory\n", card->name);
		return -ENOMEM;
	}
	if ((u64)card->dma > 0xffffffff) {
		pr_info("%s: DMA outside 32 bit\n", card->name);
		return -ENOMEM;
	}
	for (i = 0; i < 2; i++) {
		card->bc[i].hsbuf = kmalloc(NJ_DMA_TXSIZE, GFP_ATOMIC);
		if (!card->bc[i].hsbuf) {
			pr_info("%s: no B%d send buffer\n", card->name, i + 1);
			return -ENOMEM;
		}
		card->bc[i].hrbuf = kmalloc(NJ_DMA_RXSIZE, GFP_ATOMIC);
		if (!card->bc[i].hrbuf) {
			pr_info("%s: no B%d recv buffer\n", card->name, i + 1);
			return -ENOMEM;
		}
	}
	memset(card->dma_p, 0xff, NJ_DMA_SIZE);

	card->send.start = card->dma_p;
	card->send.dmastart = (u32)card->dma;
	card->send.dmaend = card->send.dmastart +
		(4 * (NJ_DMA_TXSIZE - 1));
	card->send.dmairq = card->send.dmastart +
		(4 * ((NJ_DMA_TXSIZE / 2) - 1));
	card->send.size = NJ_DMA_TXSIZE;

	if (debug & DEBUG_HW)
		pr_notice("%s: send buffer phy %#x - %#x - %#x  virt %p"
			  " size %zu u32\n", card->name,
			  card->send.dmastart, card->send.dmairq,
			  card->send.dmaend, card->send.start, card->send.size);

	outl(card->send.dmastart, card->base + NJ_DMA_READ_START);
	outl(card->send.dmairq, card->base + NJ_DMA_READ_IRQ);
	outl(card->send.dmaend, card->base + NJ_DMA_READ_END);

	card->recv.start = card->dma_p + (NJ_DMA_SIZE / 2);
	card->recv.dmastart = (u32)card->dma  + (NJ_DMA_SIZE / 2);
	card->recv.dmaend = card->recv.dmastart +
		(4 * (NJ_DMA_RXSIZE - 1));
	card->recv.dmairq = card->recv.dmastart +
		(4 * ((NJ_DMA_RXSIZE / 2) - 1));
	card->recv.size = NJ_DMA_RXSIZE;

	if (debug & DEBUG_HW)
		pr_notice("%s: recv buffer phy %#x - %#x - %#x  virt %p"
			  " size %zu u32\n", card->name,
			  card->recv.dmastart, card->recv.dmairq,
			  card->recv.dmaend, card->recv.start, card->recv.size);

	outl(card->recv.dmastart, card->base + NJ_DMA_WRITE_START);
	outl(card->recv.dmairq, card->base + NJ_DMA_WRITE_IRQ);
	outl(card->recv.dmaend, card->base + NJ_DMA_WRITE_END);
	return 0;
}

static void
read_dma(struct tiger_ch *bc, u32 idx, int cnt)
{
	struct tiger_hw *card = bc->bch.hw;
	int i, stat;
	u32 val;
	u8 *p, *pn;

	if (bc->lastrx == idx) {
		bc->rxstate |= RX_OVERRUN;
		pr_info("%s: B%1d overrun at idx %d\n", card->name,
			bc->bch.nr, idx);
	}
	bc->lastrx = idx;
	if (test_bit(FLG_RX_OFF, &bc->bch.Flags)) {
		bc->bch.dropcnt += cnt;
		return;
	}
	stat = bchannel_get_rxbuf(&bc->bch, cnt);
	/* only transparent use the count here, HDLC overun is detected later */
	if (stat == -ENOMEM) {
		pr_warning("%s.B%d: No memory for %d bytes\n",
			   card->name, bc->bch.nr, cnt);
		return;
	}
	if (test_bit(FLG_TRANSPARENT, &bc->bch.Flags))
		p = skb_put(bc->bch.rx_skb, cnt);
	else
		p = bc->hrbuf;

	for (i = 0; i < cnt; i++) {
		val = card->recv.start[idx++];
		if (bc->bch.nr & 2)
			val >>= 8;
		if (idx >= card->recv.size)
			idx = 0;
		p[i] = val & 0xff;
	}

	if (test_bit(FLG_TRANSPARENT, &bc->bch.Flags)) {
		recv_Bchannel(&bc->bch, 0, false);
		return;
	}

	pn = bc->hrbuf;
	while (cnt > 0) {
		stat = isdnhdlc_decode(&bc->hrecv, pn, cnt, &i,
				       bc->bch.rx_skb->data, bc->bch.maxlen);
		if (stat > 0) { /* valid frame received */
			p = skb_put(bc->bch.rx_skb, stat);
			if (debug & DEBUG_HW_BFIFO) {
				snprintf(card->log, LOG_SIZE,
					 "B%1d-recv %s %d ", bc->bch.nr,
					 card->name, stat);
				print_hex_dump_bytes(card->log,
						     DUMP_PREFIX_OFFSET, p,
						     stat);
			}
			recv_Bchannel(&bc->bch, 0, false);
			stat = bchannel_get_rxbuf(&bc->bch, bc->bch.maxlen);
			if (stat < 0) {
				pr_warning("%s.B%d: No memory for %d bytes\n",
					   card->name, bc->bch.nr, cnt);
				return;
			}
		} else if (stat == -HDLC_CRC_ERROR) {
			pr_info("%s: B%1d receive frame CRC error\n",
				card->name, bc->bch.nr);
		} else if (stat == -HDLC_FRAMING_ERROR) {
			pr_info("%s: B%1d receive framing error\n",
				card->name, bc->bch.nr);
		} else if (stat == -HDLC_LENGTH_ERROR) {
			pr_info("%s: B%1d receive frame too long (> %d)\n",
				card->name, bc->bch.nr, bc->bch.maxlen);
		}
		pn += i;
		cnt -= i;
	}
}

static void
recv_tiger(struct tiger_hw *card, u8 irq_stat)
{
	u32 idx;
	int cnt = card->recv.size / 2;

	/* Note receive is via the WRITE DMA channel */
	card->last_is0 &= ~NJ_IRQM0_WR_MASK;
	card->last_is0 |= (irq_stat & NJ_IRQM0_WR_MASK);

	if (irq_stat & NJ_IRQM0_WR_END)
		idx = cnt - 1;
	else
		idx = card->recv.size - 1;

	if (test_bit(FLG_ACTIVE, &card->bc[0].bch.Flags))
		read_dma(&card->bc[0], idx, cnt);
	if (test_bit(FLG_ACTIVE, &card->bc[1].bch.Flags))
		read_dma(&card->bc[1], idx, cnt);
}

/* sync with current DMA address at start or after exception */
static void
resync(struct tiger_ch *bc, struct tiger_hw *card)
{
	card->send.dmacur = inl(card->base | NJ_DMA_READ_ADR);
	card->send.idx = (card->send.dmacur - card->send.dmastart) >> 2;
	if (bc->free > card->send.size / 2)
		bc->free = card->send.size / 2;
	/* currently we simple sync to the next complete free area
	 * this hast the advantage that we have always maximum time to
	 * handle TX irq
	 */
	if (card->send.idx < ((card->send.size / 2) - 1))
		bc->idx = (card->recv.size / 2) - 1;
	else
		bc->idx = card->recv.size - 1;
	bc->txstate = TX_RUN;
	pr_debug("%s: %s B%1d free %d idx %d/%d\n", card->name,
		 __func__, bc->bch.nr, bc->free, bc->idx, card->send.idx);
}

static int bc_next_frame(struct tiger_ch *);

static void
fill_hdlc_flag(struct tiger_ch *bc)
{
	struct tiger_hw *card = bc->bch.hw;
	int count, i;
	u32 m, v;
	u8  *p;

	if (bc->free == 0)
		return;
	pr_debug("%s: %s B%1d %d state %x idx %d/%d\n", card->name,
		 __func__, bc->bch.nr, bc->free, bc->txstate,
		 bc->idx, card->send.idx);
	if (bc->txstate & (TX_IDLE | TX_INIT | TX_UNDERRUN))
		resync(bc, card);
	count = isdnhdlc_encode(&bc->hsend, NULL, 0, &i,
				bc->hsbuf, bc->free);
	pr_debug("%s: B%1d hdlc encoded %d flags\n", card->name,
		 bc->bch.nr, count);
	bc->free -= count;
	p = bc->hsbuf;
	m = (bc->bch.nr & 1) ? 0xffffff00 : 0xffff00ff;
	for (i = 0; i < count; i++) {
		if (bc->idx >= card->send.size)
			bc->idx = 0;
		v = card->send.start[bc->idx];
		v &= m;
		v |= (bc->bch.nr & 1) ? (u32)(p[i]) : ((u32)(p[i])) << 8;
		card->send.start[bc->idx++] = v;
	}
	if (debug & DEBUG_HW_BFIFO) {
		snprintf(card->log, LOG_SIZE, "B%1d-send %s %d ",
			 bc->bch.nr, card->name, count);
		print_hex_dump_bytes(card->log, DUMP_PREFIX_OFFSET, p, count);
	}
}

static void
fill_dma(struct tiger_ch *bc)
{
	struct tiger_hw *card = bc->bch.hw;
	int count, i, fillempty = 0;
	u32 m, v, n = 0;
	u8  *p;

	if (bc->free == 0)
		return;
	if (!bc->bch.tx_skb) {
		if (!test_bit(FLG_TX_EMPTY, &bc->bch.Flags))
			return;
		fillempty = 1;
		count = card->send.size >> 1;
		p = bc->bch.fill;
	} else {
		count = bc->bch.tx_skb->len - bc->bch.tx_idx;
		if (count <= 0)
			return;
		pr_debug("%s: %s B%1d %d/%d/%d/%d state %x idx %d/%d\n",
			 card->name, __func__, bc->bch.nr, count, bc->free,
			 bc->bch.tx_idx, bc->bch.tx_skb->len, bc->txstate,
			 bc->idx, card->send.idx);
		p = bc->bch.tx_skb->data + bc->bch.tx_idx;
	}
	if (bc->txstate & (TX_IDLE | TX_INIT | TX_UNDERRUN))
		resync(bc, card);
	if (test_bit(FLG_HDLC, &bc->bch.Flags) && !fillempty) {
		count = isdnhdlc_encode(&bc->hsend, p, count, &i,
					bc->hsbuf, bc->free);
		pr_debug("%s: B%1d hdlc encoded %d in %d\n", card->name,
			 bc->bch.nr, i, count);
		bc->bch.tx_idx += i;
		bc->free -= count;
		p = bc->hsbuf;
	} else {
		if (count > bc->free)
			count = bc->free;
		if (!fillempty)
			bc->bch.tx_idx += count;
		bc->free -= count;
	}
	m = (bc->bch.nr & 1) ? 0xffffff00 : 0xffff00ff;
	if (fillempty) {
		n = p[0];
		if (!(bc->bch.nr & 1))
			n <<= 8;
		for (i = 0; i < count; i++) {
			if (bc->idx >= card->send.size)
				bc->idx = 0;
			v = card->send.start[bc->idx];
			v &= m;
			v |= n;
			card->send.start[bc->idx++] = v;
		}
	} else {
		for (i = 0; i < count; i++) {
			if (bc->idx >= card->send.size)
				bc->idx = 0;
			v = card->send.start[bc->idx];
			v &= m;
			n = p[i];
			v |= (bc->bch.nr & 1) ? n : n << 8;
			card->send.start[bc->idx++] = v;
		}
	}
	if (debug & DEBUG_HW_BFIFO) {
		snprintf(card->log, LOG_SIZE, "B%1d-send %s %d ",
			 bc->bch.nr, card->name, count);
		print_hex_dump_bytes(card->log, DUMP_PREFIX_OFFSET, p, count);
	}
	if (bc->free)
		bc_next_frame(bc);
}


static int
bc_next_frame(struct tiger_ch *bc)
{
	int ret = 1;

	if (bc->bch.tx_skb && bc->bch.tx_idx < bc->bch.tx_skb->len) {
		fill_dma(bc);
	} else {
		if (bc->bch.tx_skb)
			dev_kfree_skb(bc->bch.tx_skb);
		if (get_next_bframe(&bc->bch)) {
			fill_dma(bc);
			test_and_clear_bit(FLG_TX_EMPTY, &bc->bch.Flags);
		} else if (test_bit(FLG_TX_EMPTY, &bc->bch.Flags)) {
			fill_dma(bc);
		} else if (test_bit(FLG_FILLEMPTY, &bc->bch.Flags)) {
			test_and_set_bit(FLG_TX_EMPTY, &bc->bch.Flags);
			ret = 0;
		} else {
			ret = 0;
		}
	}
	return ret;
}

static void
send_tiger_bc(struct tiger_hw *card, struct tiger_ch *bc)
{
	int ret;

	bc->free += card->send.size / 2;
	if (bc->free >= card->send.size) {
		if (!(bc->txstate & (TX_UNDERRUN | TX_INIT))) {
			pr_info("%s: B%1d TX underrun state %x\n", card->name,
				bc->bch.nr, bc->txstate);
			bc->txstate |= TX_UNDERRUN;
		}
		bc->free = card->send.size;
	}
	ret = bc_next_frame(bc);
	if (!ret) {
		if (test_bit(FLG_HDLC, &bc->bch.Flags)) {
			fill_hdlc_flag(bc);
			return;
		}
		pr_debug("%s: B%1d TX no data free %d idx %d/%d\n", card->name,
			 bc->bch.nr, bc->free, bc->idx, card->send.idx);
		if (!(bc->txstate & (TX_IDLE | TX_INIT))) {
			fill_mem(bc, bc->idx, bc->free, 0xff);
			if (bc->free == card->send.size)
				bc->txstate |= TX_IDLE;
		}
	}
}

static void
send_tiger(struct tiger_hw *card, u8 irq_stat)
{
	int i;

	/* Note send is via the READ DMA channel */
	if ((irq_stat & card->last_is0) & NJ_IRQM0_RD_MASK) {
		pr_info("%s: tiger warn write double dma %x/%x\n",
			card->name, irq_stat, card->last_is0);
		return;
	} else {
		card->last_is0 &= ~NJ_IRQM0_RD_MASK;
		card->last_is0 |= (irq_stat & NJ_IRQM0_RD_MASK);
	}
	for (i = 0; i < 2; i++) {
		if (test_bit(FLG_ACTIVE, &card->bc[i].bch.Flags))
			send_tiger_bc(card, &card->bc[i]);
	}
}

static irqreturn_t
nj_irq(int intno, void *dev_id)
{
	struct tiger_hw *card = dev_id;
	u8 val, s1val, s0val;

	spin_lock(&card->lock);
	s0val = inb(card->base | NJ_IRQSTAT0);
	s1val = inb(card->base | NJ_IRQSTAT1);
	if ((s1val & NJ_ISACIRQ) && (s0val == 0)) {
		/* shared IRQ */
		spin_unlock(&card->lock);
		return IRQ_NONE;
	}
	pr_debug("%s: IRQSTAT0 %02x IRQSTAT1 %02x\n", card->name, s0val, s1val);
	card->irqcnt++;
	if (!(s1val & NJ_ISACIRQ)) {
		val = ReadISAC_nj(card, ISAC_ISTA);
		if (val)
			mISDNisac_irq(&card->isac, val);
	}

	if (s0val)
		/* write to clear */
		outb(s0val, card->base | NJ_IRQSTAT0);
	else
		goto end;
	s1val = s0val;
	/* set bits in sval to indicate which page is free */
	card->recv.dmacur = inl(card->base | NJ_DMA_WRITE_ADR);
	card->recv.idx = (card->recv.dmacur - card->recv.dmastart) >> 2;
	if (card->recv.dmacur < card->recv.dmairq)
		s0val = 0x08;	/* the 2nd write area is free */
	else
		s0val = 0x04;	/* the 1st write area is free */

	card->send.dmacur = inl(card->base | NJ_DMA_READ_ADR);
	card->send.idx = (card->send.dmacur - card->send.dmastart) >> 2;
	if (card->send.dmacur < card->send.dmairq)
		s0val |= 0x02;	/* the 2nd read area is free */
	else
		s0val |= 0x01;	/* the 1st read area is free */

	pr_debug("%s: DMA Status %02x/%02x/%02x %d/%d\n", card->name,
		 s1val, s0val, card->last_is0,
		 card->recv.idx, card->send.idx);
	/* test if we have a DMA interrupt */
	if (s0val != card->last_is0) {
		if ((s0val & NJ_IRQM0_RD_MASK) !=
		    (card->last_is0 & NJ_IRQM0_RD_MASK))
			/* got a write dma int */
			send_tiger(card, s0val);
		if ((s0val & NJ_IRQM0_WR_MASK) !=
		    (card->last_is0 & NJ_IRQM0_WR_MASK))
			/* got a read dma int */
			recv_tiger(card, s0val);
	}
end:
	spin_unlock(&card->lock);
	return IRQ_HANDLED;
}

static int
nj_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb)
{
	int ret = -EINVAL;
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	struct tiger_ch *bc = container_of(bch, struct tiger_ch, bch);
	struct tiger_hw *card = bch->hw;
	struct mISDNhead *hh = mISDN_HEAD_P(skb);
	unsigned long flags;

	switch (hh->prim) {
	case PH_DATA_REQ:
		spin_lock_irqsave(&card->lock, flags);
		ret = bchannel_senddata(bch, skb);
		if (ret > 0) { /* direct TX */
			fill_dma(bc);
			ret = 0;
		}
		spin_unlock_irqrestore(&card->lock, flags);
		return ret;
	case PH_ACTIVATE_REQ:
		spin_lock_irqsave(&card->lock, flags);
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags))
			ret = mode_tiger(bc, ch->protocol);
		else
			ret = 0;
		spin_unlock_irqrestore(&card->lock, flags);
		if (!ret)
			_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY, 0,
				    NULL, GFP_KERNEL);
		break;
	case PH_DEACTIVATE_REQ:
		spin_lock_irqsave(&card->lock, flags);
		mISDN_clear_bchannel(bch);
		mode_tiger(bc, ISDN_P_NONE);
		spin_unlock_irqrestore(&card->lock, flags);
		_queue_data(ch, PH_DEACTIVATE_IND, MISDN_ID_ANY, 0,
			    NULL, GFP_KERNEL);
		ret = 0;
		break;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

static int
channel_bctrl(struct tiger_ch *bc, struct mISDN_ctrl_req *cq)
{
	return mISDN_ctrl_bchannel(&bc->bch, cq);
}

static int
nj_bctrl(struct mISDNchannel *ch, u32 cmd, void *arg)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	struct tiger_ch *bc = container_of(bch, struct tiger_ch, bch);
	struct tiger_hw *card  = bch->hw;
	int ret = -EINVAL;
	u_long flags;

	pr_debug("%s: %s cmd:%x %p\n", card->name, __func__, cmd, arg);
	switch (cmd) {
	case CLOSE_CHANNEL:
		test_and_clear_bit(FLG_OPEN, &bch->Flags);
		cancel_work_sync(&bch->workq);
		spin_lock_irqsave(&card->lock, flags);
		mISDN_clear_bchannel(bch);
		mode_tiger(bc, ISDN_P_NONE);
		spin_unlock_irqrestore(&card->lock, flags);
		ch->protocol = ISDN_P_NONE;
		ch->peer = NULL;
		module_put(THIS_MODULE);
		ret = 0;
		break;
	case CONTROL_CHANNEL:
		ret = channel_bctrl(bc, arg);
		break;
	default:
		pr_info("%s: %s unknown prim(%x)\n", card->name, __func__, cmd);
	}
	return ret;
}

static int
channel_ctrl(struct tiger_hw *card, struct mISDN_ctrl_req *cq)
{
	int	ret = 0;

	switch (cq->op) {
	case MISDN_CTRL_GETOP:
		cq->op = MISDN_CTRL_LOOP | MISDN_CTRL_L1_TIMER3;
		break;
	case MISDN_CTRL_LOOP:
		/* cq->channel: 0 disable, 1 B1 loop 2 B2 loop, 3 both */
		if (cq->channel < 0 || cq->channel > 3) {
			ret = -EINVAL;
			break;
		}
		ret = card->isac.ctrl(&card->isac, HW_TESTLOOP, cq->channel);
		break;
	case MISDN_CTRL_L1_TIMER3:
		ret = card->isac.ctrl(&card->isac, HW_TIMER3_VALUE, cq->p1);
		break;
	default:
		pr_info("%s: %s unknown Op %x\n", card->name, __func__, cq->op);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int
open_bchannel(struct tiger_hw *card, struct channel_req *rq)
{
	struct bchannel *bch;

	if (rq->adr.channel == 0 || rq->adr.channel > 2)
		return -EINVAL;
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;
	bch = &card->bc[rq->adr.channel - 1].bch;
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
nj_dctrl(struct mISDNchannel *ch, u32 cmd, void *arg)
{
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct tiger_hw	*card = dch->hw;
	struct channel_req	*rq;
	int			err = 0;

	pr_debug("%s: %s cmd:%x %p\n", card->name, __func__, cmd, arg);
	switch (cmd) {
	case OPEN_CHANNEL:
		rq = arg;
		if (rq->protocol == ISDN_P_TE_S0)
			err = card->isac.open(&card->isac, rq);
		else
			err = open_bchannel(card, rq);
		if (err)
			break;
		if (!try_module_get(THIS_MODULE))
			pr_info("%s: cannot get module\n", card->name);
		break;
	case CLOSE_CHANNEL:
		pr_debug("%s: dev(%d) close from %p\n", card->name, dch->dev.id,
			 __builtin_return_address(0));
		module_put(THIS_MODULE);
		break;
	case CONTROL_CHANNEL:
		err = channel_ctrl(card, arg);
		break;
	default:
		pr_debug("%s: %s unknown command %x\n",
			 card->name, __func__, cmd);
		return -EINVAL;
	}
	return err;
}

static int
nj_init_card(struct tiger_hw *card)
{
	u_long flags;
	int ret;

	spin_lock_irqsave(&card->lock, flags);
	nj_disable_hwirq(card);
	spin_unlock_irqrestore(&card->lock, flags);

	card->irq = card->pdev->irq;
	if (request_irq(card->irq, nj_irq, IRQF_SHARED, card->name, card)) {
		pr_info("%s: couldn't get interrupt %d\n",
			card->name, card->irq);
		card->irq = -1;
		return -EIO;
	}

	spin_lock_irqsave(&card->lock, flags);
	nj_reset(card);
	ret = card->isac.init(&card->isac);
	if (ret)
		goto error;
	ret = inittiger(card);
	if (ret)
		goto error;
	mode_tiger(&card->bc[0], ISDN_P_NONE);
	mode_tiger(&card->bc[1], ISDN_P_NONE);
error:
	spin_unlock_irqrestore(&card->lock, flags);
	return ret;
}


static void
nj_release(struct tiger_hw *card)
{
	u_long flags;
	int i;

	if (card->base_s) {
		spin_lock_irqsave(&card->lock, flags);
		nj_disable_hwirq(card);
		mode_tiger(&card->bc[0], ISDN_P_NONE);
		mode_tiger(&card->bc[1], ISDN_P_NONE);
		card->isac.release(&card->isac);
		spin_unlock_irqrestore(&card->lock, flags);
		release_region(card->base, card->base_s);
		card->base_s = 0;
	}
	if (card->irq > 0)
		free_irq(card->irq, card);
	if (card->isac.dch.dev.dev.class)
		mISDN_unregister_device(&card->isac.dch.dev);

	for (i = 0; i < 2; i++) {
		mISDN_freebchannel(&card->bc[i].bch);
		kfree(card->bc[i].hsbuf);
		kfree(card->bc[i].hrbuf);
	}
	if (card->dma_p)
		pci_free_consistent(card->pdev, NJ_DMA_SIZE,
				    card->dma_p, card->dma);
	write_lock_irqsave(&card_lock, flags);
	list_del(&card->list);
	write_unlock_irqrestore(&card_lock, flags);
	pci_clear_master(card->pdev);
	pci_disable_device(card->pdev);
	pci_set_drvdata(card->pdev, NULL);
	kfree(card);
}


static int
nj_setup(struct tiger_hw *card)
{
	card->base = pci_resource_start(card->pdev, 0);
	card->base_s = pci_resource_len(card->pdev, 0);
	if (!request_region(card->base, card->base_s, card->name)) {
		pr_info("%s: NETjet config port %#x-%#x already in use\n",
			card->name, card->base,
			(u32)(card->base + card->base_s - 1));
		card->base_s = 0;
		return -EIO;
	}
	ASSIGN_FUNC(nj, ISAC, card->isac);
	return 0;
}


static int
setup_instance(struct tiger_hw *card)
{
	int i, err;
	u_long flags;

	snprintf(card->name, MISDN_MAX_IDLEN - 1, "netjet.%d", nj_cnt + 1);
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
	card->isac.dch.dev.D.ctrl = nj_dctrl;
	for (i = 0; i < 2; i++) {
		card->bc[i].bch.nr = i + 1;
		set_channelmap(i + 1, card->isac.dch.dev.channelmap);
		mISDN_initbchannel(&card->bc[i].bch, MAX_DATA_MEM,
				   NJ_DMA_RXSIZE >> 1);
		card->bc[i].bch.hw = card;
		card->bc[i].bch.ch.send = nj_l2l1B;
		card->bc[i].bch.ch.ctrl = nj_bctrl;
		card->bc[i].bch.ch.nr = i + 1;
		list_add(&card->bc[i].bch.ch.list,
			 &card->isac.dch.dev.bchannels);
		card->bc[i].bch.hw = card;
	}
	err = nj_setup(card);
	if (err)
		goto error;
	err = mISDN_register_device(&card->isac.dch.dev, &card->pdev->dev,
				    card->name);
	if (err)
		goto error;
	err = nj_init_card(card);
	if (!err)  {
		nj_cnt++;
		pr_notice("Netjet %d cards installed\n", nj_cnt);
		return 0;
	}
error:
	nj_release(card);
	return err;
}

static int
nj_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = -ENOMEM;
	int cfg;
	struct tiger_hw *card;

	if (pdev->subsystem_vendor == 0x8086 &&
	    pdev->subsystem_device == 0x0003) {
		pr_notice("Netjet: Digium X100P/X101P not handled\n");
		return -ENODEV;
	}

	if (pdev->subsystem_vendor == 0x55 &&
	    pdev->subsystem_device == 0x02) {
		pr_notice("Netjet: Enter!Now not handled yet\n");
		return -ENODEV;
	}

	if (pdev->subsystem_vendor == 0xb100 &&
	    pdev->subsystem_device == 0x0003) {
		pr_notice("Netjet: Digium TDM400P not handled yet\n");
		return -ENODEV;
	}

	card = kzalloc(sizeof(struct tiger_hw), GFP_KERNEL);
	if (!card) {
		pr_info("No kmem for Netjet\n");
		return err;
	}

	card->pdev = pdev;

	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return err;
	}

	printk(KERN_INFO "nj_probe(mISDN): found adapter at %s\n",
	       pci_name(pdev));

	pci_set_master(pdev);

	/* the TJ300 and TJ320 must be detected, the IRQ handling is different
	 * unfortunately the chips use the same device ID, but the TJ320 has
	 * the bit20 in status PCI cfg register set
	 */
	pci_read_config_dword(pdev, 0x04, &cfg);
	if (cfg & 0x00100000)
		card->typ = NETJET_S_TJ320;
	else
		card->typ = NETJET_S_TJ300;

	card->base = pci_resource_start(pdev, 0);
	card->irq = pdev->irq;
	pci_set_drvdata(pdev, card);
	err = setup_instance(card);
	if (err)
		pci_set_drvdata(pdev, NULL);

	return err;
}


static void nj_remove(struct pci_dev *pdev)
{
	struct tiger_hw *card = pci_get_drvdata(pdev);

	if (card)
		nj_release(card);
	else
		pr_info("%s drvdata already removed\n", __func__);
}

/* We cannot select cards with PCI_SUB... IDs, since here are cards with
 * SUB IDs set to PCI_ANY_ID, so we need to match all and reject
 * known other cards which not work with this driver - see probe function */
static const struct pci_device_id nj_pci_ids[] = {
	{ PCI_VENDOR_ID_TIGERJET, PCI_DEVICE_ID_TIGERJET_300,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ }
};
MODULE_DEVICE_TABLE(pci, nj_pci_ids);

static struct pci_driver nj_driver = {
	.name = "netjet",
	.probe = nj_probe,
	.remove = nj_remove,
	.id_table = nj_pci_ids,
};

static int __init nj_init(void)
{
	int err;

	pr_notice("Netjet PCI driver Rev. %s\n", NETJET_REV);
	err = pci_register_driver(&nj_driver);
	return err;
}

static void __exit nj_cleanup(void)
{
	pci_unregister_driver(&nj_driver);
}

module_init(nj_init);
module_exit(nj_cleanup);
