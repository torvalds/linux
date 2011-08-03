/*
 * w6692.c     mISDN driver for Winbond w6692 based cards
 *
 * Author      Karsten Keil <kkeil@suse.de>
 *             based on the w6692 I4L driver from Petr Novak <petr.novak@i.cz>
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

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/mISDNhw.h>
#include <linux/slab.h>
#include "w6692.h"

#define W6692_REV	"2.0"

#define DBUSY_TIMER_VALUE	80

enum {
	W6692_ASUS,
	W6692_WINBOND,
	W6692_USR
};

/* private data in the PCI devices list */
struct w6692map {
	u_int	subtype;
	char	*name;
};

static const struct w6692map  w6692_map[] =
{
	{W6692_ASUS, "Dynalink/AsusCom IS64PH"},
	{W6692_WINBOND, "Winbond W6692"},
	{W6692_USR, "USR W6692"}
};

#ifndef PCI_VENDOR_ID_USR
#define PCI_VENDOR_ID_USR	0x16ec
#define PCI_DEVICE_ID_USR_6692	0x3409
#endif

struct w6692_ch {
	struct bchannel		bch;
	u32			addr;
	struct timer_list	timer;
	u8			b_mode;
};

struct w6692_hw {
	struct list_head	list;
	struct pci_dev		*pdev;
	char			name[MISDN_MAX_IDLEN];
	u32			irq;
	u32			irqcnt;
	u32			addr;
	u32			fmask;	/* feature mask - bit set per card nr */
	int			subtype;
	spinlock_t		lock;	/* hw lock */
	u8			imask;
	u8			pctl;
	u8			xaddr;
	u8			xdata;
	u8			state;
	struct w6692_ch		bc[2];
	struct dchannel		dch;
	char			log[64];
};

static LIST_HEAD(Cards);
static DEFINE_RWLOCK(card_lock); /* protect Cards */

static int w6692_cnt;
static int debug;
static u32 led;
static u32 pots;

static void
_set_debug(struct w6692_hw *card)
{
	card->dch.debug = debug;
	card->bc[0].bch.debug = debug;
	card->bc[1].bch.debug = debug;
}

