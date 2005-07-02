/* $Id: netjet.c,v 1.29.2.4 2004/02/11 13:21:34 keil Exp $
 *
 * low level stuff for Traverse Technologie NETJet ISDN cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to Traverse Technologies Australia for documents and information
 *
 * 16-Apr-2002 - led code added - Guy Ellis (guy@traverse.com.au)
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/ppp_defs.h>
#include <asm/io.h>
#include "netjet.h"

/* Interface functions */

u_char
NETjet_ReadIC(struct IsdnCardState *cs, u_char offset)
{
	u_char ret;
	
	cs->hw.njet.auxd &= 0xfc;
	cs->hw.njet.auxd |= (offset>>4) & 3;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	ret = bytein(cs->hw.njet.isac + ((offset & 0xf)<<2));
	return(ret);
}

void
NETjet_WriteIC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	cs->hw.njet.auxd &= 0xfc;
	cs->hw.njet.auxd |= (offset>>4) & 3;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	byteout(cs->hw.njet.isac + ((offset & 0xf)<<2), value);
}

void
NETjet_ReadICfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	cs->hw.njet.auxd &= 0xfc;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	insb(cs->hw.njet.isac, data, size);
}

void 
NETjet_WriteICfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	cs->hw.njet.auxd &= 0xfc;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	outsb(cs->hw.njet.isac, data, size);
}

static void fill_mem(struct BCState *bcs, u_int *pos, u_int cnt, int chan, u_char fill)
{
	u_int mask=0x000000ff, val = 0, *p=pos;
	u_int i;
	
	val |= fill;
	if (chan) {
		val  <<= 8;
		mask <<= 8;
	}
	mask ^= 0xffffffff;
	for (i=0; i<cnt; i++) {
		*p   &= mask;
		*p++ |= val;
		if (p > bcs->hw.tiger.s_end)
			p = bcs->hw.tiger.send;
	}
}

static void
mode_tiger(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
        u_char led;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "Tiger mode %d bchan %d/%d",
			mode, bc, bcs->channel);
	bcs->mode = mode;
	bcs->channel = bc;
	switch (mode) {
		case (L1_MODE_NULL):
			fill_mem(bcs, bcs->hw.tiger.send,
				NETJET_DMA_TXSIZE, bc, 0xff);
			if (cs->debug & L1_DEB_HSCX)
				debugl1(cs, "Tiger stat rec %d/%d send %d",
					bcs->hw.tiger.r_tot, bcs->hw.tiger.r_err,
					bcs->hw.tiger.s_tot); 
			if ((cs->bcs[0].mode == L1_MODE_NULL) &&
				(cs->bcs[1].mode == L1_MODE_NULL)) {
				cs->hw.njet.dmactrl = 0;
				byteout(cs->hw.njet.base + NETJET_DMACTRL,
					cs->hw.njet.dmactrl);
				byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0);
			}
                        if (cs->typ == ISDN_CTYPE_NETJET_S)
                        {
                                // led off
                                led = bc & 0x01;
                                led = 0x01 << (6 + led); // convert to mask
                                led = ~led;
                                cs->hw.njet.auxd &= led;
                                byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
                        }
			break;
		case (L1_MODE_TRANS):
			break;
		case (L1_MODE_HDLC_56K):
		case (L1_MODE_HDLC):
			fill_mem(bcs, bcs->hw.tiger.send,
				NETJET_DMA_TXSIZE, bc, 0xff);
			bcs->hw.tiger.r_state = HDLC_ZERO_SEARCH;
			bcs->hw.tiger.r_tot = 0;
			bcs->hw.tiger.r_bitcnt = 0;
			bcs->hw.tiger.r_one = 0;
			bcs->hw.tiger.r_err = 0;
			bcs->hw.tiger.s_tot = 0;
			if (! cs->hw.njet.dmactrl) {
				fill_mem(bcs, bcs->hw.tiger.send,
					NETJET_DMA_TXSIZE, !bc, 0xff);
				cs->hw.njet.dmactrl = 1;
				byteout(cs->hw.njet.base + NETJET_DMACTRL,
					cs->hw.njet.dmactrl);
				byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0x0f);
			/* was 0x3f now 0x0f for TJ300 and TJ320  GE 13/07/00 */
			}
			bcs->hw.tiger.sendp = bcs->hw.tiger.send;
			bcs->hw.tiger.free = NETJET_DMA_TXSIZE;
			test_and_set_bit(BC_FLG_EMPTY, &bcs->Flag);
                        if (cs->typ == ISDN_CTYPE_NETJET_S)
                        {
                                // led on
                                led = bc & 0x01;
                                led = 0x01 << (6 + led); // convert to mask
                                cs->hw.njet.auxd |= led;
                                byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
                        }
			break;
	}
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "tiger: set %x %x %x  %x/%x  pulse=%d",
			bytein(cs->hw.njet.base + NETJET_DMACTRL),
			bytein(cs->hw.njet.base + NETJET_IRQMASK0),
			bytein(cs->hw.njet.base + NETJET_IRQSTAT0),
			inl(cs->hw.njet.base + NETJET_DMA_READ_ADR),
			inl(cs->hw.njet.base + NETJET_DMA_WRITE_ADR),
			bytein(cs->hw.njet.base + NETJET_PULSE_CNT));
}

