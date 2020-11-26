// SPDX-License-Identifier: GPL-2.0-only
/*
 * mISDNisar.c   ISAR (Siemens PSB 7110) specific functions
 *
 * Author Karsten Keil (keil@isdn4linux.de)
 *
 * Copyright 2009  by Karsten Keil <keil@isdn4linux.de>
 */

/* define this to enable static debug messages, if you kernel supports
 * dynamic debugging, you should use debugfs for this
 */
/* #define DEBUG */

#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/mISDNhw.h>
#include <linux/module.h>
#include "isar.h"

#define ISAR_REV	"2.1"

MODULE_AUTHOR("Karsten Keil");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(ISAR_REV);

#define DEBUG_HW_FIRMWARE_FIFO	0x10000

static const u8 faxmodulation[] = {3, 24, 48, 72, 73, 74, 96, 97, 98, 121,
				   122, 145, 146};
#define FAXMODCNT 13

static void isar_setup(struct isar_hw *);

static inline int
waitforHIA(struct isar_hw *isar, int timeout)
{
	int t = timeout;
	u8 val = isar->read_reg(isar->hw, ISAR_HIA);

	while ((val & 1) && t) {
		udelay(1);
		t--;
		val = isar->read_reg(isar->hw, ISAR_HIA);
	}
	pr_debug("%s: HIA after %dus\n", isar->name, timeout - t);
	return timeout;
}

/*
 * send msg to ISAR mailbox
 * if msg is NULL use isar->buf
 */
static int
send_mbox(struct isar_hw *isar, u8 his, u8 creg, u8 len, u8 *msg)
{
	if (!waitforHIA(isar, 1000))
		return 0;
	pr_debug("send_mbox(%02x,%02x,%d)\n", his, creg, len);
	isar->write_reg(isar->hw, ISAR_CTRL_H, creg);
	isar->write_reg(isar->hw, ISAR_CTRL_L, len);
	isar->write_reg(isar->hw, ISAR_WADR, 0);
	if (!msg)
		msg = isar->buf;
	if (msg && len) {
		isar->write_fifo(isar->hw, ISAR_MBOX, msg, len);
		if (isar->ch[0].bch.debug & DEBUG_HW_BFIFO) {
			int l = 0;

			while (l < (int)len) {
				hex_dump_to_buffer(msg + l, len - l, 32, 1,
						   isar->log, 256, 1);
				pr_debug("%s: %s %02x: %s\n", isar->name,
					 __func__, l, isar->log);
				l += 32;
			}
		}
	}
	isar->write_reg(isar->hw, ISAR_HIS, his);
	waitforHIA(isar, 1000);
	return 1;
}

/*
 * receive message from ISAR mailbox
 * if msg is NULL use isar->buf
 */
static void
rcv_mbox(struct isar_hw *isar, u8 *msg)
{
	if (!msg)
		msg = isar->buf;
	isar->write_reg(isar->hw, ISAR_RADR, 0);
	if (msg && isar->clsb) {
		isar->read_fifo(isar->hw, ISAR_MBOX, msg, isar->clsb);
		if (isar->ch[0].bch.debug & DEBUG_HW_BFIFO) {
			int l = 0;

			while (l < (int)isar->clsb) {
				hex_dump_to_buffer(msg + l, isar->clsb - l, 32,
						   1, isar->log, 256, 1);
				pr_debug("%s: %s %02x: %s\n", isar->name,
					 __func__, l, isar->log);
				l += 32;
			}
		}
	}
	isar->write_reg(isar->hw, ISAR_IIA, 0);
}

static inline void
get_irq_infos(struct isar_hw *isar)
{
	isar->iis = isar->read_reg(isar->hw, ISAR_IIS);
	isar->cmsb = isar->read_reg(isar->hw, ISAR_CTRL_H);
	isar->clsb = isar->read_reg(isar->hw, ISAR_CTRL_L);
	pr_debug("%s: rcv_mbox(%02x,%02x,%d)\n", isar->name,
		 isar->iis, isar->cmsb, isar->clsb);
}

/*
 * poll answer message from ISAR mailbox
 * should be used only with ISAR IRQs disabled before DSP was started
 *
 */
static int
poll_mbox(struct isar_hw *isar, int maxdelay)
{
	int t = maxdelay;
	u8 irq;

	irq = isar->read_reg(isar->hw, ISAR_IRQBIT);
	while (t && !(irq & ISAR_IRQSTA)) {
		udelay(1);
		t--;
	}
	if (t)	{
		get_irq_infos(isar);
		rcv_mbox(isar, NULL);
	}
	pr_debug("%s: pulled %d bytes after %d us\n",
		 isar->name, isar->clsb, maxdelay - t);
	return t;
}

static int
ISARVersion(struct isar_hw *isar)
{
	int ver;

	/* disable ISAR IRQ */
	isar->write_reg(isar->hw, ISAR_IRQBIT, 0);
	isar->buf[0] = ISAR_MSG_HWVER;
	isar->buf[1] = 0;
	isar->buf[2] = 1;
	if (!send_mbox(isar, ISAR_HIS_VNR, 0, 3, NULL))
		return -1;
	if (!poll_mbox(isar, 1000))
		return -2;
	if (isar->iis == ISAR_IIS_VNR) {
		if (isar->clsb == 1) {
			ver = isar->buf[0] & 0xf;
			return ver;
		}
		return -3;
	}
	return -4;
}

