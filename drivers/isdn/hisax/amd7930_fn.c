/* gerdes_amd7930.c,v 0.99 2001/10/02
 *
 * gerdes_amd7930.c     Amd 79C30A and 79C32A specific routines
 *                      (based on HiSax driver by Karsten Keil)
 *
 * Author               Christoph Ersfeld <info@formula-n.de>
 *                      Formula-n Europe AG (www.formula-n.com)
 *                      previously Gerdes AG
 *
 *
 *                      This file is (c) under GNU PUBLIC LICENSE
 *
 *
 * Notes:
 * Version 0.99 is the first release of this driver and there are
 * certainly a few bugs.
 *
 * Please don't report any malfunction to me without sending
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
 * necessary this driver for the used Amd 7930 D-channel-controller
 * was spnsored by Formula-n Europe AG.
 * Thanks to Karsten Keil and Petr Novak, who gave me support in
 * Hisax-specific questions.
 * I want so say special thanks to Carl-Friedrich Braun, who had to
 * answer a lot of questions about generally ISDN and about handling
 * of the Amd-Chip.
 *
 */


#include "hisax.h"
#include "isdnl1.h"
#include "isac.h"
#include "amd7930_fn.h"
#include <linux/interrupt.h>
#include <linux/init.h>

static void Amd7930_new_ph(struct IsdnCardState *cs);

static WORD initAMD[] = {
	0x0100,

	0x00A5, 3, 0x01, 0x40, 0x58,				// LPR, LMR1, LMR2
	0x0086, 1, 0x0B,					// DMR1 (D-Buffer TH-Interrupts on)
	0x0087, 1, 0xFF,					// DMR2
	0x0092, 1, 0x03,					// EFCR (extended mode d-channel-fifo on)
	0x0090, 4, 0xFE, 0xFF, 0x02, 0x0F,			// FRAR4, SRAR4, DMR3, DMR4 (address recognition )
	0x0084, 2, 0x80, 0x00,					// DRLR
	0x00C0, 1, 0x47,					// PPCR1
	0x00C8, 1, 0x01,					// PPCR2

	0x0102,
	0x0107,
	0x01A1, 1,
	0x0121, 1,
	0x0189, 2,

	0x0045, 4, 0x61, 0x72, 0x00, 0x00,			// MCR1, MCR2, MCR3, MCR4
	0x0063, 2, 0x08, 0x08,					// GX
	0x0064, 2, 0x08, 0x08,					// GR
	0x0065, 2, 0x99, 0x00,					// GER
	0x0066, 2, 0x7C, 0x8B,					// STG
	0x0067, 2, 0x00, 0x00,					// FTGR1, FTGR2
	0x0068, 2, 0x20, 0x20,					// ATGR1, ATGR2
	0x0069, 1, 0x4F,					// MMR1
	0x006A, 1, 0x00,					// MMR2
	0x006C, 1, 0x40,					// MMR3
	0x0021, 1, 0x02,					// INIT
	0x00A3, 1, 0x40,					// LMR1

	0xFFFF
};


static void /* macro wWordAMD */
WriteWordAmd7930(struct IsdnCardState *cs, BYTE reg, WORD val)
{
        wByteAMD(cs, 0x00, reg);
        wByteAMD(cs, 0x01, LOBYTE(val));
        wByteAMD(cs, 0x01, HIBYTE(val));
}

static WORD /* macro rWordAMD */
ReadWordAmd7930(struct IsdnCardState *cs, BYTE reg)
{
        WORD res;
        /* direct access register */
        if(reg < 8) {
        	res = rByteAMD(cs, reg);
                res += 256*rByteAMD(cs, reg);
        }
        /* indirect access register */
        else {
                wByteAMD(cs, 0x00, reg);
	        res = rByteAMD(cs, 0x01);
                res += 256*rByteAMD(cs, 0x01);
        }
	return (res);
}


static void
Amd7930_ph_command(struct IsdnCardState *cs, u_char command, char *s)
{
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "AMD7930: %s: ph_command 0x%02X", s, command);

        cs->dc.amd7930.lmr1 = command;
        wByteAMD(cs, 0xA3, command);
}