static void printframe(struct IsdnCardState *cs, u_char *buf, int count, char *s) {
	char tmp[128];
	char *t = tmp;
	int i=count,j;
	u_char *p = buf;

	t += sprintf(t, "tiger %s(%4d)", s, count);
	while (i>0) {
		if (i>16)
			j=16;
		else
			j=i;
		QuickHex(t, p, j);
		debugl1(cs, tmp);
		p += j;
		i -= j;
		t = tmp;
		t += sprintf(t, "tiger %s      ", s);
	}
}

// macro for 64k

#define MAKE_RAW_BYTE for (j=0; j<8; j++) { \
			bitcnt++;\
			s_val >>= 1;\
			if (val & 1) {\
				s_one++;\
				s_val |= 0x80;\
			} else {\
				s_one = 0;\
				s_val &= 0x7f;\
			}\
			if (bitcnt==8) {\
				bcs->hw.tiger.sendbuf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			if (s_one == 5) {\
				s_val >>= 1;\
				s_val &= 0x7f;\
				bitcnt++;\
				s_one = 0;\
			}\
			if (bitcnt==8) {\
				bcs->hw.tiger.sendbuf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			val >>= 1;\
		}

static int make_raw_data(struct BCState *bcs) {
// this make_raw is for 64k
	register u_int i,s_cnt=0;
	register u_char j;
	register u_char val;
	register u_char s_one = 0;
	register u_char s_val = 0;
	register u_char bitcnt = 0;
	u_int fcs;
	
	if (!bcs->tx_skb) {
		debugl1(bcs->cs, "tiger make_raw: NULL skb");
		return(1);
	}
	bcs->hw.tiger.sendbuf[s_cnt++] = HDLC_FLAG_VALUE;
	fcs = PPP_INITFCS;
	for (i=0; i<bcs->tx_skb->len; i++) {
		val = bcs->tx_skb->data[i];
		fcs = PPP_FCS (fcs, val);
		MAKE_RAW_BYTE;
	}
	fcs ^= 0xffff;
	val = fcs & 0xff;
	MAKE_RAW_BYTE;
	val = (fcs>>8) & 0xff;
	MAKE_RAW_BYTE;
	val = HDLC_FLAG_VALUE;
	for (j=0; j<8; j++) { 
		bitcnt++;
		s_val >>= 1;
		if (val & 1)
			s_val |= 0x80;
		else
			s_val &= 0x7f;
		if (bitcnt==8) {
			bcs->hw.tiger.sendbuf[s_cnt++] = s_val;
			bitcnt = 0;
		}
		val >>= 1;
	}
	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs,"tiger make_raw: in %ld out %d.%d",
			bcs->tx_skb->len, s_cnt, bitcnt);
	if (bitcnt) {
		while (8>bitcnt++) {
			s_val >>= 1;
			s_val |= 0x80;
		}
		bcs->hw.tiger.sendbuf[s_cnt++] = s_val;
		bcs->hw.tiger.sendbuf[s_cnt++] = 0xff;	// NJ<->NJ thoughput bug fix
	}
	bcs->hw.tiger.sendcnt = s_cnt;
	bcs->tx_cnt -= bcs->tx_skb->len;
	bcs->hw.tiger.sp = bcs->hw.tiger.sendbuf;
	return(0);
}

// macro for 56k