static int
load_firmware(struct isar_hw *isar, const u8 *buf, int size)
{
	u32	saved_debug = isar->ch[0].bch.debug;
	int	ret, cnt;
	u8	nom, noc;
	u16	left, val, *sp = (u16 *)buf;
	u8	*mp;
	u_long	flags;

	struct {
		u16 sadr;
		u16 len;
		u16 d_key;
	} blk_head;

	if (1 != isar->version) {
		pr_err("%s: ISAR wrong version %d firmware download aborted\n",
		       isar->name, isar->version);
		return -EINVAL;
	}
	if (!(saved_debug & DEBUG_HW_FIRMWARE_FIFO))
		isar->ch[0].bch.debug &= ~DEBUG_HW_BFIFO;
	pr_debug("%s: load firmware %d words (%d bytes)\n",
		 isar->name, size / 2, size);
	cnt = 0;
	size /= 2;
	/* disable ISAR IRQ */
	spin_lock_irqsave(isar->hwlock, flags);
	isar->write_reg(isar->hw, ISAR_IRQBIT, 0);
	spin_unlock_irqrestore(isar->hwlock, flags);
	while (cnt < size) {
		blk_head.sadr = le16_to_cpu(*sp++);
		blk_head.len = le16_to_cpu(*sp++);
		blk_head.d_key = le16_to_cpu(*sp++);
		cnt += 3;
		pr_debug("ISAR firmware block (%#x,%d,%#x)\n",
			 blk_head.sadr, blk_head.len, blk_head.d_key & 0xff);
		left = blk_head.len;
		if (cnt + left > size) {
			pr_info("%s: firmware error have %d need %d words\n",
				isar->name, size, cnt + left);
			ret = -EINVAL;
			goto reterrflg;
		}
		spin_lock_irqsave(isar->hwlock, flags);
		if (!send_mbox(isar, ISAR_HIS_DKEY, blk_head.d_key & 0xff,
			       0, NULL)) {
			pr_info("ISAR send_mbox dkey failed\n");
			ret = -ETIME;
			goto reterror;
		}
		if (!poll_mbox(isar, 1000)) {
			pr_warn("ISAR poll_mbox dkey failed\n");
			ret = -ETIME;
			goto reterror;
		}
		spin_unlock_irqrestore(isar->hwlock, flags);
		if ((isar->iis != ISAR_IIS_DKEY) || isar->cmsb || isar->clsb) {
			pr_info("ISAR wrong dkey response (%x,%x,%x)\n",
				isar->iis, isar->cmsb, isar->clsb);
			ret = 1;
			goto reterrflg;
		}
		while (left > 0) {
			if (left > 126)
				noc = 126;
			else
				noc = left;
			nom = (2 * noc) + 3;
			mp  = isar->buf;
			/* the ISAR is big endian */
			*mp++ = blk_head.sadr >> 8;
			*mp++ = blk_head.sadr & 0xFF;
			left -= noc;
			cnt += noc;
			*mp++ = noc;
			pr_debug("%s: load %3d words at %04x\n", isar->name,
				 noc, blk_head.sadr);
			blk_head.sadr += noc;
			while (noc) {
				val = le16_to_cpu(*sp++);
				*mp++ = val >> 8;
				*mp++ = val & 0xFF;
				noc--;
			}
			spin_lock_irqsave(isar->hwlock, flags);
			if (!send_mbox(isar, ISAR_HIS_FIRM, 0, nom, NULL)) {
				pr_info("ISAR send_mbox prog failed\n");
				ret = -ETIME;
				goto reterror;
			}
			if (!poll_mbox(isar, 1000)) {
				pr_info("ISAR poll_mbox prog failed\n");
				ret = -ETIME;
				goto reterror;
			}
			spin_unlock_irqrestore(isar->hwlock, flags);
			if ((isar->iis != ISAR_IIS_FIRM) ||
			    isar->cmsb || isar->clsb) {
				pr_info("ISAR wrong prog response (%x,%x,%x)\n",
					isar->iis, isar->cmsb, isar->clsb);
				ret = -EIO;
				goto reterrflg;
			}
		}
		pr_debug("%s: ISAR firmware block %d words loaded\n",
			 isar->name, blk_head.len);
	}
	isar->ch[0].bch.debug = saved_debug;
	/* 10ms delay */
	cnt = 10;
	while (cnt--)
		mdelay(1);
	isar->buf[0] = 0xff;
	isar->buf[1] = 0xfe;
	isar->bstat = 0;
	spin_lock_irqsave(isar->hwlock, flags);
	if (!send_mbox(isar, ISAR_HIS_STDSP, 0, 2, NULL)) {
		pr_info("ISAR send_mbox start dsp failed\n");
		ret = -ETIME;
		goto reterror;
	}
	if (!poll_mbox(isar, 1000)) {
		pr_info("ISAR poll_mbox start dsp failed\n");
		ret = -ETIME;
		goto reterror;
	}
	if ((isar->iis != ISAR_IIS_STDSP) || isar->cmsb || isar->clsb) {
		pr_info("ISAR wrong start dsp response (%x,%x,%x)\n",
			isar->iis, isar->cmsb, isar->clsb);
		ret = -EIO;
		goto reterror;
	} else
		pr_debug("%s: ISAR start dsp success\n", isar->name);

	/* NORMAL mode entered */
	/* Enable IRQs of ISAR */
	isar->write_reg(isar->hw, ISAR_IRQBIT, ISAR_IRQSTA);
	spin_unlock_irqrestore(isar->hwlock, flags);
	cnt = 1000; /* max 1s */
	while ((!isar->bstat) && cnt) {
		mdelay(1);
		cnt--;
	}
	if (!cnt) {
		pr_info("ISAR no general status event received\n");
		ret = -ETIME;
		goto reterrflg;
	} else
		pr_debug("%s: ISAR general status event %x\n",
			 isar->name, isar->bstat);
	/* 10ms delay */
	cnt = 10;
	while (cnt--)
		mdelay(1);
	isar->iis = 0;
	spin_lock_irqsave(isar->hwlock, flags);
	if (!send_mbox(isar, ISAR_HIS_DIAG, ISAR_CTRL_STST, 0, NULL)) {
		pr_info("ISAR send_mbox self tst failed\n");
		ret = -ETIME;
		goto reterror;
	}
	spin_unlock_irqrestore(isar->hwlock, flags);
	cnt = 10000; /* max 100 ms */
	while ((isar->iis != ISAR_IIS_DIAG) && cnt) {
		udelay(10);
		cnt--;
	}
	mdelay(1);
	if (!cnt) {
		pr_info("ISAR no self tst response\n");
		ret = -ETIME;
		goto reterrflg;
	}
	if ((isar->cmsb == ISAR_CTRL_STST) && (isar->clsb == 1)
	    && (isar->buf[0] == 0))
		pr_debug("%s: ISAR selftest OK\n", isar->name);
	else {
		pr_info("ISAR selftest not OK %x/%x/%x\n",
			isar->cmsb, isar->clsb, isar->buf[0]);
		ret = -EIO;
		goto reterrflg;
	}
	spin_lock_irqsave(isar->hwlock, flags);
	isar->iis = 0;
	if (!send_mbox(isar, ISAR_HIS_DIAG, ISAR_CTRL_SWVER, 0, NULL)) {
		pr_info("ISAR RQST SVN failed\n");
		ret = -ETIME;
		goto reterror;
	}
	spin_unlock_irqrestore(isar->hwlock, flags);
	cnt = 30000; /* max 300 ms */
	while ((isar->iis != ISAR_IIS_DIAG) && cnt) {
		udelay(10);
		cnt--;
	}
	mdelay(1);
	if (!cnt) {
		pr_info("ISAR no SVN response\n");
		ret = -ETIME;
		goto reterrflg;
	} else {
		if ((isar->cmsb == ISAR_CTRL_SWVER) && (isar->clsb == 1)) {
			pr_notice("%s: ISAR software version %#x\n",
				  isar->name, isar->buf[0]);
		} else {
			pr_info("%s: ISAR wrong swver response (%x,%x)"
				" cnt(%d)\n", isar->name, isar->cmsb,
				isar->clsb, cnt);
			ret = -EIO;
			goto reterrflg;
		}
	}
	spin_lock_irqsave(isar->hwlock, flags);
	isar_setup(isar);
	spin_unlock_irqrestore(isar->hwlock, flags);
	ret = 0;
reterrflg:
	spin_lock_irqsave(isar->hwlock, flags);
reterror:
	isar->ch[0].bch.debug = saved_debug;
	if (ret)
		/* disable ISAR IRQ */
		isar->write_reg(isar->hw, ISAR_IRQBIT, 0);
	spin_unlock_irqrestore(isar->hwlock, flags);
	return ret;
}

static inline void
deliver_status(struct isar_ch *ch, int status)
{
	pr_debug("%s: HL->LL FAXIND %x\n", ch->is->name, status);
	_queue_data(&ch->bch.ch, PH_CONTROL_IND, status, 0, NULL, GFP_ATOMIC);
}