static BYTE i430States[] = {
// to   reset  F3    F4    F5    F6    F7    F8    AR     from
        0x01, 0x02, 0x00, 0x00, 0x00, 0x07, 0x05, 0x00,   // init
        0x01, 0x02, 0x00, 0x00, 0x00, 0x07, 0x05, 0x00,   // reset
        0x01, 0x02, 0x00, 0x00, 0x00, 0x09, 0x05, 0x04,   // F3
        0x01, 0x02, 0x00, 0x00, 0x1B, 0x00, 0x00, 0x00,   // F4
        0x01, 0x02, 0x00, 0x00, 0x1B, 0x00, 0x00, 0x00,   // F5
        0x01, 0x03, 0x00, 0x00, 0x00, 0x06, 0x05, 0x00,   // F6
        0x11, 0x13, 0x00, 0x00, 0x1B, 0x00, 0x15, 0x00,   // F7
        0x01, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00,   // F8
        0x01, 0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x0A};  // AR


/*                    Row     init    -   reset  F3    F4    F5    F6    F7    F8    AR */
static BYTE stateHelper[] = { 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };




static void
Amd7930_get_state(struct IsdnCardState *cs) {
        BYTE lsr = rByteAMD(cs, 0xA1);
        cs->dc.amd7930.ph_state = (lsr & 0x7) + 2;
        Amd7930_new_ph(cs);
}



static void
Amd7930_new_ph(struct IsdnCardState *cs)
{
        u_char index = stateHelper[cs->dc.amd7930.old_state]*8 + stateHelper[cs->dc.amd7930.ph_state]-1;
        u_char message = i430States[index];

        if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "AMD7930: new_ph %d, old_ph %d, message %d, index %d",
                        cs->dc.amd7930.ph_state, cs->dc.amd7930.old_state, message & 0x0f, index);

        cs->dc.amd7930.old_state = cs->dc.amd7930.ph_state;

        /* abort transmit if nessesary */
        if ((message & 0xf0) && (cs->tx_skb)) {
                wByteAMD(cs, 0x21, 0xC2);
                wByteAMD(cs, 0x21, 0x02);
        }

	switch (message & 0x0f) {

                case (1):
                        l1_msg(cs, HW_RESET | INDICATION, NULL);
                        Amd7930_get_state(cs);
                        break;
                case (2): /* init, Card starts in F3 */
                        l1_msg(cs, HW_DEACTIVATE | CONFIRM, NULL);
                        break;
                case (3):
                        l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
                        break;
                case (4):
                        l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
                        Amd7930_ph_command(cs, 0x50, "HW_ENABLE REQUEST");
                        break;
                case (5):
			l1_msg(cs, HW_RSYNC | INDICATION, NULL);
                        break;
                case (6):
			l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
                        break;
                case (7): /* init, Card starts in F7 */
			l1_msg(cs, HW_RSYNC | INDICATION, NULL);
			l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
                        break;
                case (8):
                        l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
                        /* fall through */
                case (9):
                        Amd7930_ph_command(cs, 0x40, "HW_ENABLE REQ cleared if set");
			l1_msg(cs, HW_RSYNC | INDICATION, NULL);
			l1_msg(cs, HW_INFO2 | INDICATION, NULL);
			l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
                        break;
                case (10):
                        Amd7930_ph_command(cs, 0x40, "T3 expired, HW_ENABLE REQ cleared");
                        cs->dc.amd7930.old_state = 3;
                        break;
                case (11):
			l1_msg(cs, HW_INFO2 | INDICATION, NULL);
                        break;
		default:
			break;
	}
}



static void
Amd7930_bh(struct work_struct *work)
{
	struct IsdnCardState *cs =
		container_of(work, struct IsdnCardState, tqueue);
        struct PStack *stptr;

	if (!cs)
		return;
	if (test_and_clear_bit(D_CLEARBUSY, &cs->event)) {
                if (cs->debug)
			debugl1(cs, "Amd7930: bh, D-Channel Busy cleared");
		stptr = cs->stlist;
		while (stptr != NULL) {
			stptr->l1.l1l2(stptr, PH_PAUSE | CONFIRM, NULL);
			stptr = stptr->next;
		}
	}
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event)) {
	        if (cs->debug & L1_DEB_ISAC)
		        debugl1(cs, "AMD7930: bh, D_L1STATECHANGE");
                Amd7930_new_ph(cs);
        }

        if (test_and_clear_bit(D_RCVBUFREADY, &cs->event)) {
	        if (cs->debug & L1_DEB_ISAC)
		        debugl1(cs, "AMD7930: bh, D_RCVBUFREADY");
                DChannel_proc_rcv(cs);
        }

        if (test_and_clear_bit(D_XMTBUFREADY, &cs->event)) {
	        if (cs->debug & L1_DEB_ISAC)
		        debugl1(cs, "AMD7930: bh, D_XMTBUFREADY");
                DChannel_proc_xmt(cs);
        }
}