#define MAKE_RAW_BYTE_56K for (j=0; j<8; j++) { \
			bitcnt++;\
			s_val >>= 1;\
			if (val & 1) {\
				s_one++;\
				s_val |= 0x80;\
			} else {\
				s_one = 0;\
				s_val &= 0x7f;\
			}\
			if (bitcnt==7) {\
				s_val >>= 1;\
				s_val |= 0x80;\
				bcs->hw.tiger.sendbuf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			if (s_one == 5) {\
				s_val >>= 1;\
				s_val &= 0x7f;\
				bitcnt++;\
				s_one = 0;\
			}\
			if (bitcnt==7) {\
				s_val >>= 1;\
				s_val |= 0x80;\
				bcs->hw.tiger.sendbuf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			val >>= 1;\
		}

static int make_raw_data_56k(struct BCState *bcs) {
// this make_raw is for 56k
	register u_int i,s_cnt=0;
	register u_char j;
	register u_char val;
	register u_char s_one = 0;
	register u_char s_val = 0;
	register u_char bitcnt = 0;
	u_int fcs;
	
	if (!bcs->tx_skb) {
		debugl1(bcs->cs, "tiger make_raw_56k: NULL skb");
		return(1);
	}
	val = HDLC_FLAG_VALUE;
	for (j=0; j<8; j++) { 
		bitcnt++;
		s_val >>= 1;
		if (val & 1)
			s_val |= 0x80;
		else
			s_val &= 0x7f;
		if (bitcnt==7) {
			s_val >>= 1;
			s_val |= 0x80;
			bcs->hw.tiger.sendbuf[s_cnt++] = s_val;
			bitcnt = 0;
		}
		val >>= 1;
	};
	fcs = PPP_INITFCS;
	for (i=0; i<bcs->tx_skb->len; i++) {
		val = bcs->tx_skb->data[i];
		fcs = PPP_FCS (fcs, val);
		MAKE_RAW_BYTE_56K;
	}
	fcs ^= 0xffff;
	val = fcs & 0xff;
	MAKE_RAW_BYTE_56K;
	val = (fcs>>8) & 0xff;
	MAKE_RAW_BYTE_56K;
	val = HDLC_FLAG_VALUE;
	for (j=0; j<8; j++) { 
		bitcnt++;
		s_val >>= 1;
		if (val & 1)
			s_val |= 0x80;
		else
			s_val &= 0x7f;
		if (bitcnt==7) {
			s_val >>= 1;
			s_val |= 0x80;
			bcs->hw.tiger.sendbuf[s_cnt++] = s_val;
			bitcnt = 0;
		}
		val >>= 1;
	}
	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs,"tiger make_raw_56k: in %ld out %d.%d",
			bcs->tx_skb->len, s_cnt, bitcnt);
	if (bitcnt) {
		while (8>bitcnt++) {
			s_val >>= 1;
			s_val |= 0x80;
		}
		bcs->hw.tiger.sendbuf[s_cnt++] = s_val;
		bcs->hw.tiger.sendbuf[s_cnt++] = 0xff;	// NJ<->NJ thoughput bug fix
	}
	bcs->hw.tiger.sendcnt = s_cnt;
	bcs->tx_cnt -= bcs->tx_skb->len;
	bcs->hw.tiger.sp = bcs->hw.tiger.sendbuf;
	return(0);
}

static void got_frame(struct BCState *bcs, int count) {
	struct sk_buff *skb;
		
	if (!(skb = dev_alloc_skb(count)))
		printk(KERN_WARNING "TIGER: receive out of memory\n");
	else {
		memcpy(skb_put(skb, count), bcs->hw.tiger.rcvbuf, count);
		skb_queue_tail(&bcs->rqueue, skb);
	}
	test_and_set_bit(B_RCVBUFREADY, &bcs->event);
	schedule_work(&bcs->tqueue);
	
	if (bcs->cs->debug & L1_DEB_RECEIVE_FRAME)
		printframe(bcs->cs, bcs->hw.tiger.rcvbuf, count, "rec");
}