static inline void
isar_rcv_frame(struct isar_ch *ch)
{
	u8	*ptr;
	int	maxlen;

	if (!ch->is->clsb) {
		pr_debug("%s; ISAR zero len frame\n", ch->is->name);
		ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
		return;
	}
	if (test_bit(FLG_RX_OFF, &ch->bch.Flags)) {
		ch->bch.dropcnt += ch->is->clsb;
		ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
		return;
	}
	switch (ch->bch.state) {
	case ISDN_P_NONE:
		pr_debug("%s: ISAR protocol 0 spurious IIS_RDATA %x/%x/%x\n",
			 ch->is->name, ch->is->iis, ch->is->cmsb, ch->is->clsb);
		ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
		break;
	case ISDN_P_B_RAW:
	case ISDN_P_B_L2DTMF:
	case ISDN_P_B_MODEM_ASYNC:
		maxlen = bchannel_get_rxbuf(&ch->bch, ch->is->clsb);
		if (maxlen < 0) {
			pr_warn("%s.B%d: No bufferspace for %d bytes\n",
				ch->is->name, ch->bch.nr, ch->is->clsb);
			ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
			break;
		}
		rcv_mbox(ch->is, skb_put(ch->bch.rx_skb, ch->is->clsb));
		recv_Bchannel(&ch->bch, 0, false);
		break;
	case ISDN_P_B_HDLC:
		maxlen = bchannel_get_rxbuf(&ch->bch, ch->is->clsb);
		if (maxlen < 0) {
			pr_warn("%s.B%d: No bufferspace for %d bytes\n",
				ch->is->name, ch->bch.nr, ch->is->clsb);
			ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
			break;
		}
		if (ch->is->cmsb & HDLC_ERROR) {
			pr_debug("%s: ISAR frame error %x len %d\n",
				 ch->is->name, ch->is->cmsb, ch->is->clsb);
#ifdef ERROR_STATISTIC
			if (ch->is->cmsb & HDLC_ERR_RER)
				ch->bch.err_inv++;
			if (ch->is->cmsb & HDLC_ERR_CER)
				ch->bch.err_crc++;
#endif
			skb_trim(ch->bch.rx_skb, 0);
			ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
			break;
		}
		if (ch->is->cmsb & HDLC_FSD)
			skb_trim(ch->bch.rx_skb, 0);
		ptr = skb_put(ch->bch.rx_skb, ch->is->clsb);
		rcv_mbox(ch->is, ptr);
		if (ch->is->cmsb & HDLC_FED) {
			if (ch->bch.rx_skb->len < 3) { /* last 2 are the FCS */
				pr_debug("%s: ISAR frame to short %d\n",
					 ch->is->name, ch->bch.rx_skb->len);
				skb_trim(ch->bch.rx_skb, 0);
				break;
			}
			skb_trim(ch->bch.rx_skb, ch->bch.rx_skb->len - 2);
			recv_Bchannel(&ch->bch, 0, false);
		}
		break;
	case ISDN_P_B_T30_FAX:
		if (ch->state != STFAX_ACTIV) {
			pr_debug("%s: isar_rcv_frame: not ACTIV\n",
				 ch->is->name);
			ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
			if (ch->bch.rx_skb)
				skb_trim(ch->bch.rx_skb, 0);
			break;
		}
		if (!ch->bch.rx_skb) {
			ch->bch.rx_skb = mI_alloc_skb(ch->bch.maxlen,
						      GFP_ATOMIC);
			if (unlikely(!ch->bch.rx_skb)) {
				pr_info("%s: B receive out of memory\n",
					__func__);
				ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
				break;
			}
		}
		if (ch->cmd == PCTRL_CMD_FRM) {
			rcv_mbox(ch->is, skb_put(ch->bch.rx_skb, ch->is->clsb));
			pr_debug("%s: isar_rcv_frame: %d\n",
				 ch->is->name, ch->bch.rx_skb->len);
			if (ch->is->cmsb & SART_NMD) { /* ABORT */
				pr_debug("%s: isar_rcv_frame: no more data\n",
					 ch->is->name);
				ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
				send_mbox(ch->is, SET_DPS(ch->dpath) |
					  ISAR_HIS_PUMPCTRL, PCTRL_CMD_ESC,
					  0, NULL);
				ch->state = STFAX_ESCAPE;
				/* set_skb_flag(skb, DF_NOMOREDATA); */
			}
			recv_Bchannel(&ch->bch, 0, false);
			if (ch->is->cmsb & SART_NMD)
				deliver_status(ch, HW_MOD_NOCARR);
			break;
		}
		if (ch->cmd != PCTRL_CMD_FRH) {
			pr_debug("%s: isar_rcv_frame: unknown fax mode %x\n",
				 ch->is->name, ch->cmd);
			ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
			if (ch->bch.rx_skb)
				skb_trim(ch->bch.rx_skb, 0);
			break;
		}
		/* PCTRL_CMD_FRH */
		if ((ch->bch.rx_skb->len + ch->is->clsb) >
		    (ch->bch.maxlen + 2)) {
			pr_info("%s: %s incoming packet too large\n",
				ch->is->name, __func__);
			ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
			skb_trim(ch->bch.rx_skb, 0);
			break;
		}  else if (ch->is->cmsb & HDLC_ERROR) {
			pr_info("%s: ISAR frame error %x len %d\n",
				ch->is->name, ch->is->cmsb, ch->is->clsb);
			skb_trim(ch->bch.rx_skb, 0);
			ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
			break;
		}
		if (ch->is->cmsb & HDLC_FSD)
			skb_trim(ch->bch.rx_skb, 0);
		ptr = skb_put(ch->bch.rx_skb, ch->is->clsb);
		rcv_mbox(ch->is, ptr);
		if (ch->is->cmsb & HDLC_FED) {
			if (ch->bch.rx_skb->len < 3) { /* last 2 are the FCS */
				pr_info("%s: ISAR frame to short %d\n",
					ch->is->name, ch->bch.rx_skb->len);
				skb_trim(ch->bch.rx_skb, 0);
				break;
			}
			skb_trim(ch->bch.rx_skb, ch->bch.rx_skb->len - 2);
			recv_Bchannel(&ch->bch, 0, false);
		}
		if (ch->is->cmsb & SART_NMD) { /* ABORT */
			pr_debug("%s: isar_rcv_frame: no more data\n",
				 ch->is->name);
			ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
			if (ch->bch.rx_skb)
				skb_trim(ch->bch.rx_skb, 0);
			send_mbox(ch->is, SET_DPS(ch->dpath) |
				  ISAR_HIS_PUMPCTRL, PCTRL_CMD_ESC, 0, NULL);
			ch->state = STFAX_ESCAPE;
			deliver_status(ch, HW_MOD_NOCARR);
		}
		break;
	default:
		pr_info("isar_rcv_frame protocol (%x)error\n", ch->bch.state);
		ch->is->write_reg(ch->is->hw, ISAR_IIA, 0);
		break;
	}
}

static void
isar_fill_fifo(struct isar_ch *ch)
{
	int count;
	u8 msb;
	u8 *ptr;

	pr_debug("%s: ch%d  tx_skb %d tx_idx %d\n", ch->is->name, ch->bch.nr,
		 ch->bch.tx_skb ? ch->bch.tx_skb->len : -1, ch->bch.tx_idx);
	if (!(ch->is->bstat &
	      (ch->dpath == 1 ? BSTAT_RDM1 : BSTAT_RDM2)))
		return;
	if (!ch->bch.tx_skb) {
		if (!test_bit(FLG_TX_EMPTY, &ch->bch.Flags) ||
		    (ch->bch.state != ISDN_P_B_RAW))
			return;
		count = ch->mml;
		/* use the card buffer */
		memset(ch->is->buf, ch->bch.fill[0], count);
		send_mbox(ch->is, SET_DPS(ch->dpath) | ISAR_HIS_SDATA,
			  0, count, ch->is->buf);
		return;
	}
	count = ch->bch.tx_skb->len - ch->bch.tx_idx;
	if (count <= 0)
		return;
	if (count > ch->mml) {
		msb = 0;
		count = ch->mml;
	} else {
		msb = HDLC_FED;
	}
	ptr = ch->bch.tx_skb->data + ch->bch.tx_idx;
	if (!ch->bch.tx_idx) {
		pr_debug("%s: frame start\n", ch->is->name);
		if ((ch->bch.state == ISDN_P_B_T30_FAX) &&
		    (ch->cmd == PCTRL_CMD_FTH)) {
			if (count > 1) {
				if ((ptr[0] == 0xff) && (ptr[1] == 0x13)) {
					/* last frame */
					test_and_set_bit(FLG_LASTDATA,
							 &ch->bch.Flags);
					pr_debug("%s: set LASTDATA\n",
						 ch->is->name);
					if (msb == HDLC_FED)
						test_and_set_bit(FLG_DLEETX,
								 &ch->bch.Flags);
				}
			}
		}
		msb |= HDLC_FST;
	}
	ch->bch.tx_idx += count;
	switch (ch->bch.state) {
	case ISDN_P_NONE:
		pr_info("%s: wrong protocol 0\n", __func__);
		break;
	case ISDN_P_B_RAW:
	case ISDN_P_B_L2DTMF:
	case ISDN_P_B_MODEM_ASYNC:
		send_mbox(ch->is, SET_DPS(ch->dpath) | ISAR_HIS_SDATA,
			  0, count, ptr);
		break;
	case ISDN_P_B_HDLC:
		send_mbox(ch->is, SET_DPS(ch->dpath) | ISAR_HIS_SDATA,
			  msb, count, ptr);
		break;
	case ISDN_P_B_T30_FAX:
		if (ch->state != STFAX_ACTIV)
			pr_debug("%s: not ACTIV\n", ch->is->name);
		else if (ch->cmd == PCTRL_CMD_FTH)
			send_mbox(ch->is, SET_DPS(ch->dpath) | ISAR_HIS_SDATA,
				  msb, count, ptr);
		else if (ch->cmd == PCTRL_CMD_FTM)
			send_mbox(ch->is, SET_DPS(ch->dpath) | ISAR_HIS_SDATA,
				  0, count, ptr);
		else
			pr_debug("%s: not FTH/FTM\n", ch->is->name);
		break;
	default:
		pr_info("%s: protocol(%x) error\n",
			__func__, ch->bch.state);
		break;
	}
}