static void
Amd7930_empty_Dfifo(struct IsdnCardState *cs, int flag)
{

        BYTE stat, der;
	BYTE *ptr;
	struct sk_buff *skb;


	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "Amd7930: empty_Dfifo");


	ptr = cs->rcvbuf + cs->rcvidx;

	/* AMD interrupts off */
	AmdIrqOff(cs);

	/* read D-Channel-Fifo*/
	stat = rByteAMD(cs, 0x07); // DSR2

		/* while Data in Fifo ... */
		while ( (stat & 2) && ((ptr-cs->rcvbuf) < MAX_DFRAME_LEN_L1) ) {
			*ptr = rByteAMD(cs, 0x04); // DCRB
			ptr++;
	                stat = rByteAMD(cs, 0x07); // DSR2
			cs->rcvidx = ptr - cs->rcvbuf;

                        /* Paket ready? */
			if (stat & 1) {

                                der = rWordAMD(cs, 0x03);

                                /* no errors, packet ok */
                                if(!der && !flag) {
					rWordAMD(cs, 0x89); // clear DRCR

                                        if ((cs->rcvidx) > 0) {
                                                if (!(skb = alloc_skb(cs->rcvidx, GFP_ATOMIC)))
							printk(KERN_WARNING "HiSax: Amd7930: empty_Dfifo, D receive out of memory!\n");
						else {
                                                        /* Debugging */
                                                        if (cs->debug & L1_DEB_ISAC_FIFO) {
								char *t = cs->dlog;

								t += sprintf(t, "Amd7930: empty_Dfifo cnt: %d |", cs->rcvidx);
								QuickHex(t, cs->rcvbuf, cs->rcvidx);
								debugl1(cs, cs->dlog);
							}
                                                        /* moves received data in sk-buffer */
							memcpy(skb_put(skb, cs->rcvidx), cs->rcvbuf, cs->rcvidx);
							skb_queue_tail(&cs->rq, skb);
						}
					}

				}
                                /* throw damaged packets away, reset receive-buffer, indicate RX */
				ptr = cs->rcvbuf;
				cs->rcvidx = 0;
				schedule_event(cs, D_RCVBUFREADY);
			}
                }
		/* Packet to long, overflow */
		if(cs->rcvidx >= MAX_DFRAME_LEN_L1) {
			if (cs->debug & L1_DEB_WARN)
			        debugl1(cs, "AMD7930: empty_Dfifo L2-Framelength overrun");
			cs->rcvidx = 0;
			return;
		}
	/* AMD interrupts on */
	AmdIrqOn(cs);
}


static void
Amd7930_fill_Dfifo(struct IsdnCardState *cs)
{

        WORD dtcrr, dtcrw, len, count;
        BYTE txstat, dmr3;
        BYTE *ptr, *deb_ptr;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "Amd7930: fill_Dfifo");

	if ((!cs->tx_skb) || (cs->tx_skb->len <= 0))
		return;

        dtcrw = 0;
        if(!cs->dc.amd7930.tx_xmtlen)
                /* new Frame */
                len = dtcrw = cs->tx_skb->len;
        /* continue frame */
        else len = cs->dc.amd7930.tx_xmtlen;


	/* AMD interrupts off */
	AmdIrqOff(cs);

        deb_ptr = ptr = cs->tx_skb->data;

        /* while free place in tx-fifo available and data in sk-buffer */
        txstat = 0x10;
        while((txstat & 0x10) && (cs->tx_cnt < len)) {
                wByteAMD(cs, 0x04, *ptr);
                ptr++;
                cs->tx_cnt++;
                txstat= rByteAMD(cs, 0x07);
        }
        count = ptr - cs->tx_skb->data;
	skb_pull(cs->tx_skb, count);


        dtcrr = rWordAMD(cs, 0x85); // DTCR
        dmr3  = rByteAMD(cs, 0x8E);

	if (cs->debug & L1_DEB_ISAC) {
		debugl1(cs, "Amd7930: fill_Dfifo, DMR3: 0x%02X, DTCR read: 0x%04X write: 0x%02X 0x%02X", dmr3, dtcrr, LOBYTE(dtcrw), HIBYTE(dtcrw));
        }

        /* writeing of dtcrw starts transmit */
        if(!cs->dc.amd7930.tx_xmtlen) {
                wWordAMD(cs, 0x85, dtcrw);
                cs->dc.amd7930.tx_xmtlen = dtcrw;
        }

	if (test_and_set_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		debugl1(cs, "Amd7930: fill_Dfifo dbusytimer running");
		del_timer(&cs->dbusytimer);
	}
	init_timer(&cs->dbusytimer);
	cs->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ) / 1000);
	add_timer(&cs->dbusytimer);

	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char *t = cs->dlog;

		t += sprintf(t, "Amd7930: fill_Dfifo cnt: %d |", count);
		QuickHex(t, deb_ptr, count);
		debugl1(cs, cs->dlog);
	}
	/* AMD interrupts on */
        AmdIrqOn(cs);
}