static int
set_debug(const char *val, struct kernel_param *kp)
{
	int ret;
	struct w6692_hw *card;

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
MODULE_VERSION(W6692_REV);
module_param_call(debug, set_debug, param_get_uint, &debug, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "W6692 debug mask");
module_param(led, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(led, "W6692 LED support bitmask (one bit per card)");
module_param(pots, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pots, "W6692 POTS support bitmask (one bit per card)");

static inline u8
ReadW6692(struct w6692_hw *card, u8 offset)
{
	return inb(card->addr + offset);
}

static inline void
WriteW6692(struct w6692_hw *card, u8 offset, u8 value)
{
	outb(value, card->addr + offset);
}

static inline u8
ReadW6692B(struct w6692_ch *bc, u8 offset)
{
	return inb(bc->addr + offset);
}

static inline void
WriteW6692B(struct w6692_ch *bc, u8 offset, u8 value)
{
	outb(value, bc->addr + offset);
}

static void
enable_hwirq(struct w6692_hw *card)
{
	WriteW6692(card, W_IMASK, card->imask);
}

static void
disable_hwirq(struct w6692_hw *card)
{
	WriteW6692(card, W_IMASK, 0xff);
}

static const char *W6692Ver[] = {"V00", "V01", "V10", "V11"};

static void
W6692Version(struct w6692_hw *card)
{
	int val;

	val = ReadW6692(card, W_D_RBCH);
	pr_notice("%s: Winbond W6692 version: %s\n", card->name,
		W6692Ver[(val >> 6) & 3]);
}

static void
w6692_led_handler(struct w6692_hw *card, int on)
{
	if ((!(card->fmask & led)) || card->subtype == W6692_USR)
		return;
	if (on) {
		card->xdata &= 0xfb;	/*  LED ON */
		WriteW6692(card, W_XDATA, card->xdata);
	} else {
		card->xdata |= 0x04;	/*  LED OFF */
		WriteW6692(card, W_XDATA, card->xdata);
	}
}

static void
ph_command(struct w6692_hw *card, u8 cmd)
{
	pr_debug("%s: ph_command %x\n", card->name, cmd);
	WriteW6692(card, W_CIX, cmd);
}

static void
W6692_new_ph(struct w6692_hw *card)
{
	if (card->state == W_L1CMD_RST)
		ph_command(card, W_L1CMD_DRC);
	schedule_event(&card->dch, FLG_PHCHANGE);
}

static void
W6692_ph_bh(struct dchannel *dch)
{
	struct w6692_hw *card = dch->hw;

	switch (card->state) {
	case W_L1CMD_RST:
		dch->state = 0;
		l1_event(dch->l1, HW_RESET_IND);
		break;
	case W_L1IND_CD:
		dch->state = 3;
		l1_event(dch->l1, HW_DEACT_CNF);
		break;
	case W_L1IND_DRD:
		dch->state = 3;
		l1_event(dch->l1, HW_DEACT_IND);
		break;
	case W_L1IND_CE:
		dch->state = 4;
		l1_event(dch->l1, HW_POWERUP_IND);
		break;
	case W_L1IND_LD:
		if (dch->state <= 5) {
			dch->state = 5;
			l1_event(dch->l1, ANYSIGNAL);
		} else {
			dch->state = 8;
			l1_event(dch->l1, LOSTFRAMING);
		}
		break;
	case W_L1IND_ARD:
		dch->state = 6;
		l1_event(dch->l1, INFO2);
		break;
	case W_L1IND_AI8:
		dch->state = 7;
		l1_event(dch->l1, INFO4_P8);
		break;
	case W_L1IND_AI10:
		dch->state = 7;
		l1_event(dch->l1, INFO4_P10);
		break;
	default:
		pr_debug("%s: TE unknown state %02x dch state %02x\n",
			card->name, card->state, dch->state);
		break;
	}
	pr_debug("%s: TE newstate %02x\n", card->name, dch->state);
}

static void
W6692_empty_Dfifo(struct w6692_hw *card, int count)
{
	struct dchannel *dch = &card->dch;
	u8 *ptr;

	pr_debug("%s: empty_Dfifo %d\n", card->name, count);
	if (!dch->rx_skb) {
		dch->rx_skb = mI_alloc_skb(card->dch.maxlen, GFP_ATOMIC);
		if (!dch->rx_skb) {
			pr_info("%s: D receive out of memory\n", card->name);
			WriteW6692(card, W_D_CMDR, W_D_CMDR_RACK);
			return;
		}
	}
	if ((dch->rx_skb->len + count) >= dch->maxlen) {
		pr_debug("%s: empty_Dfifo overrun %d\n", card->name,
			dch->rx_skb->len + count);
		WriteW6692(card, W_D_CMDR, W_D_CMDR_RACK);
		return;
	}
	ptr = skb_put(dch->rx_skb, count);
	insb(card->addr + W_D_RFIFO, ptr, count);
	WriteW6692(card, W_D_CMDR, W_D_CMDR_RACK);
	if (debug & DEBUG_HW_DFIFO) {
		snprintf(card->log, 63, "D-recv %s %d ",
			card->name, count);
		print_hex_dump_bytes(card->log, DUMP_PREFIX_OFFSET, ptr, count);
	}
}

static void
W6692_fill_Dfifo(struct w6692_hw *card)
{
	struct dchannel *dch = &card->dch;
	int count;
	u8 *ptr;
	u8 cmd = W_D_CMDR_XMS;

	pr_debug("%s: fill_Dfifo\n", card->name);
	if (!dch->tx_skb)
		return;
	count = dch->tx_skb->len - dch->tx_idx;
	if (count <= 0)
		return;
	if (count > W_D_FIFO_THRESH)
		count = W_D_FIFO_THRESH;
	else
		cmd |= W_D_CMDR_XME;
	ptr = dch->tx_skb->data + dch->tx_idx;
	dch->tx_idx += count;
	outsb(card->addr + W_D_XFIFO, ptr, count);
	WriteW6692(card, W_D_CMDR, cmd);
	if (test_and_set_bit(FLG_BUSY_TIMER, &dch->Flags)) {
		pr_debug("%s: fill_Dfifo dbusytimer running\n", card->name);
		del_timer(&dch->timer);
	}
	init_timer(&dch->timer);
	dch->timer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&dch->timer);
	if (debug & DEBUG_HW_DFIFO) {
		snprintf(card->log, 63, "D-send %s %d ",
			card->name, count);
		print_hex_dump_bytes(card->log, DUMP_PREFIX_OFFSET, ptr, count);
	}
}

static void
d_retransmit(struct w6692_hw *card)
{
	struct dchannel *dch = &card->dch;

	if (test_and_clear_bit(FLG_BUSY_TIMER, &dch->Flags))
		del_timer(&dch->timer);
#ifdef FIXME
	if (test_and_clear_bit(FLG_L1_BUSY, &dch->Flags))
		dchannel_sched_event(dch, D_CLEARBUSY);
#endif
	if (test_bit(FLG_TX_BUSY, &dch->Flags)) {
		/* Restart frame */
		dch->tx_idx = 0;
		W6692_fill_Dfifo(card);
	} else if (dch->tx_skb) { /* should not happen */
		pr_info("%s: %s without TX_BUSY\n", card->name, __func__);
		test_and_set_bit(FLG_TX_BUSY, &dch->Flags);
		dch->tx_idx = 0;
		W6692_fill_Dfifo(card);
	} else {
		pr_info("%s: XDU no TX_BUSY\n", card->name);
		if (get_next_dframe(dch))
			W6692_fill_Dfifo(card);
	}
}

static void
handle_rxD(struct w6692_hw *card) {
	u8	stat;
	int	count;

	stat = ReadW6692(card, W_D_RSTA);
	if (stat & (W_D_RSTA_RDOV | W_D_RSTA_CRCE | W_D_RSTA_RMB)) {
		if (stat & W_D_RSTA_RDOV) {
			pr_debug("%s: D-channel RDOV\n", card->name);
#ifdef ERROR_STATISTIC
			card->dch.err_rx++;
#endif
		}
		if (stat & W_D_RSTA_CRCE) {
			pr_debug("%s: D-channel CRC error\n", card->name);
#ifdef ERROR_STATISTIC
			card->dch.err_crc++;
#endif
		}
		if (stat & W_D_RSTA_RMB) {
			pr_debug("%s: D-channel ABORT\n", card->name);
#ifdef ERROR_STATISTIC
			card->dch.err_rx++;
#endif
		}
		if (card->dch.rx_skb)
			dev_kfree_skb(card->dch.rx_skb);
		card->dch.rx_skb = NULL;
		WriteW6692(card, W_D_CMDR, W_D_CMDR_RACK | W_D_CMDR_RRST);
	} else {
		count = ReadW6692(card, W_D_RBCL) & (W_D_FIFO_THRESH - 1);
		if (count == 0)
			count = W_D_FIFO_THRESH;
		W6692_empty_Dfifo(card, count);
		recv_Dchannel(&card->dch);
	}
}

static void
handle_txD(struct w6692_hw *card) {
	if (test_and_clear_bit(FLG_BUSY_TIMER, &card->dch.Flags))
		del_timer(&card->dch.timer);
	if (card->dch.tx_skb && card->dch.tx_idx < card->dch.tx_skb->len) {
		W6692_fill_Dfifo(card);
	} else {
		if (card->dch.tx_skb)
			dev_kfree_skb(card->dch.tx_skb);
		if (get_next_dframe(&card->dch))
			W6692_fill_Dfifo(card);
	}
}

static void
handle_statusD(struct w6692_hw *card)
{
	struct dchannel *dch = &card->dch;
	u8 exval, v1, cir;

	exval = ReadW6692(card, W_D_EXIR);

	pr_debug("%s: D_EXIR %02x\n", card->name, exval);
	if (exval & (W_D_EXI_XDUN | W_D_EXI_XCOL)) {
		/* Transmit underrun/collision */
		pr_debug("%s: D-channel underrun/collision\n", card->name);
#ifdef ERROR_STATISTIC
		dch->err_tx++;
#endif
		d_retransmit(card);
	}
	if (exval & W_D_EXI_RDOV) {	/* RDOV */
		pr_debug("%s: D-channel RDOV\n", card->name);
		WriteW6692(card, W_D_CMDR, W_D_CMDR_RRST);
	}
	if (exval & W_D_EXI_TIN2)	/* TIN2 - never */
		pr_debug("%s: spurious TIN2 interrupt\n", card->name);
	if (exval & W_D_EXI_MOC) {	/* MOC - not supported */
		v1 = ReadW6692(card, W_MOSR);
		pr_debug("%s: spurious MOC interrupt MOSR %02x\n",
			card->name, v1);
	}
	if (exval & W_D_EXI_ISC) {	/* ISC - Level1 change */
		cir = ReadW6692(card, W_CIR);
		pr_debug("%s: ISC CIR %02X\n", card->name, cir);
		if (cir & W_CIR_ICC) {
			v1 = cir & W_CIR_COD_MASK;
			pr_debug("%s: ph_state_change %x -> %x\n", card->name,
				dch->state, v1);
			card->state = v1;
			if (card->fmask & led) {
				switch (v1) {
				case W_L1IND_AI8:
				case W_L1IND_AI10:
					w6692_led_handler(card, 1);
					break;
				default:
					w6692_led_handler(card, 0);
					break;
				}
			}
			W6692_new_ph(card);
		}
		if (cir & W_CIR_SCC) {
			v1 = ReadW6692(card, W_SQR);
			pr_debug("%s: SCC SQR %02X\n", card->name, v1);
		}
	}
	if (exval & W_D_EXI_WEXP)
		pr_debug("%s: spurious WEXP interrupt!\n", card->name);
	if (exval & W_D_EXI_TEXP)
		pr_debug("%s: spurious TEXP interrupt!\n", card->name);
}

static void
W6692_empty_Bfifo(struct w6692_ch *wch, int count)
{
	struct w6692_hw *card = wch->bch.hw;
	u8 *ptr;

	pr_debug("%s: empty_Bfifo %d\n", card->name, count);
	if (unlikely(wch->bch.state == ISDN_P_NONE)) {
		pr_debug("%s: empty_Bfifo ISDN_P_NONE\n", card->name);
		WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
		if (wch->bch.rx_skb)
			skb_trim(wch->bch.rx_skb, 0);
		return;
	}
	if (!wch->bch.rx_skb) {
		wch->bch.rx_skb = mI_alloc_skb(wch->bch.maxlen, GFP_ATOMIC);
		if (unlikely(!wch->bch.rx_skb)) {
			pr_info("%s: B receive out of memory\n", card->name);
			WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RACK |
				W_B_CMDR_RACT);
			return;
		}
	}
	if (wch->bch.rx_skb->len + count > wch->bch.maxlen) {
		pr_debug("%s: empty_Bfifo incoming packet too large\n",
			card->name);
		WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
		skb_trim(wch->bch.rx_skb, 0);
		return;
	}
	ptr = skb_put(wch->bch.rx_skb, count);
	insb(wch->addr + W_B_RFIFO, ptr, count);
	WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
	if (debug & DEBUG_HW_DFIFO) {
		snprintf(card->log, 63, "B%1d-recv %s %d ",
			wch->bch.nr, card->name, count);
		print_hex_dump_bytes(card->log, DUMP_PREFIX_OFFSET, ptr, count);
	}
}