static inline struct isar_ch *
sel_bch_isar(struct isar_hw *isar, u8 dpath)
{
	struct isar_ch	*base = &isar->ch[0];

	if ((!dpath) || (dpath > 2))
		return NULL;
	if (base->dpath == dpath)
		return base;
	base++;
	if (base->dpath == dpath)
		return base;
	return NULL;
}

static void
send_next(struct isar_ch *ch)
{
	pr_debug("%s: %s ch%d tx_skb %d tx_idx %d\n", ch->is->name, __func__,
		 ch->bch.nr, ch->bch.tx_skb ? ch->bch.tx_skb->len : -1,
		 ch->bch.tx_idx);
	if (ch->bch.state == ISDN_P_B_T30_FAX) {
		if (ch->cmd == PCTRL_CMD_FTH) {
			if (test_bit(FLG_LASTDATA, &ch->bch.Flags)) {
				pr_debug("set NMD_DATA\n");
				test_and_set_bit(FLG_NMD_DATA, &ch->bch.Flags);
			}
		} else if (ch->cmd == PCTRL_CMD_FTM) {
			if (test_bit(FLG_DLEETX, &ch->bch.Flags)) {
				test_and_set_bit(FLG_LASTDATA, &ch->bch.Flags);
				test_and_set_bit(FLG_NMD_DATA, &ch->bch.Flags);
			}
		}
	}
	dev_kfree_skb(ch->bch.tx_skb);
	if (get_next_bframe(&ch->bch)) {
		isar_fill_fifo(ch);
		test_and_clear_bit(FLG_TX_EMPTY, &ch->bch.Flags);
	} else if (test_bit(FLG_TX_EMPTY, &ch->bch.Flags)) {
		isar_fill_fifo(ch);
	} else {
		if (test_and_clear_bit(FLG_DLEETX, &ch->bch.Flags)) {
			if (test_and_clear_bit(FLG_LASTDATA,
					       &ch->bch.Flags)) {
				if (test_and_clear_bit(FLG_NMD_DATA,
						       &ch->bch.Flags)) {
					u8 zd = 0;
					send_mbox(ch->is, SET_DPS(ch->dpath) |
						  ISAR_HIS_SDATA, 0x01, 1, &zd);
				}
				test_and_set_bit(FLG_LL_OK, &ch->bch.Flags);
			} else {
				deliver_status(ch, HW_MOD_CONNECT);
			}
		} else if (test_bit(FLG_FILLEMPTY, &ch->bch.Flags)) {
			test_and_set_bit(FLG_TX_EMPTY, &ch->bch.Flags);
		}
	}
}

static void
check_send(struct isar_hw *isar, u8 rdm)
{
	struct isar_ch	*ch;

	pr_debug("%s: rdm %x\n", isar->name, rdm);
	if (rdm & BSTAT_RDM1) {
		ch = sel_bch_isar(isar, 1);
		if (ch && test_bit(FLG_ACTIVE, &ch->bch.Flags)) {
			if (ch->bch.tx_skb && (ch->bch.tx_skb->len >
					       ch->bch.tx_idx))
				isar_fill_fifo(ch);
			else
				send_next(ch);
		}
	}
	if (rdm & BSTAT_RDM2) {
		ch = sel_bch_isar(isar, 2);
		if (ch && test_bit(FLG_ACTIVE, &ch->bch.Flags)) {
			if (ch->bch.tx_skb && (ch->bch.tx_skb->len >
					       ch->bch.tx_idx))
				isar_fill_fifo(ch);
			else
				send_next(ch);
		}
	}
}

static const char *dmril[] = {"NO SPEED", "1200/75", "NODEF2", "75/1200", "NODEF4",
		       "300", "600", "1200", "2400", "4800", "7200",
		       "9600nt", "9600t", "12000", "14400", "WRONG"};
static const char *dmrim[] = {"NO MOD", "NO DEF", "V32/V32b", "V22", "V21",
		       "Bell103", "V23", "Bell202", "V17", "V29", "V27ter"};

static void
isar_pump_status_rsp(struct isar_ch *ch) {
	u8 ril = ch->is->buf[0];
	u8 rim;

	if (!test_and_clear_bit(ISAR_RATE_REQ, &ch->is->Flags))
		return;
	if (ril > 14) {
		pr_info("%s: wrong pstrsp ril=%d\n", ch->is->name, ril);
		ril = 15;
	}
	switch (ch->is->buf[1]) {
	case 0:
		rim = 0;
		break;
	case 0x20:
		rim = 2;
		break;
	case 0x40:
		rim = 3;
		break;
	case 0x41:
		rim = 4;
		break;
	case 0x51:
		rim = 5;
		break;
	case 0x61:
		rim = 6;
		break;
	case 0x71:
		rim = 7;
		break;
	case 0x82:
		rim = 8;
		break;
	case 0x92:
		rim = 9;
		break;
	case 0xa2:
		rim = 10;
		break;
	default:
		rim = 1;
		break;
	}
	sprintf(ch->conmsg, "%s %s", dmril[ril], dmrim[rim]);
	pr_debug("%s: pump strsp %s\n", ch->is->name, ch->conmsg);
}

static void
isar_pump_statev_modem(struct isar_ch *ch, u8 devt) {
	u8 dps = SET_DPS(ch->dpath);

	switch (devt) {
	case PSEV_10MS_TIMER:
		pr_debug("%s: pump stev TIMER\n", ch->is->name);
		break;
	case PSEV_CON_ON:
		pr_debug("%s: pump stev CONNECT\n", ch->is->name);
		deliver_status(ch, HW_MOD_CONNECT);
		break;
	case PSEV_CON_OFF:
		pr_debug("%s: pump stev NO CONNECT\n", ch->is->name);
		send_mbox(ch->is, dps | ISAR_HIS_PSTREQ, 0, 0, NULL);
		deliver_status(ch, HW_MOD_NOCARR);
		break;
	case PSEV_V24_OFF:
		pr_debug("%s: pump stev V24 OFF\n", ch->is->name);
		break;
	case PSEV_CTS_ON:
		pr_debug("%s: pump stev CTS ON\n", ch->is->name);
		break;
	case PSEV_CTS_OFF:
		pr_debug("%s pump stev CTS OFF\n", ch->is->name);
		break;
	case PSEV_DCD_ON:
		pr_debug("%s: pump stev CARRIER ON\n", ch->is->name);
		test_and_set_bit(ISAR_RATE_REQ, &ch->is->Flags);
		send_mbox(ch->is, dps | ISAR_HIS_PSTREQ, 0, 0, NULL);
		break;
	case PSEV_DCD_OFF:
		pr_debug("%s: pump stev CARRIER OFF\n", ch->is->name);
		break;
	case PSEV_DSR_ON:
		pr_debug("%s: pump stev DSR ON\n", ch->is->name);
		break;
	case PSEV_DSR_OFF:
		pr_debug("%s: pump stev DSR_OFF\n", ch->is->name);
		break;
	case PSEV_REM_RET:
		pr_debug("%s: pump stev REMOTE RETRAIN\n", ch->is->name);
		break;
	case PSEV_REM_REN:
		pr_debug("%s: pump stev REMOTE RENEGOTIATE\n", ch->is->name);
		break;
	case PSEV_GSTN_CLR:
		pr_debug("%s: pump stev GSTN CLEAR\n", ch->is->name);
		break;
	default:
		pr_info("u%s: unknown pump stev %x\n", ch->is->name, devt);
		break;
	}
}