void Amd7930_interrupt(struct IsdnCardState *cs, BYTE irflags)
{
	BYTE dsr1, dsr2, lsr;
        WORD der;

 while (irflags)
 {

        dsr1 = rByteAMD(cs, 0x02);
        der  = rWordAMD(cs, 0x03);
        dsr2 = rByteAMD(cs, 0x07);
        lsr  = rByteAMD(cs, 0xA1);

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "Amd7930: interrupt: flags: 0x%02X, DSR1: 0x%02X, DSR2: 0x%02X, LSR: 0x%02X, DER=0x%04X", irflags, dsr1, dsr2, lsr, der);

        /* D error -> read DER and DSR2 bit 2 */
	if (der || (dsr2 & 4)) {

                if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "Amd7930: interrupt: D error DER=0x%04X", der);

                /* RX, TX abort if collision detected */
                if (der & 2) {
                        wByteAMD(cs, 0x21, 0xC2);
                        wByteAMD(cs, 0x21, 0x02);
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				schedule_event(cs, D_CLEARBUSY);
                        /* restart frame */
                        if (cs->tx_skb) {
				skb_push(cs->tx_skb, cs->tx_cnt);
				cs->tx_cnt = 0;
                                cs->dc.amd7930.tx_xmtlen = 0;
				Amd7930_fill_Dfifo(cs);
			} else {
				printk(KERN_WARNING "HiSax: Amd7930 D-Collision, no skb\n");
				debugl1(cs, "Amd7930: interrupt: D-Collision, no skb");
			}
                }
                /* remove damaged data from fifo */
		Amd7930_empty_Dfifo(cs, 1);

		if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
			del_timer(&cs->dbusytimer);
		if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			schedule_event(cs, D_CLEARBUSY);
                /* restart TX-Frame */
                if (cs->tx_skb) {
			skb_push(cs->tx_skb, cs->tx_cnt);
			cs->tx_cnt = 0;
                        cs->dc.amd7930.tx_xmtlen = 0;
			Amd7930_fill_Dfifo(cs);
		}
	}

        /* D TX FIFO empty -> fill */
	if (irflags & 1) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "Amd7930: interrupt: clear Timer and fill D-TX-FIFO if data");

		/* AMD interrupts off */
                AmdIrqOff(cs);

                if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
			del_timer(&cs->dbusytimer);
		if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			schedule_event(cs, D_CLEARBUSY);
		if (cs->tx_skb) {
			if (cs->tx_skb->len)
				Amd7930_fill_Dfifo(cs);
		}
		/* AMD interrupts on */
                AmdIrqOn(cs);
	}


        /* D RX FIFO full or tiny packet in Fifo -> empty */
	if ((irflags & 2) || (dsr1 & 2)) {
                if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "Amd7930: interrupt: empty D-FIFO");
                Amd7930_empty_Dfifo(cs, 0);
	}


        /* D-Frame transmit complete */
	if (dsr1 & 64) {
		if (cs->debug & L1_DEB_ISAC) {
			debugl1(cs, "Amd7930: interrupt: transmit packet ready");
        	}
		/* AMD interrupts off */
                AmdIrqOff(cs);

                if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
			del_timer(&cs->dbusytimer);
		if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			schedule_event(cs, D_CLEARBUSY);

                if (cs->tx_skb) {
        		if (cs->debug & L1_DEB_ISAC)
	        		debugl1(cs, "Amd7930: interrupt: TX-Packet ready, freeing skb");
                        dev_kfree_skb_irq(cs->tx_skb);
			cs->tx_cnt = 0;
                        cs->dc.amd7930.tx_xmtlen=0;
			cs->tx_skb = NULL;
                }
                if ((cs->tx_skb = skb_dequeue(&cs->sq))) {
        		if (cs->debug & L1_DEB_ISAC)
	        		debugl1(cs, "Amd7930: interrupt: TX-Packet ready, next packet dequeued");
	        	cs->tx_cnt = 0;
                        cs->dc.amd7930.tx_xmtlen=0;
			Amd7930_fill_Dfifo(cs);
		}
                else
			schedule_event(cs, D_XMTBUFREADY);
		/* AMD interrupts on */
                AmdIrqOn(cs);
        }

	/* LIU status interrupt -> read LSR, check statechanges */
	if (lsr & 0x38) {
                /* AMD interrupts off */
                AmdIrqOff(cs);

		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "Amd: interrupt: LSR=0x%02X, LIU is in state %d", lsr, ((lsr & 0x7) +2));

		cs->dc.amd7930.ph_state = (lsr & 0x7) + 2;

		schedule_event(cs, D_L1STATECHANGE);
		/* AMD interrupts on */
                AmdIrqOn(cs);
	}

        /* reads Interrupt-Register again. If there is a new interrupt-flag: restart handler */
        irflags = rByteAMD(cs, 0x00);
 }

}