static void
W6692_fill_Bfifo(struct w6692_ch *wch)
{
	struct w6692_hw *card = wch->bch.hw;
	int count;
	u8 *ptr, cmd = W_B_CMDR_RACT | W_B_CMDR_XMS;

	pr_debug("%s: fill Bfifo\n", card->name);
	if (!wch->bch.tx_skb)
		return;
	count = wch->bch.tx_skb->len - wch->bch.tx_idx;
	if (count <= 0)
		return;
	ptr = wch->bch.tx_skb->data + wch->bch.tx_idx;
	if (count > W_B_FIFO_THRESH)
		count = W_B_FIFO_THRESH;
	else if (test_bit(FLG_HDLC, &wch->bch.Flags))
		cmd |= W_B_CMDR_XME;

	pr_debug("%s: fill Bfifo%d/%d\n", card->name,
			count, wch->bch.tx_idx);
	wch->bch.tx_idx += count;
	outsb(wch->addr + W_B_XFIFO, ptr, count);
	WriteW6692B(wch, W_B_CMDR, cmd);
	if (debug & DEBUG_HW_DFIFO) {
		snprintf(card->log, 63, "B%1d-send %s %d ",
			wch->bch.nr, card->name, count);
		print_hex_dump_bytes(card->log, DUMP_PREFIX_OFFSET, ptr, count);
	}
}

#if 0
static int
setvolume(struct w6692_ch *wch, int mic, struct sk_buff *skb)
{
	struct w6692_hw *card = wch->bch.hw;
	u16 *vol = (u16 *)skb->data;
	u8 val;

	if ((!(card->fmask & pots)) ||
	    !test_bit(FLG_TRANSPARENT, &wch->bch.Flags))
		return -ENODEV;
	if (skb->len < 2)
		return -EINVAL;
	if (*vol > 7)
		return -EINVAL;
	val = *vol & 7;
	val = 7 - val;
	if (mic) {
		val <<= 3;
		card->xaddr &= 0xc7;
	} else {
		card->xaddr &= 0xf8;
	}
	card->xaddr |= val;
	WriteW6692(card, W_XADDR, card->xaddr);
	return 0;
}

static int
enable_pots(struct w6692_ch *wch)
{
	struct w6692_hw *card = wch->bch.hw;

	if ((!(card->fmask & pots)) ||
	    !test_bit(FLG_TRANSPARENT, &wch->bch.Flags))
		return -ENODEV;
	wch->b_mode |= W_B_MODE_EPCM | W_B_MODE_BSW0;
	WriteW6692B(wch, W_B_MODE, wch->b_mode);
	WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
	card->pctl |= ((wch->bch.nr & 2) ? W_PCTL_PCX : 0);
	WriteW6692(card, W_PCTL, card->pctl);
	return 0;
}
#endif