static void
isar_pump_statev_fax(struct isar_ch *ch, u8 devt) {
	u8 dps = SET_DPS(ch->dpath);
	u8 p1;

	switch (devt) {
	case PSEV_10MS_TIMER:
		pr_debug("%s: pump stev TIMER\n", ch->is->name);
		break;
	case PSEV_RSP_READY:
		pr_debug("%s: pump stev RSP_READY\n", ch->is->name);
		ch->state = STFAX_READY;
		deliver_status(ch, HW_MOD_READY);
#ifdef AUTOCON
		if (test_bit(BC_FLG_ORIG, &ch->bch.Flags))
			isar_pump_cmd(bch, HW_MOD_FRH, 3);
		else
			isar_pump_cmd(bch, HW_MOD_FTH, 3);
#endif
		break;
	case PSEV_LINE_TX_H:
		if (ch->state == STFAX_LINE) {
			pr_debug("%s: pump stev LINE_TX_H\n", ch->is->name);
			ch->state = STFAX_CONT;
			send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL,
				  PCTRL_CMD_CONT, 0, NULL);
		} else {
			pr_debug("%s: pump stev LINE_TX_H wrong st %x\n",
				 ch->is->name, ch->state);
		}
		break;
	case PSEV_LINE_RX_H:
		if (ch->state == STFAX_LINE) {
			pr_debug("%s: pump stev LINE_RX_H\n", ch->is->name);
			ch->state = STFAX_CONT;
			send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL,
				  PCTRL_CMD_CONT, 0, NULL);
		} else {
			pr_debug("%s: pump stev LINE_RX_H wrong st %x\n",
				 ch->is->name, ch->state);
		}
		break;
	case PSEV_LINE_TX_B:
		if (ch->state == STFAX_LINE) {
			pr_debug("%s: pump stev LINE_TX_B\n", ch->is->name);
			ch->state = STFAX_CONT;
			send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL,
				  PCTRL_CMD_CONT, 0, NULL);
		} else {
			pr_debug("%s: pump stev LINE_TX_B wrong st %x\n",
				 ch->is->name, ch->state);
		}
		break;
	case PSEV_LINE_RX_B:
		if (ch->state == STFAX_LINE) {
			pr_debug("%s: pump stev LINE_RX_B\n", ch->is->name);
			ch->state = STFAX_CONT;
			send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL,
				  PCTRL_CMD_CONT, 0, NULL);
		} else {
			pr_debug("%s: pump stev LINE_RX_B wrong st %x\n",
				 ch->is->name, ch->state);
		}
		break;
	case PSEV_RSP_CONN:
		if (ch->state == STFAX_CONT) {
			pr_debug("%s: pump stev RSP_CONN\n", ch->is->name);
			ch->state = STFAX_ACTIV;
			test_and_set_bit(ISAR_RATE_REQ, &ch->is->Flags);
			send_mbox(ch->is, dps | ISAR_HIS_PSTREQ, 0, 0, NULL);
			if (ch->cmd == PCTRL_CMD_FTH) {
				int delay = (ch->mod == 3) ? 1000 : 200;
				/* 1s (200 ms) Flags before data */
				if (test_and_set_bit(FLG_FTI_RUN,
						     &ch->bch.Flags))
					del_timer(&ch->ftimer);
				ch->ftimer.expires =
					jiffies + ((delay * HZ) / 1000);
				test_and_set_bit(FLG_LL_CONN,
						 &ch->bch.Flags);
				add_timer(&ch->ftimer);
			} else {
				deliver_status(ch, HW_MOD_CONNECT);
			}
		} else {
			pr_debug("%s: pump stev RSP_CONN wrong st %x\n",
				 ch->is->name, ch->state);
		}
		break;
	case PSEV_FLAGS_DET:
		pr_debug("%s: pump stev FLAGS_DET\n", ch->is->name);
		break;
	case PSEV_RSP_DISC:
		pr_debug("%s: pump stev RSP_DISC state(%d)\n",
			 ch->is->name, ch->state);
		if (ch->state == STFAX_ESCAPE) {
			p1 = 5;
			switch (ch->newcmd) {
			case 0:
				ch->state = STFAX_READY;
				break;
			case PCTRL_CMD_FTM:
				p1 = 2;
				fallthrough;
			case PCTRL_CMD_FTH:
				send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL,
					  PCTRL_CMD_SILON, 1, &p1);
				ch->state = STFAX_SILDET;
				break;
			case PCTRL_CMD_FRH:
			case PCTRL_CMD_FRM:
				ch->mod = ch->newmod;
				p1 = ch->newmod;
				ch->newmod = 0;
				ch->cmd = ch->newcmd;
				ch->newcmd = 0;
				send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL,
					  ch->cmd, 1, &p1);
				ch->state = STFAX_LINE;
				ch->try_mod = 3;
				break;
			default:
				pr_debug("%s: RSP_DISC unknown newcmd %x\n",
					 ch->is->name, ch->newcmd);
				break;
			}
		} else if (ch->state == STFAX_ACTIV) {
			if (test_and_clear_bit(FLG_LL_OK, &ch->bch.Flags))
				deliver_status(ch, HW_MOD_OK);
			else if (ch->cmd == PCTRL_CMD_FRM)
				deliver_status(ch, HW_MOD_NOCARR);
			else
				deliver_status(ch, HW_MOD_FCERROR);
			ch->state = STFAX_READY;
		} else if (ch->state != STFAX_SILDET) {
			/* ignore in STFAX_SILDET */
			ch->state = STFAX_READY;
			deliver_status(ch, HW_MOD_FCERROR);
		}
		break;
	case PSEV_RSP_SILDET:
		pr_debug("%s: pump stev RSP_SILDET\n", ch->is->name);
		if (ch->state == STFAX_SILDET) {
			ch->mod = ch->newmod;
			p1 = ch->newmod;
			ch->newmod = 0;
			ch->cmd = ch->newcmd;
			ch->newcmd = 0;
			send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL,
				  ch->cmd, 1, &p1);
			ch->state = STFAX_LINE;
			ch->try_mod = 3;
		}
		break;
	case PSEV_RSP_SILOFF:
		pr_debug("%s: pump stev RSP_SILOFF\n", ch->is->name);
		break;
	case PSEV_RSP_FCERR:
		if (ch->state == STFAX_LINE) {
			pr_debug("%s: pump stev RSP_FCERR try %d\n",
				 ch->is->name, ch->try_mod);
			if (ch->try_mod--) {
				send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL,
					  ch->cmd, 1, &ch->mod);
				break;
			}
		}
		pr_debug("%s: pump stev RSP_FCERR\n", ch->is->name);
		ch->state = STFAX_ESCAPE;
		send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL, PCTRL_CMD_ESC,
			  0, NULL);
		deliver_status(ch, HW_MOD_FCERROR);
		break;
	default:
		break;
	}
}