static void
Amd7930_l1hw(struct PStack *st, int pr, void *arg)
{
        struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	u_long flags;

        if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "Amd7930: l1hw called, pr: 0x%04X", pr);

	switch (pr) {
		case (PH_DATA | REQUEST):
                        if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			spin_lock_irqsave(&cs->lock, flags);
			if (cs->tx_skb) {
				skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "Amd7930: l1hw: PH_DATA Queued", 0);
#endif
			} else {
				cs->tx_skb = skb;
				cs->tx_cnt = 0;
                                cs->dc.amd7930.tx_xmtlen=0;
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "Amd7930: l1hw: PH_DATA", 0);
#endif
				Amd7930_fill_Dfifo(cs);
			}
			spin_unlock_irqrestore(&cs->lock, flags);
			break;
		case (PH_PULL | INDICATION):
			spin_lock_irqsave(&cs->lock, flags);
			if (cs->tx_skb) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "Amd7930: l1hw: l2l1 tx_skb exist this shouldn't happen");
				skb_queue_tail(&cs->sq, skb);
				break;
			}
			if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
                        cs->dc.amd7930.tx_xmtlen=0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "Amd7930: l1hw: PH_DATA_PULLED", 0);
#endif
			Amd7930_fill_Dfifo(cs);
			spin_unlock_irqrestore(&cs->lock, flags);
			break;
		case (PH_PULL | REQUEST):
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				debugl1(cs, "Amd7930: l1hw: -> PH_REQUEST_PULL, skb: %s", (cs->tx_skb)? "yes":"no");
#endif
			if (!cs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (HW_RESET | REQUEST):
			spin_lock_irqsave(&cs->lock, flags);
			if ((cs->dc.amd7930.ph_state == 8)) {
				/* b-channels off, PH-AR cleared
				 * change to F3 */
				Amd7930_ph_command(cs, 0x20, "HW_RESET REQEST"); //LMR1 bit 5
				spin_unlock_irqrestore(&cs->lock, flags);
			} else {
				Amd7930_ph_command(cs, 0x40, "HW_RESET REQUEST");
				cs->dc.amd7930.ph_state = 2;
				spin_unlock_irqrestore(&cs->lock, flags);
				Amd7930_new_ph(cs);
			}
			break;
		case (HW_ENABLE | REQUEST):
                        cs->dc.amd7930.ph_state = 9;
                        Amd7930_new_ph(cs);
			break;
		case (HW_INFO3 | REQUEST):
			// automatic
			break;
		case (HW_TESTLOOP | REQUEST):
			/* not implemented yet */
			break;
		case (HW_DEACTIVATE | RESPONSE):
                        skb_queue_purge(&cs->rq);
			skb_queue_purge(&cs->sq);
			if (cs->tx_skb) {
				dev_kfree_skb(cs->tx_skb);
				cs->tx_skb = NULL;
			}
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				schedule_event(cs, D_CLEARBUSY);
			break;
		default:
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "Amd7930: l1hw: unknown %04x", pr);
			break;
	}
}