static void read_raw(struct BCState *bcs, u_int *buf, int cnt){
	int i;
	register u_char j;
	register u_char val;
	u_int  *pend = bcs->hw.tiger.rec +NETJET_DMA_RXSIZE -1;
	register u_char state = bcs->hw.tiger.r_state;
	register u_char r_one = bcs->hw.tiger.r_one;
	register u_char r_val = bcs->hw.tiger.r_val;
	register u_int bitcnt = bcs->hw.tiger.r_bitcnt;
	u_int *p = buf;
	int bits;
	u_char mask;

        if (bcs->mode == L1_MODE_HDLC) { // it's 64k
		mask = 0xff;
		bits = 8;
	}
	else { // it's 56K
		mask = 0x7f;
		bits = 7;
	};
	for (i=0;i<cnt;i++) {
		val = bcs->channel ? ((*p>>8) & 0xff) : (*p & 0xff);
		p++;
		if (p > pend)
			p = bcs->hw.tiger.rec;
		if ((val & mask) == mask) {
			state = HDLC_ZERO_SEARCH;
			bcs->hw.tiger.r_tot++;
			bitcnt = 0;
			r_one = 0;
			continue;
		}
		for (j=0;j<bits;j++) {
			if (state == HDLC_ZERO_SEARCH) {
				if (val & 1) {
					r_one++;
				} else {
					r_one=0;
					state= HDLC_FLAG_SEARCH;
					if (bcs->cs->debug & L1_DEB_HSCX)
						debugl1(bcs->cs,"tiger read_raw: zBit(%d,%d,%d) %x",
							bcs->hw.tiger.r_tot,i,j,val);
				}
			} else if (state == HDLC_FLAG_SEARCH) { 
				if (val & 1) {
					r_one++;
					if (r_one>6) {
						state=HDLC_ZERO_SEARCH;
					}
				} else {
					if (r_one==6) {
						bitcnt=0;
						r_val=0;
						state=HDLC_FLAG_FOUND;
						if (bcs->cs->debug & L1_DEB_HSCX)
							debugl1(bcs->cs,"tiger read_raw: flag(%d,%d,%d) %x",
								bcs->hw.tiger.r_tot,i,j,val);
					}
					r_one=0;
				}
			} else if (state ==  HDLC_FLAG_FOUND) {
				if (val & 1) {
					r_one++;
					if (r_one>6) {
						state=HDLC_ZERO_SEARCH;
					} else {
						r_val >>= 1;
						r_val |= 0x80;
						bitcnt++;
					}
				} else {
					if (r_one==6) {
						bitcnt=0;
						r_val=0;
						r_one=0;
						val >>= 1;
						continue;
					} else if (r_one!=5) {
						r_val >>= 1;
						r_val &= 0x7f;
						bitcnt++;
					}
					r_one=0;	
				}
				if ((state != HDLC_ZERO_SEARCH) &&
					!(bitcnt & 7)) {
					state=HDLC_FRAME_FOUND;
					bcs->hw.tiger.r_fcs = PPP_INITFCS;
					bcs->hw.tiger.rcvbuf[0] = r_val;
					bcs->hw.tiger.r_fcs = PPP_FCS (bcs->hw.tiger.r_fcs, r_val);
					if (bcs->cs->debug & L1_DEB_HSCX)
						debugl1(bcs->cs,"tiger read_raw: byte1(%d,%d,%d) rval %x val %x i %x",
							bcs->hw.tiger.r_tot,i,j,r_val,val,
							bcs->cs->hw.njet.irqstat0);
				}
			} else if (state ==  HDLC_FRAME_FOUND) {
				if (val & 1) {
					r_one++;
					if (r_one>6) {
						state=HDLC_ZERO_SEARCH;
						bitcnt=0;
					} else {
						r_val >>= 1;
						r_val |= 0x80;
						bitcnt++;
					}
				} else {
					if (r_one==6) {
						r_val=0; 
						r_one=0;
						bitcnt++;
						if (bitcnt & 7) {
							debugl1(bcs->cs, "tiger: frame not byte aligned");
							state=HDLC_FLAG_SEARCH;
							bcs->hw.tiger.r_err++;
#ifdef ERROR_STATISTIC
							bcs->err_inv++;
#endif
						} else {
							if (bcs->cs->debug & L1_DEB_HSCX)
								debugl1(bcs->cs,"tiger frame end(%d,%d): fcs(%x) i %x",
									i,j,bcs->hw.tiger.r_fcs, bcs->cs->hw.njet.irqstat0);
							if (bcs->hw.tiger.r_fcs == PPP_GOODFCS) {
								got_frame(bcs, (bitcnt>>3)-3);
							} else {
								if (bcs->cs->debug) {
									debugl1(bcs->cs, "tiger FCS error");
									printframe(bcs->cs, bcs->hw.tiger.rcvbuf,
										(bitcnt>>3)-1, "rec");
									bcs->hw.tiger.r_err++;
								}
#ifdef ERROR_STATISTIC
							bcs->err_crc++;
#endif
							}
							state=HDLC_FLAG_FOUND;
						}
						bitcnt=0;
					} else if (r_one==5) {
						val >>= 1;
						r_one=0;
						continue;
					} else {
						r_val >>= 1;
						r_val &= 0x7f;
						bitcnt++;
					}
					r_one=0;	
				}
				if ((state == HDLC_FRAME_FOUND) &&
					!(bitcnt & 7)) {
					if ((bitcnt>>3)>=HSCX_BUFMAX) {
						debugl1(bcs->cs, "tiger: frame too big");
						r_val=0; 
						state=HDLC_FLAG_SEARCH;
						bcs->hw.tiger.r_err++;
#ifdef ERROR_STATISTIC
						bcs->err_inv++;
#endif
					} else {
						bcs->hw.tiger.rcvbuf[(bitcnt>>3)-1] = r_val;
						bcs->hw.tiger.r_fcs = 
							PPP_FCS (bcs->hw.tiger.r_fcs, r_val);
					}
				}
			}
			val >>= 1;
		}
		bcs->hw.tiger.r_tot++;
	}
	bcs->hw.tiger.r_state = state;
	bcs->hw.tiger.r_one = r_one;
	bcs->hw.tiger.r_val = r_val;
	bcs->hw.tiger.r_bitcnt = bitcnt;
}