void
mISDNisar_irq(struct isar_hw *isar)
{
	struct isar_ch *ch;

	get_irq_infos(isar);
	switch (isar->iis & ISAR_IIS_MSCMSD) {
	case ISAR_IIS_RDATA:
		ch = sel_bch_isar(isar, isar->iis >> 6);
		if (ch)
			isar_rcv_frame(ch);
		else {
			pr_debug("%s: ISAR spurious IIS_RDATA %x/%x/%x\n",
				 isar->name, isar->iis, isar->cmsb,
				 isar->clsb);
			isar->write_reg(isar->hw, ISAR_IIA, 0);
		}
		break;
	case ISAR_IIS_GSTEV:
		isar->write_reg(isar->hw, ISAR_IIA, 0);
		isar->bstat |= isar->cmsb;
		check_send(isar, isar->cmsb);
		break;
	case ISAR_IIS_BSTEV:
#ifdef ERROR_STATISTIC
		ch = sel_bch_isar(isar, isar->iis >> 6);
		if (ch) {
			if (isar->cmsb == BSTEV_TBO)
				ch->bch.err_tx++;
			if (isar->cmsb == BSTEV_RBO)
				ch->bch.err_rdo++;
		}
#endif
		pr_debug("%s: Buffer STEV dpath%d msb(%x)\n",
			 isar->name, isar->iis >> 6, isar->cmsb);
		isar->write_reg(isar->hw, ISAR_IIA, 0);
		break;
	case ISAR_IIS_PSTEV:
		ch = sel_bch_isar(isar, isar->iis >> 6);
		if (ch) {
			rcv_mbox(isar, NULL);
			if (ch->bch.state == ISDN_P_B_MODEM_ASYNC)
				isar_pump_statev_modem(ch, isar->cmsb);
			else if (ch->bch.state == ISDN_P_B_T30_FAX)
				isar_pump_statev_fax(ch, isar->cmsb);
			else if (ch->bch.state == ISDN_P_B_RAW) {
				int	tt;
				tt = isar->cmsb | 0x30;
				if (tt == 0x3e)
					tt = '*';
				else if (tt == 0x3f)
					tt = '#';
				else if (tt > '9')
					tt += 7;
				tt |= DTMF_TONE_VAL;
				_queue_data(&ch->bch.ch, PH_CONTROL_IND,
					    MISDN_ID_ANY, sizeof(tt), &tt,
					    GFP_ATOMIC);
			} else
				pr_debug("%s: ISAR IIS_PSTEV pm %d sta %x\n",
					 isar->name, ch->bch.state,
					 isar->cmsb);
		} else {
			pr_debug("%s: ISAR spurious IIS_PSTEV %x/%x/%x\n",
				 isar->name, isar->iis, isar->cmsb,
				 isar->clsb);
			isar->write_reg(isar->hw, ISAR_IIA, 0);
		}
		break;
	case ISAR_IIS_PSTRSP:
		ch = sel_bch_isar(isar, isar->iis >> 6);
		if (ch) {
			rcv_mbox(isar, NULL);
			isar_pump_status_rsp(ch);
		} else {
			pr_debug("%s: ISAR spurious IIS_PSTRSP %x/%x/%x\n",
				 isar->name, isar->iis, isar->cmsb,
				 isar->clsb);
			isar->write_reg(isar->hw, ISAR_IIA, 0);
		}
		break;
	case ISAR_IIS_DIAG:
	case ISAR_IIS_BSTRSP:
	case ISAR_IIS_IOM2RSP:
		rcv_mbox(isar, NULL);
		break;
	case ISAR_IIS_INVMSG:
		rcv_mbox(isar, NULL);
		pr_debug("%s: invalid msg his:%x\n", isar->name, isar->cmsb);
		break;
	default:
		rcv_mbox(isar, NULL);
		pr_debug("%s: unhandled msg iis(%x) ctrl(%x/%x)\n",
			 isar->name, isar->iis, isar->cmsb, isar->clsb);
		break;
	}
}
EXPORT_SYMBOL(mISDNisar_irq);

static void
ftimer_handler(struct timer_list *t)
{
	struct isar_ch *ch = from_timer(ch, t, ftimer);

	pr_debug("%s: ftimer flags %lx\n", ch->is->name, ch->bch.Flags);
	test_and_clear_bit(FLG_FTI_RUN, &ch->bch.Flags);
	if (test_and_clear_bit(FLG_LL_CONN, &ch->bch.Flags))
		deliver_status(ch, HW_MOD_CONNECT);
}

static void
setup_pump(struct isar_ch *ch) {
	u8 dps = SET_DPS(ch->dpath);
	u8 ctrl, param[6];

	switch (ch->bch.state) {
	case ISDN_P_NONE:
	case ISDN_P_B_RAW:
	case ISDN_P_B_HDLC:
		send_mbox(ch->is, dps | ISAR_HIS_PUMPCFG, PMOD_BYPASS, 0, NULL);
		break;
	case ISDN_P_B_L2DTMF:
		if (test_bit(FLG_DTMFSEND, &ch->bch.Flags)) {
			param[0] = 5; /* TOA 5 db */
			send_mbox(ch->is, dps | ISAR_HIS_PUMPCFG,
				  PMOD_DTMF_TRANS, 1, param);
		} else {
			param[0] = 40; /* REL -46 dbm */
			send_mbox(ch->is, dps | ISAR_HIS_PUMPCFG,
				  PMOD_DTMF, 1, param);
		}
		fallthrough;
	case ISDN_P_B_MODEM_ASYNC:
		ctrl = PMOD_DATAMODEM;
		if (test_bit(FLG_ORIGIN, &ch->bch.Flags)) {
			ctrl |= PCTRL_ORIG;
			param[5] = PV32P6_CTN;
		} else {
			param[5] = PV32P6_ATN;
		}
		param[0] = 6; /* 6 db */
		param[1] = PV32P2_V23R | PV32P2_V22A | PV32P2_V22B |
			PV32P2_V22C | PV32P2_V21 | PV32P2_BEL;
		param[2] = PV32P3_AMOD | PV32P3_V32B | PV32P3_V23B;
		param[3] = PV32P4_UT144;
		param[4] = PV32P5_UT144;
		send_mbox(ch->is, dps | ISAR_HIS_PUMPCFG, ctrl, 6, param);
		break;
	case ISDN_P_B_T30_FAX:
		ctrl = PMOD_FAX;
		if (test_bit(FLG_ORIGIN, &ch->bch.Flags)) {
			ctrl |= PCTRL_ORIG;
			param[1] = PFAXP2_CTN;
		} else {
			param[1] = PFAXP2_ATN;
		}
		param[0] = 6; /* 6 db */
		send_mbox(ch->is, dps | ISAR_HIS_PUMPCFG, ctrl, 2, param);
		ch->state = STFAX_NULL;
		ch->newcmd = 0;
		ch->newmod = 0;
		test_and_set_bit(FLG_FTI_RUN, &ch->bch.Flags);
		break;
	}
	udelay(1000);
	send_mbox(ch->is, dps | ISAR_HIS_PSTREQ, 0, 0, NULL);
	udelay(1000);
}

static void
setup_sart(struct isar_ch *ch) {
	u8 dps = SET_DPS(ch->dpath);
	u8 ctrl, param[2] = {0, 0};

	switch (ch->bch.state) {
	case ISDN_P_NONE:
		send_mbox(ch->is, dps | ISAR_HIS_SARTCFG, SMODE_DISABLE,
			  0, NULL);
		break;
	case ISDN_P_B_RAW:
	case ISDN_P_B_L2DTMF:
		send_mbox(ch->is, dps | ISAR_HIS_SARTCFG, SMODE_BINARY,
			  2, param);
		break;
	case ISDN_P_B_HDLC:
	case ISDN_P_B_T30_FAX:
		send_mbox(ch->is, dps | ISAR_HIS_SARTCFG, SMODE_HDLC,
			  1, param);
		break;
	case ISDN_P_B_MODEM_ASYNC:
		ctrl = SMODE_V14 | SCTRL_HDMC_BOTH;
		param[0] = S_P1_CHS_8;
		param[1] = S_P2_BFT_DEF;
		send_mbox(ch->is, dps | ISAR_HIS_SARTCFG, ctrl, 2, param);
		break;
	}
	udelay(1000);
	send_mbox(ch->is, dps | ISAR_HIS_BSTREQ, 0, 0, NULL);
	udelay(1000);
}

static void
setup_iom2(struct isar_ch *ch) {
	u8 dps = SET_DPS(ch->dpath);
	u8 cmsb = IOM_CTRL_ENA, msg[5] = {IOM_P1_TXD, 0, 0, 0, 0};

	if (ch->bch.nr == 2) {
		msg[1] = 1;
		msg[3] = 1;
	}
	switch (ch->bch.state) {
	case ISDN_P_NONE:
		cmsb = 0;
		/* dummy slot */
		msg[1] = ch->dpath + 2;
		msg[3] = ch->dpath + 2;
		break;
	case ISDN_P_B_RAW:
	case ISDN_P_B_HDLC:
		break;
	case ISDN_P_B_MODEM_ASYNC:
	case ISDN_P_B_T30_FAX:
		cmsb |= IOM_CTRL_RCV;
		fallthrough;
	case ISDN_P_B_L2DTMF:
		if (test_bit(FLG_DTMFSEND, &ch->bch.Flags))
			cmsb |= IOM_CTRL_RCV;
		cmsb |= IOM_CTRL_ALAW;
		break;
	}
	send_mbox(ch->is, dps | ISAR_HIS_IOM2CFG, cmsb, 5, msg);
	udelay(1000);
	send_mbox(ch->is, dps | ISAR_HIS_IOM2REQ, 0, 0, NULL);
	udelay(1000);
}