static int
disable_pots(struct w6692_ch *wch)
{
	struct w6692_hw *card = wch->bch.hw;

	if (!(card->fmask & pots))
		return -ENODEV;
	wch->b_mode &= ~(W_B_MODE_EPCM | W_B_MODE_BSW0);
	WriteW6692B(wch, W_B_MODE, wch->b_mode);
	WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_RACT |
		W_B_CMDR_XRST);
	return 0;
}

static int
w6692_mode(struct w6692_ch *wch, u32 pr)
{
	struct w6692_hw	*card;

	card = wch->bch.hw;
	pr_debug("%s: B%d protocol %x-->%x\n", card->name,
		wch->bch.nr, wch->bch.state, pr);
	switch (pr) {
	case ISDN_P_NONE:
		if ((card->fmask & pots) && (wch->b_mode & W_B_MODE_EPCM))
			disable_pots(wch);
		wch->b_mode = 0;
		mISDN_clear_bchannel(&wch->bch);
		WriteW6692B(wch, W_B_MODE, wch->b_mode);
		WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
		test_and_clear_bit(FLG_HDLC, &wch->bch.Flags);
		test_and_clear_bit(FLG_TRANSPARENT, &wch->bch.Flags);
		break;
	case ISDN_P_B_RAW:
		wch->b_mode = W_B_MODE_MMS;
		WriteW6692B(wch, W_B_MODE, wch->b_mode);
		WriteW6692B(wch, W_B_EXIM, 0);
		WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_RACT |
			W_B_CMDR_XRST);
		test_and_set_bit(FLG_TRANSPARENT, &wch->bch.Flags);
		break;
	case ISDN_P_B_HDLC:
		wch->b_mode = W_B_MODE_ITF;
		WriteW6692B(wch, W_B_MODE, wch->b_mode);
		WriteW6692B(wch, W_B_ADM1, 0xff);
		WriteW6692B(wch, W_B_ADM2, 0xff);
		WriteW6692B(wch, W_B_EXIM, 0);
		WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_RACT |
			W_B_CMDR_XRST);
		test_and_set_bit(FLG_HDLC, &wch->bch.Flags);
		break;
	default:
		pr_info("%s: protocol %x not known\n", card->name, pr);
		return -ENOPROTOOPT;
	}
	wch->bch.state = pr;
	return 0;
}

static void
send_next(struct w6692_ch *wch)
{
	if (wch->bch.tx_skb && wch->bch.tx_idx < wch->bch.tx_skb->len)
		W6692_fill_Bfifo(wch);
	else {
		if (wch->bch.tx_skb) {
			/* send confirm, on trans, free on hdlc. */
			if (test_bit(FLG_TRANSPARENT, &wch->bch.Flags))
				confirm_Bsend(&wch->bch);
			dev_kfree_skb(wch->bch.tx_skb);
		}
		if (get_next_bframe(&wch->bch))
			W6692_fill_Bfifo(wch);
	}
}

static void
W6692B_interrupt(struct w6692_hw *card, int ch)
{
	struct w6692_ch	*wch = &card->bc[ch];
	int		count;
	u8		stat, star = 0;

	stat = ReadW6692B(wch, W_B_EXIR);
	pr_debug("%s: B%d EXIR %02x\n", card->name, wch->bch.nr, stat);
	if (stat & W_B_EXI_RME) {
		star = ReadW6692B(wch, W_B_STAR);
		if (star & (W_B_STAR_RDOV | W_B_STAR_CRCE | W_B_STAR_RMB)) {
			if ((star & W_B_STAR_RDOV) &&
			    test_bit(FLG_ACTIVE, &wch->bch.Flags)) {
				pr_debug("%s: B%d RDOV proto=%x\n", card->name,
					wch->bch.nr, wch->bch.state);
#ifdef ERROR_STATISTIC
				wch->bch.err_rdo++;
#endif
			}
			if (test_bit(FLG_HDLC, &wch->bch.Flags)) {
				if (star & W_B_STAR_CRCE) {
					pr_debug("%s: B%d CRC error\n",
						card->name, wch->bch.nr);
#ifdef ERROR_STATISTIC
					wch->bch.err_crc++;
#endif
				}
				if (star & W_B_STAR_RMB) {
					pr_debug("%s: B%d message abort\n",
						card->name, wch->bch.nr);
#ifdef ERROR_STATISTIC
					wch->bch.err_inv++;
#endif
				}
			}
			WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RACK |
				W_B_CMDR_RRST | W_B_CMDR_RACT);
			if (wch->bch.rx_skb)
				skb_trim(wch->bch.rx_skb, 0);
		} else {
			count = ReadW6692B(wch, W_B_RBCL) &
				(W_B_FIFO_THRESH - 1);
			if (count == 0)
				count = W_B_FIFO_THRESH;
			W6692_empty_Bfifo(wch, count);
			recv_Bchannel(&wch->bch, 0);
		}
	}
	if (stat & W_B_EXI_RMR) {
		if (!(stat & W_B_EXI_RME))
			star = ReadW6692B(wch, W_B_STAR);
		if (star & W_B_STAR_RDOV) {
			pr_debug("%s: B%d RDOV proto=%x\n", card->name,
				wch->bch.nr, wch->bch.state);
#ifdef ERROR_STATISTIC
			wch->bch.err_rdo++;
#endif
			WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RACK |
				W_B_CMDR_RRST | W_B_CMDR_RACT);
		} else {
			W6692_empty_Bfifo(wch, W_B_FIFO_THRESH);
			if (test_bit(FLG_TRANSPARENT, &wch->bch.Flags) &&
			    wch->bch.rx_skb && (wch->bch.rx_skb->len > 0))
				recv_Bchannel(&wch->bch, 0);
		}
	}
	if (stat & W_B_EXI_RDOV) {
		/* only if it is not handled yet */
		if (!(star & W_B_STAR_RDOV)) {
			pr_debug("%s: B%d RDOV IRQ proto=%x\n", card->name,
				wch->bch.nr, wch->bch.state);
#ifdef ERROR_STATISTIC
			wch->bch.err_rdo++;
#endif
			WriteW6692B(wch, W_B_CMDR, W_B_CMDR_RACK |
				W_B_CMDR_RRST | W_B_CMDR_RACT);
		}
	}
	if (stat & W_B_EXI_XFR) {
		if (!(stat & (W_B_EXI_RME | W_B_EXI_RMR))) {
			star = ReadW6692B(wch, W_B_STAR);
			pr_debug("%s: B%d star %02x\n", card->name,
				wch->bch.nr, star);
		}
		if (star & W_B_STAR_XDOW) {
			pr_debug("%s: B%d XDOW proto=%x\n", card->name,
				wch->bch.nr, wch->bch.state);
#ifdef ERROR_STATISTIC
			wch->bch.err_xdu++;
#endif
			WriteW6692B(wch, W_B_CMDR, W_B_CMDR_XRST |
				W_B_CMDR_RACT);
			/* resend */
			if (wch->bch.tx_skb) {
				if (!test_bit(FLG_TRANSPARENT, &wch->bch.Flags))
					wch->bch.tx_idx = 0;
			}
		}
		send_next(wch);
		if (stat & W_B_EXI_XDUN)
			return; /* handle XDOW only once */
	}
	if (stat & W_B_EXI_XDUN) {
		pr_debug("%s: B%d XDUN proto=%x\n", card->name,
			wch->bch.nr, wch->bch.state);
#ifdef ERROR_STATISTIC
		wch->bch.err_xdu++;
#endif
		WriteW6692B(wch, W_B_CMDR, W_B_CMDR_XRST | W_B_CMDR_RACT);
		/* resend */
		if (wch->bch.tx_skb) {
			if (!test_bit(FLG_TRANSPARENT, &wch->bch.Flags))
				wch->bch.tx_idx = 0;
		}
		send_next(wch);
	}
}