static void
setstack_Amd7930(struct PStack *st, struct IsdnCardState *cs)
{

        if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "Amd7930: setstack called");

        st->l1.l1hw = Amd7930_l1hw;
}


static void
DC_Close_Amd7930(struct IsdnCardState *cs) {
        if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "Amd7930: DC_Close called");
}


static void
dbusy_timer_handler(struct IsdnCardState *cs)
{
	u_long flags;
	struct PStack *stptr;
        WORD dtcr, der;
        BYTE dsr1, dsr2;


        if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "Amd7930: dbusy_timer expired!");

	if (test_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		spin_lock_irqsave(&cs->lock, flags);
                /* D Transmit Byte Count Register:
                 * Counts down packet's number of Bytes, 0 if packet ready */
                dtcr = rWordAMD(cs, 0x85);
                dsr1 = rByteAMD(cs, 0x02);
                dsr2 = rByteAMD(cs, 0x07);
                der  = rWordAMD(cs, 0x03);

	        if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "Amd7930: dbusy_timer_handler: DSR1=0x%02X, DSR2=0x%02X, DER=0x%04X, cs->tx_skb->len=%u, tx_stat=%u, dtcr=%u, cs->tx_cnt=%u", dsr1, dsr2, der, cs->tx_skb->len, cs->dc.amd7930.tx_xmtlen, dtcr, cs->tx_cnt);

		if ((cs->dc.amd7930.tx_xmtlen - dtcr) < cs->tx_cnt) {	/* D-Channel Busy */
			test_and_set_bit(FLG_L1_DBUSY, &cs->HW_Flags);
			stptr = cs->stlist;
			spin_unlock_irqrestore(&cs->lock, flags);
			while (stptr != NULL) {
				stptr->l1.l1l2(stptr, PH_PAUSE | INDICATION, NULL);
				stptr = stptr->next;
			}

		} else {
			/* discard frame; reset transceiver */
			test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags);
			if (cs->tx_skb) {
				dev_kfree_skb_any(cs->tx_skb);
				cs->tx_cnt = 0;
				cs->tx_skb = NULL;
                                cs->dc.amd7930.tx_xmtlen = 0;
			} else {
				printk(KERN_WARNING "HiSax: Amd7930: D-Channel Busy no skb\n");
				debugl1(cs, "Amd7930: D-Channel Busy no skb");

			}
			/* Transmitter reset, abort transmit */
			wByteAMD(cs, 0x21, 0x82);
			wByteAMD(cs, 0x21, 0x02);
			spin_unlock_irqrestore(&cs->lock, flags);
			cs->irq_func(cs->irq, cs);

                        if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "Amd7930: dbusy_timer_handler: Transmitter reset");
		}
	}
}



void __devinit
Amd7930_init(struct IsdnCardState *cs)
{
    WORD *ptr;
    BYTE cmd, cnt;

        if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "Amd7930: initamd called");

        cs->dc.amd7930.tx_xmtlen = 0;
        cs->dc.amd7930.old_state = 0;
        cs->dc.amd7930.lmr1 = 0x40;
        cs->dc.amd7930.ph_command = Amd7930_ph_command;
	cs->setstack_d = setstack_Amd7930;
	cs->DC_Close = DC_Close_Amd7930;

	/* AMD Initialisation */
	for (ptr = initAMD; *ptr != 0xFFFF; ) {
		cmd = LOBYTE(*ptr);

                /* read */
                if (*ptr++ >= 0x100) {
			if (cmd < 8)
                                /* reset register */
                                rByteAMD(cs, cmd);
			else {
				wByteAMD(cs, 0x00, cmd);
				for (cnt = *ptr++; cnt > 0; cnt--)
					rByteAMD(cs, 0x01);
			}
		}
                /* write */
                else if (cmd < 8)
			wByteAMD(cs, cmd, LOBYTE(*ptr++));

		else {
			wByteAMD(cs, 0x00, cmd);
			for (cnt = *ptr++; cnt > 0; cnt--)
				wByteAMD(cs, 0x01, LOBYTE(*ptr++));
		}
	}
}

void __devinit
setup_Amd7930(struct IsdnCardState *cs)
{
        INIT_WORK(&cs->tqueue, Amd7930_bh);
	cs->dbusytimer.function = (void *) dbusy_timer_handler;
	cs->dbusytimer.data = (long) cs;
	init_timer(&cs->dbusytimer);
}