static int
modeisar(struct isar_ch *ch, u32 bprotocol)
{
	/* Here we are selecting the best datapath for requested protocol */
	if (ch->bch.state == ISDN_P_NONE) { /* New Setup */
		switch (bprotocol) {
		case ISDN_P_NONE: /* init */
			if (!ch->dpath)
				/* no init for dpath 0 */
				return 0;
			test_and_clear_bit(FLG_HDLC, &ch->bch.Flags);
			test_and_clear_bit(FLG_TRANSPARENT, &ch->bch.Flags);
			break;
		case ISDN_P_B_RAW:
		case ISDN_P_B_HDLC:
			/* best is datapath 2 */
			if (!test_and_set_bit(ISAR_DP2_USE, &ch->is->Flags))
				ch->dpath = 2;
			else if (!test_and_set_bit(ISAR_DP1_USE,
						   &ch->is->Flags))
				ch->dpath = 1;
			else {
				pr_info("modeisar both paths in use\n");
				return -EBUSY;
			}
			if (bprotocol == ISDN_P_B_HDLC)
				test_and_set_bit(FLG_HDLC, &ch->bch.Flags);
			else
				test_and_set_bit(FLG_TRANSPARENT,
						 &ch->bch.Flags);
			break;
		case ISDN_P_B_MODEM_ASYNC:
		case ISDN_P_B_T30_FAX:
		case ISDN_P_B_L2DTMF:
			/* only datapath 1 */
			if (!test_and_set_bit(ISAR_DP1_USE, &ch->is->Flags))
				ch->dpath = 1;
			else {
				pr_info("%s: ISAR modeisar analog functions"
					"only with DP1\n", ch->is->name);
				return -EBUSY;
			}
			break;
		default:
			pr_info("%s: protocol not known %x\n", ch->is->name,
				bprotocol);
			return -ENOPROTOOPT;
		}
	}
	pr_debug("%s: ISAR ch%d dp%d protocol %x->%x\n", ch->is->name,
		 ch->bch.nr, ch->dpath, ch->bch.state, bprotocol);
	ch->bch.state = bprotocol;
	setup_pump(ch);
	setup_iom2(ch);
	setup_sart(ch);
	if (ch->bch.state == ISDN_P_NONE) {
		/* Clear resources */
		if (ch->dpath == 1)
			test_and_clear_bit(ISAR_DP1_USE, &ch->is->Flags);
		else if (ch->dpath == 2)
			test_and_clear_bit(ISAR_DP2_USE, &ch->is->Flags);
		ch->dpath = 0;
		ch->is->ctrl(ch->is->hw, HW_DEACT_IND, ch->bch.nr);
	} else
		ch->is->ctrl(ch->is->hw, HW_ACTIVATE_IND, ch->bch.nr);
	return 0;
}

static void
isar_pump_cmd(struct isar_ch *ch, u32 cmd, u8 para)
{
	u8 dps = SET_DPS(ch->dpath);
	u8 ctrl = 0, nom = 0, p1 = 0;

	pr_debug("%s: isar_pump_cmd %x/%x state(%x)\n",
		 ch->is->name, cmd, para, ch->bch.state);
	switch (cmd) {
	case HW_MOD_FTM:
		if (ch->state == STFAX_READY) {
			p1 = para;
			ctrl = PCTRL_CMD_FTM;
			nom = 1;
			ch->state = STFAX_LINE;
			ch->cmd = ctrl;
			ch->mod = para;
			ch->newmod = 0;
			ch->newcmd = 0;
			ch->try_mod = 3;
		} else if ((ch->state == STFAX_ACTIV) &&
			   (ch->cmd == PCTRL_CMD_FTM) && (ch->mod == para))
			deliver_status(ch, HW_MOD_CONNECT);
		else {
			ch->newmod = para;
			ch->newcmd = PCTRL_CMD_FTM;
			nom = 0;
			ctrl = PCTRL_CMD_ESC;
			ch->state = STFAX_ESCAPE;
		}
		break;
	case HW_MOD_FTH:
		if (ch->state == STFAX_READY) {
			p1 = para;
			ctrl = PCTRL_CMD_FTH;
			nom = 1;
			ch->state = STFAX_LINE;
			ch->cmd = ctrl;
			ch->mod = para;
			ch->newmod = 0;
			ch->newcmd = 0;
			ch->try_mod = 3;
		} else if ((ch->state == STFAX_ACTIV) &&
			   (ch->cmd == PCTRL_CMD_FTH) && (ch->mod == para))
			deliver_status(ch, HW_MOD_CONNECT);
		else {
			ch->newmod = para;
			ch->newcmd = PCTRL_CMD_FTH;
			nom = 0;
			ctrl = PCTRL_CMD_ESC;
			ch->state = STFAX_ESCAPE;
		}
		break;
	case HW_MOD_FRM:
		if (ch->state == STFAX_READY) {
			p1 = para;
			ctrl = PCTRL_CMD_FRM;
			nom = 1;
			ch->state = STFAX_LINE;
			ch->cmd = ctrl;
			ch->mod = para;
			ch->newmod = 0;
			ch->newcmd = 0;
			ch->try_mod = 3;
		} else if ((ch->state == STFAX_ACTIV) &&
			   (ch->cmd == PCTRL_CMD_FRM) && (ch->mod == para))
			deliver_status(ch, HW_MOD_CONNECT);
		else {
			ch->newmod = para;
			ch->newcmd = PCTRL_CMD_FRM;
			nom = 0;
			ctrl = PCTRL_CMD_ESC;
			ch->state = STFAX_ESCAPE;
		}
		break;
	case HW_MOD_FRH:
		if (ch->state == STFAX_READY) {
			p1 = para;
			ctrl = PCTRL_CMD_FRH;
			nom = 1;
			ch->state = STFAX_LINE;
			ch->cmd = ctrl;
			ch->mod = para;
			ch->newmod = 0;
			ch->newcmd = 0;
			ch->try_mod = 3;
		} else if ((ch->state == STFAX_ACTIV) &&
			   (ch->cmd == PCTRL_CMD_FRH) && (ch->mod == para))
			deliver_status(ch, HW_MOD_CONNECT);
		else {
			ch->newmod = para;
			ch->newcmd = PCTRL_CMD_FRH;
			nom = 0;
			ctrl = PCTRL_CMD_ESC;
			ch->state = STFAX_ESCAPE;
		}
		break;
	case PCTRL_CMD_TDTMF:
		p1 = para;
		nom = 1;
		ctrl = PCTRL_CMD_TDTMF;
		break;
	}
	if (ctrl)
		send_mbox(ch->is, dps | ISAR_HIS_PUMPCTRL, ctrl, nom, &p1);
}

static void
isar_setup(struct isar_hw *isar)
{
	u8 msg;
	int i;

	/* Dpath 1, 2 */
	msg = 61;
	for (i = 0; i < 2; i++) {
		/* Buffer Config */
		send_mbox(isar, (i ? ISAR_HIS_DPS2 : ISAR_HIS_DPS1) |
			  ISAR_HIS_P12CFG, 4, 1, &msg);
		isar->ch[i].mml = msg;
		isar->ch[i].bch.state = 0;
		isar->ch[i].dpath = i + 1;
		modeisar(&isar->ch[i], ISDN_P_NONE);
	}
}