void read_tiger(struct IsdnCardState *cs) {
	u_int *p;
	int cnt = NETJET_DMA_RXSIZE/2;
	
	if ((cs->hw.njet.irqstat0 & cs->hw.njet.last_is0) & NETJET_IRQM0_READ) {
		debugl1(cs,"tiger warn read double dma %x/%x",
			cs->hw.njet.irqstat0, cs->hw.njet.last_is0);
#ifdef ERROR_STATISTIC
		if (cs->bcs[0].mode)
			cs->bcs[0].err_rdo++;
		if (cs->bcs[1].mode)
			cs->bcs[1].err_rdo++;
#endif
		return;
	} else {
		cs->hw.njet.last_is0 &= ~NETJET_IRQM0_READ;
		cs->hw.njet.last_is0 |= (cs->hw.njet.irqstat0 & NETJET_IRQM0_READ);
	}	
	if (cs->hw.njet.irqstat0 & NETJET_IRQM0_READ_1)
		p = cs->bcs[0].hw.tiger.rec + NETJET_DMA_RXSIZE - 1;
	else
		p = cs->bcs[0].hw.tiger.rec + cnt - 1;
	if ((cs->bcs[0].mode == L1_MODE_HDLC) || (cs->bcs[0].mode == L1_MODE_HDLC_56K))
		read_raw(cs->bcs, p, cnt);

	if ((cs->bcs[1].mode == L1_MODE_HDLC) || (cs->bcs[1].mode == L1_MODE_HDLC_56K))
		read_raw(cs->bcs + 1, p, cnt);
	cs->hw.njet.irqstat0 &= ~NETJET_IRQM0_READ;
}

static void write_raw(struct BCState *bcs, u_int *buf, int cnt);

void netjet_fill_dma(struct BCState *bcs)
{
	register u_int *p, *sp;
	register int cnt;

	if (!bcs->tx_skb)
		return;
	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs,"tiger fill_dma1: c%d %4x", bcs->channel,
			bcs->Flag);
	if (test_and_set_bit(BC_FLG_BUSY, &bcs->Flag))
		return;
	if (bcs->mode == L1_MODE_HDLC) { // it's 64k
		if (make_raw_data(bcs))
			return;		
	}
	else { // it's 56k
		if (make_raw_data_56k(bcs))
			return;		
	};
	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs,"tiger fill_dma2: c%d %4x", bcs->channel,
			bcs->Flag);
	if (test_and_clear_bit(BC_FLG_NOFRAME, &bcs->Flag)) {
		write_raw(bcs, bcs->hw.tiger.sendp, bcs->hw.tiger.free);
	} else if (test_and_clear_bit(BC_FLG_HALF, &bcs->Flag)) {
		p = bus_to_virt(inl(bcs->cs->hw.njet.base + NETJET_DMA_READ_ADR));
		sp = bcs->hw.tiger.sendp;
		if (p == bcs->hw.tiger.s_end)
			p = bcs->hw.tiger.send -1;
		if (sp == bcs->hw.tiger.s_end)
			sp = bcs->hw.tiger.send -1;
		cnt = p - sp;
		if (cnt <0) {
			write_raw(bcs, bcs->hw.tiger.sendp, bcs->hw.tiger.free);
		} else {
			p++;
			cnt++;
			if (p > bcs->hw.tiger.s_end)
				p = bcs->hw.tiger.send;
			p++;
			cnt++;
			if (p > bcs->hw.tiger.s_end)
				p = bcs->hw.tiger.send;
			write_raw(bcs, p, bcs->hw.tiger.free - cnt);
		}
	} else if (test_and_clear_bit(BC_FLG_EMPTY, &bcs->Flag)) {
		p = bus_to_virt(inl(bcs->cs->hw.njet.base + NETJET_DMA_READ_ADR));
		cnt = bcs->hw.tiger.s_end - p;
		if (cnt < 2) {
			p = bcs->hw.tiger.send + 1;
			cnt = NETJET_DMA_TXSIZE/2 - 2;
		} else {
			p++;
			p++;
			if (cnt <= (NETJET_DMA_TXSIZE/2))
				cnt += NETJET_DMA_TXSIZE/2;
			cnt--;
			cnt--;
		}
		write_raw(bcs, p, cnt);
	}
	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs,"tiger fill_dma3: c%d %4x", bcs->channel,
			bcs->Flag);
}