static irqreturn_t
w6692_irq(int intno, void *dev_id)
{
	struct w6692_hw	*card = dev_id;
	u8		ista;

	spin_lock(&card->lock);
	ista = ReadW6692(card, W_ISTA);
	if ((ista | card->imask) == card->imask) {
		/* possible a shared  IRQ reqest */
		spin_unlock(&card->lock);
		return IRQ_NONE;
	}
	card->irqcnt++;
	pr_debug("%s: ista %02x\n", card->name, ista);
	ista &= ~card->imask;
	if (ista & W_INT_B1_EXI)
		W6692B_interrupt(card, 0);
	if (ista & W_INT_B2_EXI)
		W6692B_interrupt(card, 1);
	if (ista & W_INT_D_RME)
		handle_rxD(card);
	if (ista & W_INT_D_RMR)
		W6692_empty_Dfifo(card, W_D_FIFO_THRESH);
	if (ista & W_INT_D_XFR)
		handle_txD(card);
	if (ista & W_INT_D_EXI)
		handle_statusD(card);
	if (ista & (W_INT_XINT0 | W_INT_XINT1)) /* XINT0/1 - never */
		pr_debug("%s: W6692 spurious XINT!\n", card->name);
/* End IRQ Handler */
	spin_unlock(&card->lock);
	return IRQ_HANDLED;
}

static void
dbusy_timer_handler(struct dchannel *dch)
{
	struct w6692_hw	*card = dch->hw;
	int		rbch, star;
	u_long		flags;

	if (test_bit(FLG_BUSY_TIMER, &dch->Flags)) {
		spin_lock_irqsave(&card->lock, flags);
		rbch = ReadW6692(card, W_D_RBCH);
		star = ReadW6692(card, W_D_STAR);
		pr_debug("%s: D-Channel Busy RBCH %02x STAR %02x\n",
			card->name, rbch, star);
		if (star & W_D_STAR_XBZ)	/* D-Channel Busy */
			test_and_set_bit(FLG_L1_BUSY, &dch->Flags);
		else {
			/* discard frame; reset transceiver */
			test_and_clear_bit(FLG_BUSY_TIMER, &dch->Flags);
			if (dch->tx_idx)
				dch->tx_idx = 0;
			else
				pr_info("%s: W6692 D-Channel Busy no tx_idx\n",
					card->name);
			/* Transmitter reset */
			WriteW6692(card, W_D_CMDR, W_D_CMDR_XRST);
		}
		spin_unlock_irqrestore(&card->lock, flags);
	}
}