static int
isar_l2l1(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	struct isar_ch *ich = container_of(bch, struct isar_ch, bch);
	int ret = -EINVAL;
	struct mISDNhead *hh = mISDN_HEAD_P(skb);
	u32 id, *val;
	u_long flags;

	switch (hh->prim) {
	case PH_DATA_REQ:
		spin_lock_irqsave(ich->is->hwlock, flags);
		ret = bchannel_senddata(bch, skb);
		if (ret > 0) { /* direct TX */
			ret = 0;
			isar_fill_fifo(ich);
		}
		spin_unlock_irqrestore(ich->is->hwlock, flags);
		return ret;
	case PH_ACTIVATE_REQ:
		spin_lock_irqsave(ich->is->hwlock, flags);
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags))
			ret = modeisar(ich, ch->protocol);
		else
			ret = 0;
		spin_unlock_irqrestore(ich->is->hwlock, flags);
		if (!ret)
			_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY, 0,
				    NULL, GFP_KERNEL);
		break;
	case PH_DEACTIVATE_REQ:
		spin_lock_irqsave(ich->is->hwlock, flags);
		mISDN_clear_bchannel(bch);
		modeisar(ich, ISDN_P_NONE);
		spin_unlock_irqrestore(ich->is->hwlock, flags);
		_queue_data(ch, PH_DEACTIVATE_IND, MISDN_ID_ANY, 0,
			    NULL, GFP_KERNEL);
		ret = 0;
		break;
	case PH_CONTROL_REQ:
		val = (u32 *)skb->data;
		pr_debug("%s: PH_CONTROL | REQUEST %x/%x\n", ich->is->name,
			 hh->id, *val);
		if ((hh->id == 0) && ((*val & ~DTMF_TONE_MASK) ==
				      DTMF_TONE_VAL)) {
			if (bch->state == ISDN_P_B_L2DTMF) {
				char tt = *val & DTMF_TONE_MASK;

				if (tt == '*')
					tt = 0x1e;
				else if (tt == '#')
					tt = 0x1f;
				else if (tt > '9')
					tt -= 7;
				tt &= 0x1f;
				spin_lock_irqsave(ich->is->hwlock, flags);
				isar_pump_cmd(ich, PCTRL_CMD_TDTMF, tt);
				spin_unlock_irqrestore(ich->is->hwlock, flags);
			} else {
				pr_info("%s: DTMF send wrong protocol %x\n",
					__func__, bch->state);
				return -EINVAL;
			}
		} else if ((hh->id == HW_MOD_FRM) || (hh->id == HW_MOD_FRH) ||
			   (hh->id == HW_MOD_FTM) || (hh->id == HW_MOD_FTH)) {
			for (id = 0; id < FAXMODCNT; id++)
				if (faxmodulation[id] == *val)
					break;
			if ((FAXMODCNT > id) &&
			    test_bit(FLG_INITIALIZED, &bch->Flags)) {
				pr_debug("%s: isar: new mod\n", ich->is->name);
				isar_pump_cmd(ich, hh->id, *val);
				ret = 0;
			} else {
				pr_info("%s: wrong modulation\n",
					ich->is->name);
				ret = -EINVAL;
			}
		} else if (hh->id == HW_MOD_LASTDATA)
			test_and_set_bit(FLG_DLEETX, &bch->Flags);
		else {
			pr_info("%s: unknown PH_CONTROL_REQ %x\n",
				ich->is->name, hh->id);
			ret = -EINVAL;
		}
		fallthrough;
	default:
		pr_info("%s: %s unknown prim(%x,%x)\n",
			ich->is->name, __func__, hh->prim, hh->id);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

static int
channel_bctrl(struct bchannel *bch, struct mISDN_ctrl_req *cq)
{
	return mISDN_ctrl_bchannel(bch, cq);
}

static int
isar_bctrl(struct mISDNchannel *ch, u32 cmd, void *arg)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	struct isar_ch *ich = container_of(bch, struct isar_ch, bch);
	int ret = -EINVAL;
	u_long flags;

	pr_debug("%s: %s cmd:%x %p\n", ich->is->name, __func__, cmd, arg);
	switch (cmd) {
	case CLOSE_CHANNEL:
		test_and_clear_bit(FLG_OPEN, &bch->Flags);
		cancel_work_sync(&bch->workq);
		spin_lock_irqsave(ich->is->hwlock, flags);
		mISDN_clear_bchannel(bch);
		modeisar(ich, ISDN_P_NONE);
		spin_unlock_irqrestore(ich->is->hwlock, flags);
		ch->protocol = ISDN_P_NONE;
		ch->peer = NULL;
		module_put(ich->is->owner);
		ret = 0;
		break;
	case CONTROL_CHANNEL:
		ret = channel_bctrl(bch, arg);
		break;
	default:
		pr_info("%s: %s unknown prim(%x)\n",
			ich->is->name, __func__, cmd);
	}
	return ret;
}

static void
free_isar(struct isar_hw *isar)
{
	modeisar(&isar->ch[0], ISDN_P_NONE);
	modeisar(&isar->ch[1], ISDN_P_NONE);
	del_timer(&isar->ch[0].ftimer);
	del_timer(&isar->ch[1].ftimer);
	test_and_clear_bit(FLG_INITIALIZED, &isar->ch[0].bch.Flags);
	test_and_clear_bit(FLG_INITIALIZED, &isar->ch[1].bch.Flags);
}

static int
init_isar(struct isar_hw *isar)
{
	int	cnt = 3;

	while (cnt--) {
		isar->version = ISARVersion(isar);
		if (isar->ch[0].bch.debug & DEBUG_HW)
			pr_notice("%s: Testing version %d (%d time)\n",
				  isar->name, isar->version, 3 - cnt);
		if (isar->version == 1)
			break;
		isar->ctrl(isar->hw, HW_RESET_REQ, 0);
	}
	if (isar->version != 1)
		return -EINVAL;
	timer_setup(&isar->ch[0].ftimer, ftimer_handler, 0);
	test_and_set_bit(FLG_INITIALIZED, &isar->ch[0].bch.Flags);
	timer_setup(&isar->ch[1].ftimer, ftimer_handler, 0);
	test_and_set_bit(FLG_INITIALIZED, &isar->ch[1].bch.Flags);
	return 0;
}

static int
isar_open(struct isar_hw *isar, struct channel_req *rq)
{
	struct bchannel		*bch;

	if (rq->adr.channel == 0 || rq->adr.channel > 2)
		return -EINVAL;
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;
	bch = &isar->ch[rq->adr.channel - 1].bch;
	if (test_and_set_bit(FLG_OPEN, &bch->Flags))
		return -EBUSY; /* b-channel can be only open once */
	bch->ch.protocol = rq->protocol;
	rq->ch = &bch->ch;
	return 0;
}

u32
mISDNisar_init(struct isar_hw *isar, void *hw)
{
	u32 ret, i;

	isar->hw = hw;
	for (i = 0; i < 2; i++) {
		isar->ch[i].bch.nr = i + 1;
		mISDN_initbchannel(&isar->ch[i].bch, MAX_DATA_MEM, 32);
		isar->ch[i].bch.ch.nr = i + 1;
		isar->ch[i].bch.ch.send = &isar_l2l1;
		isar->ch[i].bch.ch.ctrl = isar_bctrl;
		isar->ch[i].bch.hw = hw;
		isar->ch[i].is = isar;
	}

	isar->init = &init_isar;
	isar->release = &free_isar;
	isar->firmware = &load_firmware;
	isar->open = &isar_open;

	ret =	(1 << (ISDN_P_B_RAW & ISDN_P_B_MASK)) |
		(1 << (ISDN_P_B_HDLC & ISDN_P_B_MASK)) |
		(1 << (ISDN_P_B_L2DTMF & ISDN_P_B_MASK)) |
		(1 << (ISDN_P_B_MODEM_ASYNC & ISDN_P_B_MASK)) |
		(1 << (ISDN_P_B_T30_FAX & ISDN_P_B_MASK));

	return ret;
}
EXPORT_SYMBOL(mISDNisar_init);

static int __init isar_mod_init(void)
{
	pr_notice("mISDN: ISAR driver Rev. %s\n", ISAR_REV);
	return 0;
}

static void __exit isar_mod_cleanup(void)
{
	pr_notice("mISDN: ISAR module unloaded\n");
}
module_init(isar_mod_init);
module_exit(isar_mod_cleanup);