static void write_raw(struct BCState *bcs, u_int *buf, int cnt) {
	u_int mask, val, *p=buf;
	u_int i, s_cnt;
        
        if (cnt <= 0)
        	return;
	if (test_bit(BC_FLG_BUSY, &bcs->Flag)) {
		if (bcs->hw.tiger.sendcnt> cnt) {
			s_cnt = cnt;
			bcs->hw.tiger.sendcnt -= cnt;
		} else {
			s_cnt = bcs->hw.tiger.sendcnt;
			bcs->hw.tiger.sendcnt = 0;
		}
		if (bcs->channel)
			mask = 0xffff00ff;
		else
			mask = 0xffffff00;
		for (i=0; i<s_cnt; i++) {
			val = bcs->channel ? ((bcs->hw.tiger.sp[i] <<8) & 0xff00) :
				(bcs->hw.tiger.sp[i]);
			*p   &= mask;
			*p++ |= val;
			if (p>bcs->hw.tiger.s_end)
				p = bcs->hw.tiger.send;
		}
		bcs->hw.tiger.s_tot += s_cnt;
		if (bcs->cs->debug & L1_DEB_HSCX)
			debugl1(bcs->cs,"tiger write_raw: c%d %p-%p %d/%d %d %x", bcs->channel,
				buf, p, s_cnt, cnt,
				bcs->hw.tiger.sendcnt, bcs->cs->hw.njet.irqstat0);
		if (bcs->cs->debug & L1_DEB_HSCX_FIFO)
			printframe(bcs->cs, bcs->hw.tiger.sp, s_cnt, "snd");
		bcs->hw.tiger.sp += s_cnt;
		bcs->hw.tiger.sendp = p;
		if (!bcs->hw.tiger.sendcnt) {
			if (!bcs->tx_skb) {
				debugl1(bcs->cs,"tiger write_raw: NULL skb s_cnt %d", s_cnt);
			} else {
				if (test_bit(FLG_LLI_L1WAKEUP,&bcs->st->lli.flag) &&
					(PACKET_NOACK != bcs->tx_skb->pkt_type)) {
					u_long	flags;
					spin_lock_irqsave(&bcs->aclock, flags);
					bcs->ackcnt += bcs->tx_skb->len;
					spin_unlock_irqrestore(&bcs->aclock, flags);
					schedule_event(bcs, B_ACKPENDING);
				}
				dev_kfree_skb_any(bcs->tx_skb);
				bcs->tx_skb = NULL;
			}
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			bcs->hw.tiger.free = cnt - s_cnt;
			if (bcs->hw.tiger.free > (NETJET_DMA_TXSIZE/2))
				test_and_set_bit(BC_FLG_HALF, &bcs->Flag);
			else {
				test_and_clear_bit(BC_FLG_HALF, &bcs->Flag);
				test_and_set_bit(BC_FLG_NOFRAME, &bcs->Flag);
			}
			if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
				netjet_fill_dma(bcs);
			} else {
				mask ^= 0xffffffff;
				if (s_cnt < cnt) {
					for (i=s_cnt; i<cnt;i++) {
						*p++ |= mask;
						if (p>bcs->hw.tiger.s_end)
							p = bcs->hw.tiger.send;
					}
					if (bcs->cs->debug & L1_DEB_HSCX)
						debugl1(bcs->cs, "tiger write_raw: fill rest %d",
							cnt - s_cnt);
				}
				test_and_set_bit(B_XMTBUFREADY, &bcs->event);
				schedule_work(&bcs->tqueue);
			}
		}
	} else if (test_and_clear_bit(BC_FLG_NOFRAME, &bcs->Flag)) {
		test_and_set_bit(BC_FLG_HALF, &bcs->Flag);
		fill_mem(bcs, buf, cnt, bcs->channel, 0xff);
		bcs->hw.tiger.free += cnt;
		if (bcs->cs->debug & L1_DEB_HSCX)
			debugl1(bcs->cs,"tiger write_raw: fill half");
	} else if (test_and_clear_bit(BC_FLG_HALF, &bcs->Flag)) {
		test_and_set_bit(BC_FLG_EMPTY, &bcs->Flag);
		fill_mem(bcs, buf, cnt, bcs->channel, 0xff);
		if (bcs->cs->debug & L1_DEB_HSCX)
			debugl1(bcs->cs,"tiger write_raw: fill full");
	}
}