void initW6692(struct w6692_hw *card)
{
	u8	val;

	card->dch.timer.function = (void *)dbusy_timer_handler;
	card->dch.timer.data = (u_long)&card->dch;
	init_timer(&card->dch.timer);
	w6692_mode(&card->bc[0], ISDN_P_NONE);
	w6692_mode(&card->bc[1], ISDN_P_NONE);
	WriteW6692(card, W_D_CTL, 0x00);
	disable_hwirq(card);
	WriteW6692(card, W_D_SAM, 0xff);
	WriteW6692(card, W_D_TAM, 0xff);
	WriteW6692(card, W_D_MODE, W_D_MODE_RACT);
	card->state = W_L1CMD_RST;
	ph_command(card, W_L1CMD_RST);
	ph_command(card, W_L1CMD_ECK);
	/* enable all IRQ but extern */
	card->imask = 0x18;
	WriteW6692(card, W_D_EXIM, 0x00);
	WriteW6692B(&card->bc[0], W_B_EXIM, 0);
	WriteW6692B(&card->bc[1], W_B_EXIM, 0);
	/* Reset D-chan receiver and transmitter */
	WriteW6692(card, W_D_CMDR, W_D_CMDR_RRST | W_D_CMDR_XRST);
	/* Reset B-chan receiver and transmitter */
	WriteW6692B(&card->bc[0], W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
	WriteW6692B(&card->bc[1], W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
	/* enable peripheral */
	if (card->subtype == W6692_USR) {
		/* seems that USR implemented some power control features
		 * Pin 79 is connected to the oscilator circuit so we
		 * have to handle it here
		 */
		card->pctl = 0x80;
		card->xdata = 0;
		WriteW6692(card, W_PCTL, card->pctl);
		WriteW6692(card, W_XDATA, card->xdata);
	} else {
		card->pctl = W_PCTL_OE5 | W_PCTL_OE4 | W_PCTL_OE2 |
			W_PCTL_OE1 | W_PCTL_OE0;
		card->xaddr = 0x00;/* all sw off */
		if (card->fmask & pots)
			card->xdata |= 0x06;	/*  POWER UP/ LED OFF / ALAW */
		if (card->fmask & led)
			card->xdata |= 0x04;	/* LED OFF */
		if ((card->fmask & pots) || (card->fmask & led)) {
			WriteW6692(card, W_PCTL, card->pctl);
			WriteW6692(card, W_XADDR, card->xaddr);
			WriteW6692(card, W_XDATA, card->xdata);
			val = ReadW6692(card, W_XADDR);
			if (debug & DEBUG_HW)
				pr_notice("%s: W_XADDR=%02x\n",
					card->name, val);
		}
	}
}

static void
reset_w6692(struct w6692_hw *card)
{
	WriteW6692(card, W_D_CTL, W_D_CTL_SRST);
	mdelay(10);
	WriteW6692(card, W_D_CTL, 0);
}

static int
init_card(struct w6692_hw *card)
{
	int	cnt = 3;
	u_long	flags;

	spin_lock_irqsave(&card->lock, flags);
	disable_hwirq(card);
	spin_unlock_irqrestore(&card->lock, flags);
	if (request_irq(card->irq, w6692_irq, IRQF_SHARED, card->name, card)) {
		pr_info("%s: couldn't get interrupt %d\n", card->name,
			card->irq);
		return -EIO;
	}
	while (cnt--) {
		spin_lock_irqsave(&card->lock, flags);
		initW6692(card);
		enable_hwirq(card);
		spin_unlock_irqrestore(&card->lock, flags);
		/* Timeout 10ms */
		msleep_interruptible(10);
		if (debug & DEBUG_HW)
			pr_notice("%s: IRQ %d count %d\n", card->name,
				card->irq, card->irqcnt);
		if (!card->irqcnt) {
			pr_info("%s: IRQ(%d) getting no IRQs during init %d\n",
				card->name, card->irq, 3 - cnt);
			reset_w6692(card);
		} else
			return 0;
	}
	free_irq(card->irq, card);
	return -EIO;
}

static int
w6692_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	struct w6692_ch	*bc = container_of(bch, struct w6692_ch, bch);
	struct w6692_hw *card = bch->hw;
	int ret = -EINVAL;
	struct mISDNhead *hh = mISDN_HEAD_P(skb);
	u32 id;
	u_long flags;

	switch (hh->prim) {
	case PH_DATA_REQ:
		spin_lock_irqsave(&card->lock, flags);
		ret = bchannel_senddata(bch, skb);
		if (ret > 0) { /* direct TX */
			id = hh->id; /* skb can be freed */
			ret = 0;
			W6692_fill_Bfifo(bc);
			spin_unlock_irqrestore(&card->lock, flags);
			if (!test_bit(FLG_TRANSPARENT, &bch->Flags))
				queue_ch_frame(ch, PH_DATA_CNF, id, NULL);
		} else
			spin_unlock_irqrestore(&card->lock, flags);
		return ret;
	case PH_ACTIVATE_REQ:
		spin_lock_irqsave(&card->lock, flags);
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags))
			ret = w6692_mode(bc, ch->protocol);
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
		w6692_mode(bc, ISDN_P_NONE);
		spin_unlock_irqrestore(&card->lock, flags);
		_queue_data(ch, PH_DEACTIVATE_IND, MISDN_ID_ANY, 0,
			NULL, GFP_KERNEL);
		ret = 0;
		break;
	default:
		pr_info("%s: %s unknown prim(%x,%x)\n",
			card->name, __func__, hh->prim, hh->id);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

static int
channel_bctrl(struct bchannel *bch, struct mISDN_ctrl_req *cq)
{
	int	ret = 0;

	switch (cq->op) {
	case MISDN_CTRL_GETOP:
		cq->op = 0;
		break;
	/* Nothing implemented yet */
	case MISDN_CTRL_FILL_EMPTY:
	default:
		pr_info("%s: unknown Op %x\n", __func__, cq->op);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int
open_bchannel(struct w6692_hw *card, struct channel_req *rq)
{
	struct bchannel *bch;

	if (rq->adr.channel > 2)
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

static int
channel_ctrl(struct w6692_hw *card, struct mISDN_ctrl_req *cq)
{
	int	ret = 0;

	switch (cq->op) {
	case MISDN_CTRL_GETOP:
		cq->op = 0;
		break;
	default:
		pr_info("%s: unknown CTRL OP %x\n", card->name, cq->op);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int
w6692_bctrl(struct mISDNchannel *ch, u32 cmd, void *arg)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	struct w6692_ch *bc = container_of(bch, struct w6692_ch, bch);
	struct w6692_hw *card = bch->hw;
	int ret = -EINVAL;
	u_long flags;

	pr_debug("%s: %s cmd:%x %p\n", card->name, __func__, cmd, arg);
	switch (cmd) {
	case CLOSE_CHANNEL:
		test_and_clear_bit(FLG_OPEN, &bch->Flags);
		if (test_bit(FLG_ACTIVE, &bch->Flags)) {
			spin_lock_irqsave(&card->lock, flags);
			mISDN_freebchannel(bch);
			w6692_mode(bc, ISDN_P_NONE);
			spin_unlock_irqrestore(&card->lock, flags);
		} else {
			skb_queue_purge(&bch->rqueue);
			bch->rcount = 0;
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
		pr_info("%s: %s unknown prim(%x)\n",
			card->name, __func__, cmd);
	}
	return ret;
}

static int
w6692_l2l1D(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct w6692_hw		*card = container_of(dch, struct w6692_hw, dch);
	int			ret = -EINVAL;
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	u32			id;
	u_long			flags;

	switch (hh->prim) {
	case PH_DATA_REQ:
		spin_lock_irqsave(&card->lock, flags);
		ret = dchannel_senddata(dch, skb);
		if (ret > 0) { /* direct TX */
			id = hh->id; /* skb can be freed */
			W6692_fill_Dfifo(card);
			ret = 0;
			spin_unlock_irqrestore(&card->lock, flags);
			queue_ch_frame(ch, PH_DATA_CNF, id, NULL);
		} else
			spin_unlock_irqrestore(&card->lock, flags);
		return ret;
	case PH_ACTIVATE_REQ:
		ret = l1_event(dch->l1, hh->prim);
		break;
	case PH_DEACTIVATE_REQ:
		test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);
		ret = l1_event(dch->l1, hh->prim);
		break;
	}

	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

static int
w6692_l1callback(struct dchannel *dch, u32 cmd)
{
	struct w6692_hw *card = container_of(dch, struct w6692_hw, dch);
	u_long flags;

	pr_debug("%s: cmd(%x) state(%02x)\n", card->name, cmd, card->state);
	switch (cmd) {
	case INFO3_P8:
		spin_lock_irqsave(&card->lock, flags);
		ph_command(card, W_L1CMD_AR8);
		spin_unlock_irqrestore(&card->lock, flags);
		break;
	case INFO3_P10:
		spin_lock_irqsave(&card->lock, flags);
		ph_command(card, W_L1CMD_AR10);
		spin_unlock_irqrestore(&card->lock, flags);
		break;
	case HW_RESET_REQ:
		spin_lock_irqsave(&card->lock, flags);
		if (card->state != W_L1IND_DRD)
			ph_command(card, W_L1CMD_RST);
		ph_command(card, W_L1CMD_ECK);
		spin_unlock_irqrestore(&card->lock, flags);
		break;
	case HW_DEACT_REQ:
		skb_queue_purge(&dch->squeue);
		if (dch->tx_skb) {
			dev_kfree_skb(dch->tx_skb);
			dch->tx_skb = NULL;
		}
		dch->tx_idx = 0;
		if (dch->rx_skb) {
			dev_kfree_skb(dch->rx_skb);
			dch->rx_skb = NULL;
		}
		test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
		if (test_and_clear_bit(FLG_BUSY_TIMER, &dch->Flags))
			del_timer(&dch->timer);
		break;
	case HW_POWERUP_REQ:
		spin_lock_irqsave(&card->lock, flags);
		ph_command(card, W_L1CMD_ECK);
		spin_unlock_irqrestore(&card->lock, flags);
		break;
	case PH_ACTIVATE_IND:
		test_and_set_bit(FLG_ACTIVE, &dch->Flags);
		_queue_data(&dch->dev.D, cmd, MISDN_ID_ANY, 0, NULL,
			GFP_ATOMIC);
		break;
	case PH_DEACTIVATE_IND:
		test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
		_queue_data(&dch->dev.D, cmd, MISDN_ID_ANY, 0, NULL,
			GFP_ATOMIC);
		break;
	default:
		pr_debug("%s: %s unknown command %x\n", card->name,
			__func__, cmd);
		return -1;
	}
	return 0;
}

static int
open_dchannel(struct w6692_hw *card, struct channel_req *rq)
{
	pr_debug("%s: %s dev(%d) open from %p\n", card->name, __func__,
		card->dch.dev.id, __builtin_return_address(1));
	if (rq->protocol != ISDN_P_TE_S0)
		return -EINVAL;
	if (rq->adr.channel == 1)
		/* E-Channel not supported */
		return -EINVAL;
	rq->ch = &card->dch.dev.D;
	rq->ch->protocol = rq->protocol;
	if (card->dch.state == 7)
		_queue_data(rq->ch, PH_ACTIVATE_IND, MISDN_ID_ANY,
		    0, NULL, GFP_KERNEL);
	return 0;
}

static int
w6692_dctrl(struct mISDNchannel *ch, u32 cmd, void *arg)
{
	struct mISDNdevice *dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel *dch = container_of(dev, struct dchannel, dev);
	struct w6692_hw *card = container_of(dch, struct w6692_hw, dch);
	struct channel_req *rq;
	int err = 0;

	pr_debug("%s: DCTRL: %x %p\n", card->name, cmd, arg);
	switch (cmd) {
	case OPEN_CHANNEL:
		rq = arg;
		if (rq->protocol == ISDN_P_TE_S0)
			err = open_dchannel(card, rq);
		else
			err = open_bchannel(card, rq);
		if (err)
			break;
		if (!try_module_get(THIS_MODULE))
			pr_info("%s: cannot get module\n", card->name);
		break;
	case CLOSE_CHANNEL:
		pr_debug("%s: dev(%d) close from %p\n", card->name,
			dch->dev.id, __builtin_return_address(0));
		module_put(THIS_MODULE);
		break;
	case CONTROL_CHANNEL:
		err = channel_ctrl(card, arg);
		break;
	default:
		pr_debug("%s: unknown DCTRL command %x\n", card->name, cmd);
		return -EINVAL;
	}
	return err;
}

static int
setup_w6692(struct w6692_hw *card)
{
	u32	val;

	if (!request_region(card->addr, 256, card->name)) {
		pr_info("%s: config port %x-%x already in use\n", card->name,
		       card->addr, card->addr + 255);
		return -EIO;
	}
	W6692Version(card);
	card->bc[0].addr = card->addr;
	card->bc[1].addr = card->addr + 0x40;
	val = ReadW6692(card, W_ISTA);
	if (debug & DEBUG_HW)
		pr_notice("%s ISTA=%02x\n", card->name, val);
	val = ReadW6692(card, W_IMASK);
	if (debug & DEBUG_HW)
		pr_notice("%s IMASK=%02x\n", card->name, val);
	val = ReadW6692(card, W_D_EXIR);
	if (debug & DEBUG_HW)
		pr_notice("%s D_EXIR=%02x\n", card->name, val);
	val = ReadW6692(card, W_D_EXIM);
	if (debug & DEBUG_HW)
		pr_notice("%s D_EXIM=%02x\n", card->name, val);
	val = ReadW6692(card, W_D_RSTA);
	if (debug & DEBUG_HW)
		pr_notice("%s D_RSTA=%02x\n", card->name, val);
	return 0;
}

static void
release_card(struct w6692_hw *card)
{
	u_long	flags;

	spin_lock_irqsave(&card->lock, flags);
	disable_hwirq(card);
	w6692_mode(&card->bc[0], ISDN_P_NONE);
	w6692_mode(&card->bc[1], ISDN_P_NONE);
	if ((card->fmask & led) || card->subtype == W6692_USR) {
		card->xdata |= 0x04;	/*  LED OFF */
		WriteW6692(card, W_XDATA, card->xdata);
	}
	spin_unlock_irqrestore(&card->lock, flags);
	free_irq(card->irq, card);
	l1_event(card->dch.l1, CLOSE_CHANNEL);
	mISDN_unregister_device(&card->dch.dev);
	release_region(card->addr, 256);
	mISDN_freebchannel(&card->bc[1].bch);
	mISDN_freebchannel(&card->bc[0].bch);
	mISDN_freedchannel(&card->dch);
	write_lock_irqsave(&card_lock, flags);
	list_del(&card->list);
	write_unlock_irqrestore(&card_lock, flags);
	pci_disable_device(card->pdev);
	pci_set_drvdata(card->pdev, NULL);
	kfree(card);
}

static int
setup_instance(struct w6692_hw *card)
{
	int		i, err;
	u_long		flags;

	snprintf(card->name, MISDN_MAX_IDLEN - 1, "w6692.%d", w6692_cnt + 1);
	write_lock_irqsave(&card_lock, flags);
	list_add_tail(&card->list, &Cards);
	write_unlock_irqrestore(&card_lock, flags);
	card->fmask = (1 << w6692_cnt);
	_set_debug(card);
	spin_lock_init(&card->lock);
	mISDN_initdchannel(&card->dch, MAX_DFRAME_LEN_L1, W6692_ph_bh);
	card->dch.dev.Dprotocols = (1 << ISDN_P_TE_S0);
	card->dch.dev.D.send = w6692_l2l1D;
	card->dch.dev.D.ctrl = w6692_dctrl;
	card->dch.dev.Bprotocols = (1 << (ISDN_P_B_RAW & ISDN_P_B_MASK)) |
		(1 << (ISDN_P_B_HDLC & ISDN_P_B_MASK));
	card->dch.hw = card;
	card->dch.dev.nrbchan = 2;
	for (i = 0; i < 2; i++) {
		mISDN_initbchannel(&card->bc[i].bch, MAX_DATA_MEM);
		card->bc[i].bch.hw = card;
		card->bc[i].bch.nr = i + 1;
		card->bc[i].bch.ch.nr = i + 1;
		card->bc[i].bch.ch.send = w6692_l2l1B;
		card->bc[i].bch.ch.ctrl = w6692_bctrl;
		set_channelmap(i + 1, card->dch.dev.channelmap);
		list_add(&card->bc[i].bch.ch.list, &card->dch.dev.bchannels);
	}
	err = setup_w6692(card);
	if (err)
		goto error_setup;
	err = mISDN_register_device(&card->dch.dev, &card->pdev->dev,
		card->name);
	if (err)
		goto error_reg;
	err = init_card(card);
	if (err)
		goto error_init;
	err = create_l1(&card->dch, w6692_l1callback);
	if (!err) {
		w6692_cnt++;
		pr_notice("W6692 %d cards installed\n", w6692_cnt);
		return 0;
	}

	free_irq(card->irq, card);
error_init:
	mISDN_unregister_device(&card->dch.dev);
error_reg:
	release_region(card->addr, 256);
error_setup:
	mISDN_freebchannel(&card->bc[1].bch);
	mISDN_freebchannel(&card->bc[0].bch);
	mISDN_freedchannel(&card->dch);
	write_lock_irqsave(&card_lock, flags);
	list_del(&card->list);
	write_unlock_irqrestore(&card_lock, flags);
	kfree(card);
	return err;
}

static int __devinit
w6692_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int		err = -ENOMEM;
	struct w6692_hw	*card;
	struct w6692map	*m = (struct w6692map *)ent->driver_data;

	card = kzalloc(sizeof(struct w6692_hw), GFP_KERNEL);
	if (!card) {
		pr_info("No kmem for w6692 card\n");
		return err;
	}
	card->pdev = pdev;
	card->subtype = m->subtype;
	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return err;
	}

	printk(KERN_INFO "mISDN_w6692: found adapter %s at %s\n",
	       m->name, pci_name(pdev));

	card->addr = pci_resource_start(pdev, 1);
	card->irq = pdev->irq;
	pci_set_drvdata(pdev, card);
	err = setup_instance(card);
	if (err)
		pci_set_drvdata(pdev, NULL);
	return err;
}

static void __devexit
w6692_remove_pci(struct pci_dev *pdev)
{
	struct w6692_hw	*card = pci_get_drvdata(pdev);

	if (card)
		release_card(card);
	else
		if (debug)
			pr_notice("%s: drvdata already removed\n", __func__);
}

static struct pci_device_id w6692_ids[] = {
	{ PCI_VENDOR_ID_DYNALINK, PCI_DEVICE_ID_DYNALINK_IS64PH,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, (ulong)&w6692_map[0]},
	{ PCI_VENDOR_ID_WINBOND2, PCI_DEVICE_ID_WINBOND2_6692,
	  PCI_VENDOR_ID_USR, PCI_DEVICE_ID_USR_6692, 0, 0,
	  (ulong)&w6692_map[2]},
	{ PCI_VENDOR_ID_WINBOND2, PCI_DEVICE_ID_WINBOND2_6692,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, (ulong)&w6692_map[1]},
	{ }
};
MODULE_DEVICE_TABLE(pci, w6692_ids);

static struct pci_driver w6692_driver = {
	.name =  "w6692",
	.probe = w6692_probe,
	.remove = __devexit_p(w6692_remove_pci),
	.id_table = w6692_ids,
};

static int __init w6692_init(void)
{
	int err;

	pr_notice("Winbond W6692 PCI driver Rev. %s\n", W6692_REV);

	err = pci_register_driver(&w6692_driver);
	return err;
}

static void __exit w6692_cleanup(void)
{
	pci_unregister_driver(&w6692_driver);
}

module_init(w6692_init);
module_exit(w6692_cleanup);