void write_tiger(struct IsdnCardState *cs) {
	u_int *p, cnt = NETJET_DMA_TXSIZE/2;
	
	if ((cs->hw.njet.irqstat0 & cs->hw.njet.last_is0) & NETJET_IRQM0_WRITE) {
		debugl1(cs,"tiger warn write double dma %x/%x",
			cs->hw.njet.irqstat0, cs->hw.njet.last_is0);
#ifdef ERROR_STATISTIC
		if (cs->bcs[0].mode)
			cs->bcs[0].err_tx++;
		if (cs->bcs[1].mode)
			cs->bcs[1].err_tx++;
#endif
		return;
	} else {
		cs->hw.njet.last_is0 &= ~NETJET_IRQM0_WRITE;
		cs->hw.njet.last_is0 |= (cs->hw.njet.irqstat0 & NETJET_IRQM0_WRITE);
	}	
	if (cs->hw.njet.irqstat0  & NETJET_IRQM0_WRITE_1)
		p = cs->bcs[0].hw.tiger.send + NETJET_DMA_TXSIZE - 1;
	else
		p = cs->bcs[0].hw.tiger.send + cnt - 1;
	if ((cs->bcs[0].mode == L1_MODE_HDLC) || (cs->bcs[0].mode == L1_MODE_HDLC_56K))
		write_raw(cs->bcs, p, cnt);
	if ((cs->bcs[1].mode == L1_MODE_HDLC) || (cs->bcs[1].mode == L1_MODE_HDLC_56K))
		write_raw(cs->bcs + 1, p, cnt);
	cs->hw.njet.irqstat0 &= ~NETJET_IRQM0_WRITE;
}

static void
tiger_l2l1(struct PStack *st, int pr, void *arg)
{
	struct BCState *bcs = st->l1.bcs;
	struct sk_buff *skb = arg;
	u_long flags;

	switch (pr) {
		case (PH_DATA | REQUEST):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			if (bcs->tx_skb) {
				skb_queue_tail(&bcs->squeue, skb);
			} else {
				bcs->tx_skb = skb;
				bcs->cs->BC_Send_Data(bcs);
			}
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			break;
		case (PH_PULL | INDICATION):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			if (bcs->tx_skb) {
				printk(KERN_WARNING "tiger_l2l1: this shouldn't happen\n");
			} else {
				bcs->tx_skb = skb;
				bcs->cs->BC_Send_Data(bcs);
			}
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			break;
		case (PH_PULL | REQUEST):
			if (!bcs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (PH_ACTIVATE | REQUEST):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			test_and_set_bit(BC_FLG_ACTIV, &bcs->Flag);
			mode_tiger(bcs, st->l1.mode, st->l1.bc);
			/* 2001/10/04 Christoph Ersfeld, Formula-n Europe AG */
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			bcs->cs->cardmsg(bcs->cs, MDL_BC_ASSIGN, (void *)(&st->l1.bc));
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			/* 2001/10/04 Christoph Ersfeld, Formula-n Europe AG */
			bcs->cs->cardmsg(bcs->cs, MDL_BC_RELEASE, (void *)(&st->l1.bc));
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			test_and_clear_bit(BC_FLG_ACTIV, &bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			mode_tiger(bcs, 0, st->l1.bc);
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}


static void
close_tigerstate(struct BCState *bcs)
{
	mode_tiger(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.tiger.rcvbuf) {
			kfree(bcs->hw.tiger.rcvbuf);
			bcs->hw.tiger.rcvbuf = NULL;
		}
		if (bcs->hw.tiger.sendbuf) {
			kfree(bcs->hw.tiger.sendbuf);
			bcs->hw.tiger.sendbuf = NULL;
		}
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

static int
open_tigerstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.tiger.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for tiger.rcvbuf\n");
			return (1);
		}
		if (!(bcs->hw.tiger.sendbuf = kmalloc(RAW_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for tiger.sendbuf\n");
			return (1);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	bcs->hw.tiger.sendcnt = 0;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->tx_cnt = 0;
	return (0);
}

static int
setstack_tiger(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_tigerstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = tiger_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

 
void __init
inittiger(struct IsdnCardState *cs)
{
	if (!(cs->bcs[0].hw.tiger.send = kmalloc(NETJET_DMA_TXSIZE * sizeof(unsigned int),
		GFP_KERNEL | GFP_DMA))) {
		printk(KERN_WARNING
		       "HiSax: No memory for tiger.send\n");
		return;
	}
	cs->bcs[0].hw.tiger.s_irq = cs->bcs[0].hw.tiger.send + NETJET_DMA_TXSIZE/2 - 1;
	cs->bcs[0].hw.tiger.s_end = cs->bcs[0].hw.tiger.send + NETJET_DMA_TXSIZE - 1;
	cs->bcs[1].hw.tiger.send = cs->bcs[0].hw.tiger.send;
	cs->bcs[1].hw.tiger.s_irq = cs->bcs[0].hw.tiger.s_irq;
	cs->bcs[1].hw.tiger.s_end = cs->bcs[0].hw.tiger.s_end;
	
	memset(cs->bcs[0].hw.tiger.send, 0xff, NETJET_DMA_TXSIZE * sizeof(unsigned int));
	debugl1(cs, "tiger: send buf %p - %p", cs->bcs[0].hw.tiger.send,
		cs->bcs[0].hw.tiger.send + NETJET_DMA_TXSIZE - 1);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.send),
		cs->hw.njet.base + NETJET_DMA_READ_START);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.s_irq),
		cs->hw.njet.base + NETJET_DMA_READ_IRQ);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.s_end),
		cs->hw.njet.base + NETJET_DMA_READ_END);
	if (!(cs->bcs[0].hw.tiger.rec = kmalloc(NETJET_DMA_RXSIZE * sizeof(unsigned int),
		GFP_KERNEL | GFP_DMA))) {
		printk(KERN_WARNING
		       "HiSax: No memory for tiger.rec\n");
		return;
	}
	debugl1(cs, "tiger: rec buf %p - %p", cs->bcs[0].hw.tiger.rec,
		cs->bcs[0].hw.tiger.rec + NETJET_DMA_RXSIZE - 1);
	cs->bcs[1].hw.tiger.rec = cs->bcs[0].hw.tiger.rec;
	memset(cs->bcs[0].hw.tiger.rec, 0xff, NETJET_DMA_RXSIZE * sizeof(unsigned int));
	outl(virt_to_bus(cs->bcs[0].hw.tiger.rec),
		cs->hw.njet.base + NETJET_DMA_WRITE_START);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.rec + NETJET_DMA_RXSIZE/2 - 1),
		cs->hw.njet.base + NETJET_DMA_WRITE_IRQ);
	outl(virt_to_bus(cs->bcs[0].hw.tiger.rec + NETJET_DMA_RXSIZE - 1),
		cs->hw.njet.base + NETJET_DMA_WRITE_END);
	debugl1(cs, "tiger: dmacfg  %x/%x  pulse=%d",
		inl(cs->hw.njet.base + NETJET_DMA_WRITE_ADR),
		inl(cs->hw.njet.base + NETJET_DMA_READ_ADR),
		bytein(cs->hw.njet.base + NETJET_PULSE_CNT));
	cs->hw.njet.last_is0 = 0;
	cs->bcs[0].BC_SetStack = setstack_tiger;
	cs->bcs[1].BC_SetStack = setstack_tiger;
	cs->bcs[0].BC_Close = close_tigerstate;
	cs->bcs[1].BC_Close = close_tigerstate;
}

static void
releasetiger(struct IsdnCardState *cs)
{
	if (cs->bcs[0].hw.tiger.send) {
		kfree(cs->bcs[0].hw.tiger.send);
		cs->bcs[0].hw.tiger.send = NULL;
	}
	if (cs->bcs[1].hw.tiger.send) {
		cs->bcs[1].hw.tiger.send = NULL;
	}
	if (cs->bcs[0].hw.tiger.rec) {
		kfree(cs->bcs[0].hw.tiger.rec);
		cs->bcs[0].hw.tiger.rec = NULL;
	}
	if (cs->bcs[1].hw.tiger.rec) {
		cs->bcs[1].hw.tiger.rec = NULL;
	}
}

void
release_io_netjet(struct IsdnCardState *cs)
{
	byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0);
	byteout(cs->hw.njet.base + NETJET_IRQMASK1, 0);
	releasetiger(cs);
	release_region(cs->hw.njet.base, 256);
}

