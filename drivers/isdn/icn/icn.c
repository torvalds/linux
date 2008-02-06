/* $Id: icn.c,v 1.65.6.8 2001/09/23 22:24:55 kai Exp $
 *
 * ISDN low-level module for the ICN active ISDN-Card.
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@isdn4linux.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include "icn.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

static int portbase = ICN_BASEADDR;
static unsigned long membase = ICN_MEMADDR;
static char *icn_id = "\0";
static char *icn_id2 = "\0";

MODULE_DESCRIPTION("ISDN4Linux: Driver for ICN active ISDN card");
MODULE_AUTHOR("Fritz Elfert");
MODULE_LICENSE("GPL");
module_param(portbase, int, 0);
MODULE_PARM_DESC(portbase, "Port address of first card");
module_param(membase, ulong, 0);
MODULE_PARM_DESC(membase, "Shared memory address of all cards");
module_param(icn_id, charp, 0);
MODULE_PARM_DESC(icn_id, "ID-String of first card");
module_param(icn_id2, charp, 0);
MODULE_PARM_DESC(icn_id2, "ID-String of first card, second S0 (4B only)");

/*
 * Verbose bootcode- and protocol-downloading.
 */
#undef BOOT_DEBUG

/*
 * Verbose Shmem-Mapping.
 */
#undef MAP_DEBUG

static char
*revision = "$Revision: 1.65.6.8 $";

static int icn_addcard(int, char *, char *);

/*
 * Free send-queue completely.
 * Parameter:
 *   card   = pointer to card struct
 *   channel = channel number
 */
static void
icn_free_queue(icn_card * card, int channel)
{
	struct sk_buff_head *queue = &card->spqueue[channel];
	struct sk_buff *skb;

	skb_queue_purge(queue);
	card->xlen[channel] = 0;
	card->sndcount[channel] = 0;
	if ((skb = card->xskb[channel])) {
		card->xskb[channel] = NULL;
		dev_kfree_skb(skb);
	}
}

/* Put a value into a shift-register, highest bit first.
 * Parameters:
 *            port     = port for output (bit 0 is significant)
 *            val      = value to be output
 *            firstbit = Bit-Number of highest bit
 *            bitcount = Number of bits to output
 */
static inline void
icn_shiftout(unsigned short port,
	     unsigned long val,
	     int firstbit,
	     int bitcount)
{

	register u_char s;
	register u_char c;

	for (s = firstbit, c = bitcount; c > 0; s--, c--)
		OUTB_P((u_char) ((val >> s) & 1) ? 0xff : 0, port);
}

/*
 * disable a cards shared memory
 */
static inline void
icn_disable_ram(icn_card * card)
{
	OUTB_P(0, ICN_MAPRAM);
}

/*
 * enable a cards shared memory
 */
static inline void
icn_enable_ram(icn_card * card)
{
	OUTB_P(0xff, ICN_MAPRAM);
}

/*
 * Map a cards channel0 (Bank0/Bank8) or channel1 (Bank4/Bank12)
 *
 * must called with holding the devlock
 */
static inline void
icn_map_channel(icn_card * card, int channel)
{
#ifdef MAP_DEBUG
	printk(KERN_DEBUG "icn_map_channel %d %d\n", dev.channel, channel);
#endif
	if ((channel == dev.channel) && (card == dev.mcard))
		return;
	if (dev.mcard)
		icn_disable_ram(dev.mcard);
	icn_shiftout(ICN_BANK, chan2bank[channel], 3, 4);	/* Select Bank          */
	icn_enable_ram(card);
	dev.mcard = card;
	dev.channel = channel;
#ifdef MAP_DEBUG
	printk(KERN_DEBUG "icn_map_channel done\n");
#endif
}

/*
 * Lock a cards channel.
 * Return 0 if requested card/channel is unmapped (failure).
 * Return 1 on success.
 *
 * must called with holding the devlock
 */
static inline int
icn_lock_channel(icn_card * card, int channel)
{
	register int retval;

#ifdef MAP_DEBUG
	printk(KERN_DEBUG "icn_lock_channel %d\n", channel);
#endif
	if ((dev.channel == channel) && (card == dev.mcard)) {
		dev.chanlock++;
		retval = 1;
#ifdef MAP_DEBUG
		printk(KERN_DEBUG "icn_lock_channel %d OK\n", channel);
#endif
	} else {
		retval = 0;
#ifdef MAP_DEBUG
		printk(KERN_DEBUG "icn_lock_channel %d FAILED, dc=%d\n", channel, dev.channel);
#endif
	}
	return retval;
}

/*
 * Release current card/channel lock
 *
 * must called with holding the devlock
 */
static inline void
__icn_release_channel(void)
{
#ifdef MAP_DEBUG
	printk(KERN_DEBUG "icn_release_channel l=%d\n", dev.chanlock);
#endif
	if (dev.chanlock > 0)
		dev.chanlock--;
}

/*
 * Release current card/channel lock
 */
static inline void
icn_release_channel(void)
{
	ulong flags;

	spin_lock_irqsave(&dev.devlock, flags);
	__icn_release_channel();
	spin_unlock_irqrestore(&dev.devlock, flags);
}

/*
 * Try to map and lock a cards channel.
 * Return 1 on success, 0 on failure.
 */
static inline int
icn_trymaplock_channel(icn_card * card, int channel)
{
	ulong flags;

#ifdef MAP_DEBUG
	printk(KERN_DEBUG "trymaplock c=%d dc=%d l=%d\n", channel, dev.channel,
	       dev.chanlock);
#endif
	spin_lock_irqsave(&dev.devlock, flags);
	if ((!dev.chanlock) ||
	    ((dev.channel == channel) && (dev.mcard == card))) {
		dev.chanlock++;
		icn_map_channel(card, channel);
		spin_unlock_irqrestore(&dev.devlock, flags);
#ifdef MAP_DEBUG
		printk(KERN_DEBUG "trymaplock %d OK\n", channel);
#endif
		return 1;
	}
	spin_unlock_irqrestore(&dev.devlock, flags);
#ifdef MAP_DEBUG
	printk(KERN_DEBUG "trymaplock %d FAILED\n", channel);
#endif
	return 0;
}

/*
 * Release current card/channel lock,
 * then map same or other channel without locking.
 */
static inline void
icn_maprelease_channel(icn_card * card, int channel)
{
	ulong flags;

#ifdef MAP_DEBUG
	printk(KERN_DEBUG "map_release c=%d l=%d\n", channel, dev.chanlock);
#endif
	spin_lock_irqsave(&dev.devlock, flags);
	if (dev.chanlock > 0)
		dev.chanlock--;
	if (!dev.chanlock)
		icn_map_channel(card, channel);
	spin_unlock_irqrestore(&dev.devlock, flags);
}

/* Get Data from the B-Channel, assemble fragmented packets and put them
 * into receive-queue. Wake up any B-Channel-reading processes.
 * This routine is called via timer-callback from icn_pollbchan().
 */

static void
icn_pollbchan_receive(int channel, icn_card * card)
{
	int mch = channel + ((card->secondhalf) ? 2 : 0);
	int eflag;
	int cnt;
	struct sk_buff *skb;

	if (icn_trymaplock_channel(card, mch)) {
		while (rbavl) {
			cnt = readb(&rbuf_l);
			if ((card->rcvidx[channel] + cnt) > 4000) {
				printk(KERN_WARNING
				       "icn: (%s) bogus packet on ch%d, dropping.\n",
				       CID,
				       channel + 1);
				card->rcvidx[channel] = 0;
				eflag = 0;
			} else {
				memcpy_fromio(&card->rcvbuf[channel][card->rcvidx[channel]],
					      &rbuf_d, cnt);
				card->rcvidx[channel] += cnt;
				eflag = readb(&rbuf_f);
			}
			rbnext;
			icn_maprelease_channel(card, mch & 2);
			if (!eflag) {
				if ((cnt = card->rcvidx[channel])) {
					if (!(skb = dev_alloc_skb(cnt))) {
						printk(KERN_WARNING "icn: receive out of memory\n");
						break;
					}
					memcpy(skb_put(skb, cnt), card->rcvbuf[channel], cnt);
					card->rcvidx[channel] = 0;
					card->interface.rcvcallb_skb(card->myid, channel, skb);
				}
			}
			if (!icn_trymaplock_channel(card, mch))
				break;
		}
		icn_maprelease_channel(card, mch & 2);
	}
}

/* Send data-packet to B-Channel, split it up into fragments of
 * ICN_FRAGSIZE length. If last fragment is sent out, signal
 * success to upper layers via statcallb with ISDN_STAT_BSENT argument.
 * This routine is called via timer-callback from icn_pollbchan() or
 * directly from icn_sendbuf().
 */

static void
icn_pollbchan_send(int channel, icn_card * card)
{
	int mch = channel + ((card->secondhalf) ? 2 : 0);
	int cnt;
	unsigned long flags;
	struct sk_buff *skb;
	isdn_ctrl cmd;

	if (!(card->sndcount[channel] || card->xskb[channel] ||
	      !skb_queue_empty(&card->spqueue[channel])))
		return;
	if (icn_trymaplock_channel(card, mch)) {
		while (sbfree && 
		       (card->sndcount[channel] ||
			!skb_queue_empty(&card->spqueue[channel]) ||
			card->xskb[channel])) {
			spin_lock_irqsave(&card->lock, flags);
			if (card->xmit_lock[channel]) {
				spin_unlock_irqrestore(&card->lock, flags);
				break;
			}
			card->xmit_lock[channel]++;
			spin_unlock_irqrestore(&card->lock, flags);
			skb = card->xskb[channel];
			if (!skb) {
				skb = skb_dequeue(&card->spqueue[channel]);
				if (skb) {
					/* Pop ACK-flag off skb.
					 * Store length to xlen.
					 */
					if (*(skb_pull(skb,1)))
						card->xlen[channel] = skb->len;
					else
						card->xlen[channel] = 0;
				}
			}
			if (!skb)
				break;
			if (skb->len > ICN_FRAGSIZE) {
				writeb(0xff, &sbuf_f);
				cnt = ICN_FRAGSIZE;
			} else {
				writeb(0x0, &sbuf_f);
				cnt = skb->len;
			}
			writeb(cnt, &sbuf_l);
			memcpy_toio(&sbuf_d, skb->data, cnt);
			skb_pull(skb, cnt);
			sbnext; /* switch to next buffer        */
			icn_maprelease_channel(card, mch & 2);
			spin_lock_irqsave(&card->lock, flags);
			card->sndcount[channel] -= cnt;
			if (!skb->len) {
				if (card->xskb[channel])
					card->xskb[channel] = NULL;
				card->xmit_lock[channel] = 0;
				spin_unlock_irqrestore(&card->lock, flags);
				dev_kfree_skb(skb);
				if (card->xlen[channel]) {
					cmd.command = ISDN_STAT_BSENT;
					cmd.driver = card->myid;
					cmd.arg = channel;
					cmd.parm.length = card->xlen[channel];
					card->interface.statcallb(&cmd);
				}
			} else {
				card->xskb[channel] = skb;
				card->xmit_lock[channel] = 0;
				spin_unlock_irqrestore(&card->lock, flags);
			}
			if (!icn_trymaplock_channel(card, mch))
				break;
		}
		icn_maprelease_channel(card, mch & 2);
	}
}

/* Send/Receive Data to/from the B-Channel.
 * This routine is called via timer-callback.
 * It schedules itself while any B-Channel is open.
 */

static void
icn_pollbchan(unsigned long data)
{
	icn_card *card = (icn_card *) data;
	unsigned long flags;

	if (card->flags & ICN_FLAGS_B1ACTIVE) {
		icn_pollbchan_receive(0, card);
		icn_pollbchan_send(0, card);
	}
	if (card->flags & ICN_FLAGS_B2ACTIVE) {
		icn_pollbchan_receive(1, card);
		icn_pollbchan_send(1, card);
	}
	if (card->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE)) {
		/* schedule b-channel polling again */
		spin_lock_irqsave(&card->lock, flags);
		mod_timer(&card->rb_timer, jiffies+ICN_TIMER_BCREAD);
		card->flags |= ICN_FLAGS_RBTIMER;
		spin_unlock_irqrestore(&card->lock, flags);
	} else
		card->flags &= ~ICN_FLAGS_RBTIMER;
}

typedef struct icn_stat {
	char *statstr;
	int command;
	int action;
} icn_stat;
/* *INDENT-OFF* */
static icn_stat icn_stat_table[] =
{
	{"BCON_",          ISDN_STAT_BCONN, 1},	/* B-Channel connected        */
	{"BDIS_",          ISDN_STAT_BHUP,  2},	/* B-Channel disconnected     */
	/*
	** add d-channel connect and disconnect support to link-level
	*/
	{"DCON_",          ISDN_STAT_DCONN, 10},	/* D-Channel connected        */
	{"DDIS_",          ISDN_STAT_DHUP,  11},	/* D-Channel disconnected     */
	{"DCAL_I",         ISDN_STAT_ICALL, 3},	/* Incoming call dialup-line  */
	{"DSCA_I",         ISDN_STAT_ICALL, 3},	/* Incoming call 1TR6-SPV     */
	{"FCALL",          ISDN_STAT_ICALL, 4},	/* Leased line connection up  */
	{"CIF",            ISDN_STAT_CINF,  5},	/* Charge-info, 1TR6-type     */
	{"AOC",            ISDN_STAT_CINF,  6},	/* Charge-info, DSS1-type     */
	{"CAU",            ISDN_STAT_CAUSE, 7},	/* Cause code                 */
	{"TEI OK",         ISDN_STAT_RUN,   0},	/* Card connected to wallplug */
	{"E_L1: ACT FAIL", ISDN_STAT_BHUP,  8},	/* Layer-1 activation failed  */
	{"E_L2: DATA LIN", ISDN_STAT_BHUP,  8},	/* Layer-2 data link lost     */
	{"E_L1: ACTIVATION FAILED",
					   ISDN_STAT_BHUP,  8},	/* Layer-1 activation failed  */
	{NULL, 0, -1}
};
/* *INDENT-ON* */


/*
 * Check Statusqueue-Pointer from isdn-cards.
 * If there are new status-replies from the interface, check
 * them against B-Channel-connects/disconnects and set flags accordingly.
 * Wake-Up any processes, who are reading the status-device.
 * If there are B-Channels open, initiate a timer-callback to
 * icn_pollbchan().
 * This routine is called periodically via timer.
 */

static void
icn_parse_status(u_char * status, int channel, icn_card * card)
{
	icn_stat *s = icn_stat_table;
	int action = -1;
	unsigned long flags;
	isdn_ctrl cmd;

	while (s->statstr) {
		if (!strncmp(status, s->statstr, strlen(s->statstr))) {
			cmd.command = s->command;
			action = s->action;
			break;
		}
		s++;
	}
	if (action == -1)
		return;
	cmd.driver = card->myid;
	cmd.arg = channel;
	switch (action) {
		case 11:
			spin_lock_irqsave(&card->lock, flags);
			icn_free_queue(card,channel);
			card->rcvidx[channel] = 0;

			if (card->flags & 
			    ((channel)?ICN_FLAGS_B2ACTIVE:ICN_FLAGS_B1ACTIVE)) {
				
				isdn_ctrl ncmd;
				
				card->flags &= ~((channel)?
						 ICN_FLAGS_B2ACTIVE:ICN_FLAGS_B1ACTIVE);
				
				memset(&ncmd, 0, sizeof(ncmd));
				
				ncmd.driver = card->myid;
				ncmd.arg = channel;
				ncmd.command = ISDN_STAT_BHUP;
				spin_unlock_irqrestore(&card->lock, flags);
				card->interface.statcallb(&cmd);
			} else
				spin_unlock_irqrestore(&card->lock, flags);
			break;
		case 1:
			spin_lock_irqsave(&card->lock, flags);
			icn_free_queue(card,channel);
			card->flags |= (channel) ?
			    ICN_FLAGS_B2ACTIVE : ICN_FLAGS_B1ACTIVE;
			spin_unlock_irqrestore(&card->lock, flags);
			break;
		case 2:
			spin_lock_irqsave(&card->lock, flags);
			card->flags &= ~((channel) ?
				ICN_FLAGS_B2ACTIVE : ICN_FLAGS_B1ACTIVE);
			icn_free_queue(card, channel);
			card->rcvidx[channel] = 0;
			spin_unlock_irqrestore(&card->lock, flags);
			break;
		case 3:
			{
				char *t = status + 6;
				char *s = strchr(t, ',');

				*s++ = '\0';
				strlcpy(cmd.parm.setup.phone, t,
					sizeof(cmd.parm.setup.phone));
				s = strchr(t = s, ',');
				*s++ = '\0';
				if (!strlen(t))
					cmd.parm.setup.si1 = 0;
				else
					cmd.parm.setup.si1 =
					    simple_strtoul(t, NULL, 10);
				s = strchr(t = s, ',');
				*s++ = '\0';
				if (!strlen(t))
					cmd.parm.setup.si2 = 0;
				else
					cmd.parm.setup.si2 =
					    simple_strtoul(t, NULL, 10);
				strlcpy(cmd.parm.setup.eazmsn, s,
					sizeof(cmd.parm.setup.eazmsn));
			}
			cmd.parm.setup.plan = 0;
			cmd.parm.setup.screen = 0;
			break;
		case 4:
			sprintf(cmd.parm.setup.phone, "LEASED%d", card->myid);
			sprintf(cmd.parm.setup.eazmsn, "%d", channel + 1);
			cmd.parm.setup.si1 = 7;
			cmd.parm.setup.si2 = 0;
			cmd.parm.setup.plan = 0;
			cmd.parm.setup.screen = 0;
			break;
		case 5:
			strlcpy(cmd.parm.num, status + 3, sizeof(cmd.parm.num));
			break;
		case 6:
			snprintf(cmd.parm.num, sizeof(cmd.parm.num), "%d",
			     (int) simple_strtoul(status + 7, NULL, 16));
			break;
		case 7:
			status += 3;
			if (strlen(status) == 4)
				snprintf(cmd.parm.num, sizeof(cmd.parm.num), "%s%c%c",
				     status + 2, *status, *(status + 1));
			else
				strlcpy(cmd.parm.num, status + 1, sizeof(cmd.parm.num));
			break;
		case 8:
			spin_lock_irqsave(&card->lock, flags);
			card->flags &= ~ICN_FLAGS_B1ACTIVE;
			icn_free_queue(card, 0);
			card->rcvidx[0] = 0;
			spin_unlock_irqrestore(&card->lock, flags);
			cmd.arg = 0;
			cmd.driver = card->myid;
			card->interface.statcallb(&cmd);
			cmd.command = ISDN_STAT_DHUP;
			cmd.arg = 0;
			cmd.driver = card->myid;
			card->interface.statcallb(&cmd);
			cmd.command = ISDN_STAT_BHUP;
			spin_lock_irqsave(&card->lock, flags);
			card->flags &= ~ICN_FLAGS_B2ACTIVE;
			icn_free_queue(card, 1);
			card->rcvidx[1] = 0;
			spin_unlock_irqrestore(&card->lock, flags);
			cmd.arg = 1;
			cmd.driver = card->myid;
			card->interface.statcallb(&cmd);
			cmd.command = ISDN_STAT_DHUP;
			cmd.arg = 1;
			cmd.driver = card->myid;
			break;
	}
	card->interface.statcallb(&cmd);
	return;
}

static void
icn_putmsg(icn_card * card, unsigned char c)
{
	ulong flags;

	spin_lock_irqsave(&card->lock, flags);
	*card->msg_buf_write++ = (c == 0xff) ? '\n' : c;
	if (card->msg_buf_write == card->msg_buf_read) {
		if (++card->msg_buf_read > card->msg_buf_end)
			card->msg_buf_read = card->msg_buf;
	}
	if (card->msg_buf_write > card->msg_buf_end)
		card->msg_buf_write = card->msg_buf;
	spin_unlock_irqrestore(&card->lock, flags);
}

static void
icn_polldchan(unsigned long data)
{
	icn_card *card = (icn_card *) data;
	int mch = card->secondhalf ? 2 : 0;
	int avail = 0;
	int left;
	u_char c;
	int ch;
	unsigned long flags;
	int i;
	u_char *p;
	isdn_ctrl cmd;

	if (icn_trymaplock_channel(card, mch)) {
		avail = msg_avail;
		for (left = avail, i = readb(&msg_o); left > 0; i++, left--) {
			c = readb(&dev.shmem->comm_buffers.iopc_buf[i & 0xff]);
			icn_putmsg(card, c);
			if (c == 0xff) {
				card->imsg[card->iptr] = 0;
				card->iptr = 0;
				if (card->imsg[0] == '0' && card->imsg[1] >= '0' &&
				    card->imsg[1] <= '2' && card->imsg[2] == ';') {
					ch = (card->imsg[1] - '0') - 1;
					p = &card->imsg[3];
					icn_parse_status(p, ch, card);
				} else {
					p = card->imsg;
					if (!strncmp(p, "DRV1.", 5)) {
						u_char vstr[10];
						u_char *q = vstr;

						printk(KERN_INFO "icn: (%s) %s\n", CID, p);
						if (!strncmp(p + 7, "TC", 2)) {
							card->ptype = ISDN_PTYPE_1TR6;
							card->interface.features |= ISDN_FEATURE_P_1TR6;
							printk(KERN_INFO
							       "icn: (%s) 1TR6-Protocol loaded and running\n", CID);
						}
						if (!strncmp(p + 7, "EC", 2)) {
							card->ptype = ISDN_PTYPE_EURO;
							card->interface.features |= ISDN_FEATURE_P_EURO;
							printk(KERN_INFO
							       "icn: (%s) Euro-Protocol loaded and running\n", CID);
						}
						p = strstr(card->imsg, "BRV") + 3;
						while (*p) {
							if (*p >= '0' && *p <= '9')
								*q++ = *p;
							p++;
						}
						*q = '\0';
						strcat(vstr, "000");
						vstr[3] = '\0';
						card->fw_rev = (int) simple_strtoul(vstr, NULL, 10);
						continue;

					}
				}
			} else {
				card->imsg[card->iptr] = c;
				if (card->iptr < 59)
					card->iptr++;
			}
		}
		writeb((readb(&msg_o) + avail) & 0xff, &msg_o);
		icn_release_channel();
	}
	if (avail) {
		cmd.command = ISDN_STAT_STAVAIL;
		cmd.driver = card->myid;
		cmd.arg = avail;
		card->interface.statcallb(&cmd);
	}
	spin_lock_irqsave(&card->lock, flags);
	if (card->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE))
		if (!(card->flags & ICN_FLAGS_RBTIMER)) {
			/* schedule b-channel polling */
			card->flags |= ICN_FLAGS_RBTIMER;
			del_timer(&card->rb_timer);
			card->rb_timer.function = icn_pollbchan;
			card->rb_timer.data = (unsigned long) card;
			card->rb_timer.expires = jiffies + ICN_TIMER_BCREAD;
			add_timer(&card->rb_timer);
		}
	/* schedule again */
	mod_timer(&card->st_timer, jiffies+ICN_TIMER_DCREAD);
	spin_unlock_irqrestore(&card->lock, flags);
}

/* Append a packet to the transmit buffer-queue.
 * Parameters:
 *   channel = Number of B-channel
 *   skb     = pointer to sk_buff
 *   card    = pointer to card-struct
 * Return:
 *   Number of bytes transferred, -E??? on error
 */

static int
icn_sendbuf(int channel, int ack, struct sk_buff *skb, icn_card * card)
{
	int len = skb->len;
	unsigned long flags;
	struct sk_buff *nskb;

	if (len > 4000) {
		printk(KERN_WARNING
		       "icn: Send packet too large\n");
		return -EINVAL;
	}
	if (len) {
		if (!(card->flags & (channel) ? ICN_FLAGS_B2ACTIVE : ICN_FLAGS_B1ACTIVE))
			return 0;
		if (card->sndcount[channel] > ICN_MAX_SQUEUE)
			return 0;
		#warning TODO test headroom or use skb->nb to flag ACK
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (nskb) {
			/* Push ACK flag as one
			 * byte in front of data.
			 */
			*(skb_push(nskb, 1)) = ack?1:0;
			skb_queue_tail(&card->spqueue[channel], nskb);
			dev_kfree_skb(skb);
		} else
			len = 0;
		spin_lock_irqsave(&card->lock, flags);
		card->sndcount[channel] += len;
		spin_unlock_irqrestore(&card->lock, flags);
	}
	return len;
}

/*
 * Check card's status after starting the bootstrap loader.
 * On entry, the card's shared memory has already to be mapped.
 * Return:
 *   0 on success (Boot loader ready)
 *   -EIO on failure (timeout)
 */
static int
icn_check_loader(int cardnumber)
{
	int timer = 0;

	while (1) {
#ifdef BOOT_DEBUG
		printk(KERN_DEBUG "Loader %d ?\n", cardnumber);
#endif
		if (readb(&dev.shmem->data_control.scns) ||
		    readb(&dev.shmem->data_control.scnr)) {
			if (timer++ > 5) {
				printk(KERN_WARNING
				       "icn: Boot-Loader %d timed out.\n",
				       cardnumber);
				icn_release_channel();
				return -EIO;
			}
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "Loader %d TO?\n", cardnumber);
#endif
			msleep_interruptible(ICN_BOOT_TIMEOUT1);
		} else {
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "Loader %d OK\n", cardnumber);
#endif
			icn_release_channel();
			return 0;
		}
	}
}

/* Load the boot-code into the interface-card's memory and start it.
 * Always called from user-process.
 *
 * Parameters:
 *            buffer = pointer to packet
 * Return:
 *        0 if successfully loaded
 */

#ifdef BOOT_DEBUG
#define SLEEP(sec) { \
int slsec = sec; \
  printk(KERN_DEBUG "SLEEP(%d)\n",slsec); \
  while (slsec) { \
    msleep_interruptible(1000); \
    slsec--; \
  } \
}
#else
#define SLEEP(sec)
#endif

static int
icn_loadboot(u_char __user * buffer, icn_card * card)
{
	int ret;
	u_char *codebuf;
	unsigned long flags;

#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "icn_loadboot called, buffaddr=%08lx\n", (ulong) buffer);
#endif
	if (!(codebuf = kmalloc(ICN_CODE_STAGE1, GFP_KERNEL))) {
		printk(KERN_WARNING "icn: Could not allocate code buffer\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(codebuf, buffer, ICN_CODE_STAGE1)) {
		ret = -EFAULT;
		goto out_kfree;
	}
	if (!card->rvalid) {
		if (!request_region(card->port, ICN_PORTLEN, card->regname)) {
			printk(KERN_WARNING
			       "icn: (%s) ports 0x%03x-0x%03x in use.\n",
			       CID,
			       card->port,
			       card->port + ICN_PORTLEN);
			ret = -EBUSY;
			goto out_kfree;
		}
		card->rvalid = 1;
		if (card->doubleS0)
			card->other->rvalid = 1;
	}
	if (!dev.mvalid) {
		if (!request_mem_region(dev.memaddr, 0x4000, "icn-isdn (all cards)")) {
			printk(KERN_WARNING
			       "icn: memory at 0x%08lx in use.\n", dev.memaddr);
			ret = -EBUSY;
			goto out_kfree;
		}
		dev.shmem = ioremap(dev.memaddr, 0x4000);
		dev.mvalid = 1;
	}
	OUTB_P(0, ICN_RUN);     /* Reset Controller */
	OUTB_P(0, ICN_MAPRAM);  /* Disable RAM      */
	icn_shiftout(ICN_CFG, 0x0f, 3, 4);	/* Windowsize= 16k  */
	icn_shiftout(ICN_CFG, dev.memaddr, 23, 10);	/* Set RAM-Addr.    */
#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "shmem=%08lx\n", dev.memaddr);
#endif
	SLEEP(1);
#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "Map Bank 0\n");
#endif
	spin_lock_irqsave(&dev.devlock, flags);
	icn_map_channel(card, 0);	/* Select Bank 0    */
	icn_lock_channel(card, 0);	/* Lock Bank 0      */
	spin_unlock_irqrestore(&dev.devlock, flags);
	SLEEP(1);
	memcpy_toio(dev.shmem, codebuf, ICN_CODE_STAGE1);	/* Copy code        */
#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "Bootloader transferred\n");
#endif
	if (card->doubleS0) {
		SLEEP(1);
#ifdef BOOT_DEBUG
		printk(KERN_DEBUG "Map Bank 8\n");
#endif
		spin_lock_irqsave(&dev.devlock, flags);
		__icn_release_channel();
		icn_map_channel(card, 2);	/* Select Bank 8   */
		icn_lock_channel(card, 2);	/* Lock Bank 8     */
		spin_unlock_irqrestore(&dev.devlock, flags);
		SLEEP(1);
		memcpy_toio(dev.shmem, codebuf, ICN_CODE_STAGE1);	/* Copy code        */
#ifdef BOOT_DEBUG
		printk(KERN_DEBUG "Bootloader transferred\n");
#endif
	}
	SLEEP(1);
	OUTB_P(0xff, ICN_RUN);  /* Start Boot-Code */
	if ((ret = icn_check_loader(card->doubleS0 ? 2 : 1))) {
		goto out_kfree;
	}
	if (!card->doubleS0) {
		ret = 0;
		goto out_kfree;
	}
	/* reached only, if we have a Double-S0-Card */
#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "Map Bank 0\n");
#endif
	spin_lock_irqsave(&dev.devlock, flags);
	icn_map_channel(card, 0);	/* Select Bank 0   */
	icn_lock_channel(card, 0);	/* Lock Bank 0     */
	spin_unlock_irqrestore(&dev.devlock, flags);
	SLEEP(1);
	ret = (icn_check_loader(1));

 out_kfree:
	kfree(codebuf);
 out:
	return ret;
}

static int
icn_loadproto(u_char __user * buffer, icn_card * card)
{
	register u_char __user *p = buffer;
	u_char codebuf[256];
	uint left = ICN_CODE_STAGE2;
	uint cnt;
	int timer;
	unsigned long flags;

#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "icn_loadproto called\n");
#endif
	if (!access_ok(VERIFY_READ, buffer, ICN_CODE_STAGE2))
		return -EFAULT;
	timer = 0;
	spin_lock_irqsave(&dev.devlock, flags);
	if (card->secondhalf) {
		icn_map_channel(card, 2);
		icn_lock_channel(card, 2);
	} else {
		icn_map_channel(card, 0);
		icn_lock_channel(card, 0);
	}
	spin_unlock_irqrestore(&dev.devlock, flags);
	while (left) {
		if (sbfree) {   /* If there is a free buffer...  */
			cnt = left;
			if (cnt > 256)
				cnt = 256;
			if (copy_from_user(codebuf, p, cnt)) {
				icn_maprelease_channel(card, 0);
				return -EFAULT;
			}
			memcpy_toio(&sbuf_l, codebuf, cnt);	/* copy data                     */
			sbnext; /* switch to next buffer         */
			p += cnt;
			left -= cnt;
			timer = 0;
		} else {
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "boot 2 !sbfree\n");
#endif
			if (timer++ > 5) {
				icn_maprelease_channel(card, 0);
				return -EIO;
			}
			schedule_timeout_interruptible(10);
		}
	}
	writeb(0x20, &sbuf_n);
	timer = 0;
	while (1) {
		if (readb(&cmd_o) || readb(&cmd_i)) {
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "Proto?\n");
#endif
			if (timer++ > 5) {
				printk(KERN_WARNING
				       "icn: (%s) Protocol timed out.\n",
				       CID);
#ifdef BOOT_DEBUG
				printk(KERN_DEBUG "Proto TO!\n");
#endif
				icn_maprelease_channel(card, 0);
				return -EIO;
			}
#ifdef BOOT_DEBUG
			printk(KERN_DEBUG "Proto TO?\n");
#endif
			msleep_interruptible(ICN_BOOT_TIMEOUT1);
		} else {
			if ((card->secondhalf) || (!card->doubleS0)) {
#ifdef BOOT_DEBUG
				printk(KERN_DEBUG "Proto loaded, install poll-timer %d\n",
				       card->secondhalf);
#endif
				spin_lock_irqsave(&card->lock, flags);
				init_timer(&card->st_timer);
				card->st_timer.expires = jiffies + ICN_TIMER_DCREAD;
				card->st_timer.function = icn_polldchan;
				card->st_timer.data = (unsigned long) card;
				add_timer(&card->st_timer);
				card->flags |= ICN_FLAGS_RUNNING;
				if (card->doubleS0) {
					init_timer(&card->other->st_timer);
					card->other->st_timer.expires = jiffies + ICN_TIMER_DCREAD;
					card->other->st_timer.function = icn_polldchan;
					card->other->st_timer.data = (unsigned long) card->other;
					add_timer(&card->other->st_timer);
					card->other->flags |= ICN_FLAGS_RUNNING;
				}
				spin_unlock_irqrestore(&card->lock, flags);
			}
			icn_maprelease_channel(card, 0);
			return 0;
		}
	}
}

/* Read the Status-replies from the Interface */
static int
icn_readstatus(u_char __user *buf, int len, icn_card * card)
{
	int count;
	u_char __user *p;

	for (p = buf, count = 0; count < len; p++, count++) {
		if (card->msg_buf_read == card->msg_buf_write)
			return count;
		if (put_user(*card->msg_buf_read++, p))
			return -EFAULT;
		if (card->msg_buf_read > card->msg_buf_end)
			card->msg_buf_read = card->msg_buf;
	}
	return count;
}

/* Put command-strings into the command-queue of the Interface */
static int
icn_writecmd(const u_char * buf, int len, int user, icn_card * card)
{
	int mch = card->secondhalf ? 2 : 0;
	int pp;
	int i;
	int count;
	int xcount;
	int ocount;
	int loop;
	unsigned long flags;
	int lastmap_channel;
	struct icn_card *lastmap_card;
	u_char *p;
	isdn_ctrl cmd;
	u_char msg[0x100];

	ocount = 1;
	xcount = loop = 0;
	while (len) {
		count = cmd_free;
		if (count > len)
			count = len;
		if (user) {
			if (copy_from_user(msg, buf, count))
				return -EFAULT;
		} else
			memcpy(msg, buf, count);

		spin_lock_irqsave(&dev.devlock, flags);
		lastmap_card = dev.mcard;
		lastmap_channel = dev.channel;
		icn_map_channel(card, mch);

		icn_putmsg(card, '>');
		for (p = msg, pp = readb(&cmd_i), i = count; i > 0; i--, p++, pp
		     ++) {
			writeb((*p == '\n') ? 0xff : *p,
			   &dev.shmem->comm_buffers.pcio_buf[pp & 0xff]);
			len--;
			xcount++;
			icn_putmsg(card, *p);
			if ((*p == '\n') && (i > 1)) {
				icn_putmsg(card, '>');
				ocount++;
			}
			ocount++;
		}
		writeb((readb(&cmd_i) + count) & 0xff, &cmd_i);
		if (lastmap_card)
			icn_map_channel(lastmap_card, lastmap_channel);
		spin_unlock_irqrestore(&dev.devlock, flags);
		if (len) {
			mdelay(1);
			if (loop++ > 20)
				break;
		} else
			break;
	}
	if (len && (!user))
		printk(KERN_WARNING "icn: writemsg incomplete!\n");
	cmd.command = ISDN_STAT_STAVAIL;
	cmd.driver = card->myid;
	cmd.arg = ocount;
	card->interface.statcallb(&cmd);
	return xcount;
}

/*
 * Delete card's pending timers, send STOP to linklevel
 */
static void
icn_stopcard(icn_card * card)
{
	unsigned long flags;
	isdn_ctrl cmd;

	spin_lock_irqsave(&card->lock, flags);
	if (card->flags & ICN_FLAGS_RUNNING) {
		card->flags &= ~ICN_FLAGS_RUNNING;
		del_timer(&card->st_timer);
		del_timer(&card->rb_timer);
		spin_unlock_irqrestore(&card->lock, flags);
		cmd.command = ISDN_STAT_STOP;
		cmd.driver = card->myid;
		card->interface.statcallb(&cmd);
		if (card->doubleS0)
			icn_stopcard(card->other);
	} else
		spin_unlock_irqrestore(&card->lock, flags);
}

static void
icn_stopallcards(void)
{
	icn_card *p = cards;

	while (p) {
		icn_stopcard(p);
		p = p->next;
	}
}

/*
 * Unmap all cards, because some of them may be mapped accidetly during
 * autoprobing of some network drivers (SMC-driver?)
 */
static void
icn_disable_cards(void)
{
	icn_card *card = cards;

	while (card) {
		if (!request_region(card->port, ICN_PORTLEN, "icn-isdn")) {
			printk(KERN_WARNING
			       "icn: (%s) ports 0x%03x-0x%03x in use.\n",
			       CID,
			       card->port,
			       card->port + ICN_PORTLEN);
		} else {
			OUTB_P(0, ICN_RUN);	/* Reset Controller     */
			OUTB_P(0, ICN_MAPRAM);	/* Disable RAM          */
			release_region(card->port, ICN_PORTLEN);
		}
		card = card->next;
	}
}

static int
icn_command(isdn_ctrl * c, icn_card * card)
{
	ulong a;
	ulong flags;
	int i;
	char cbuf[60];
	isdn_ctrl cmd;
	icn_cdef cdef;
	char __user *arg;

	switch (c->command) {
		case ISDN_CMD_IOCTL:
			memcpy(&a, c->parm.num, sizeof(ulong));
			arg = (char __user *)a;
			switch (c->arg) {
				case ICN_IOCTL_SETMMIO:
					if (dev.memaddr != (a & 0x0ffc000)) {
						if (!request_mem_region(a & 0x0ffc000, 0x4000, "icn-isdn (all cards)")) {
							printk(KERN_WARNING
							       "icn: memory at 0x%08lx in use.\n",
							       a & 0x0ffc000);
							return -EINVAL;
						}
						release_mem_region(a & 0x0ffc000, 0x4000);
						icn_stopallcards();
						spin_lock_irqsave(&card->lock, flags);
						if (dev.mvalid) {
							iounmap(dev.shmem);
							release_mem_region(dev.memaddr, 0x4000);
						}
						dev.mvalid = 0;
						dev.memaddr = a & 0x0ffc000;
						spin_unlock_irqrestore(&card->lock, flags);
						printk(KERN_INFO
						       "icn: (%s) mmio set to 0x%08lx\n",
						       CID,
						       dev.memaddr);
					}
					break;
				case ICN_IOCTL_GETMMIO:
					return (long) dev.memaddr;
				case ICN_IOCTL_SETPORT:
					if (a == 0x300 || a == 0x310 || a == 0x320 || a == 0x330
					    || a == 0x340 || a == 0x350 || a == 0x360 ||
					    a == 0x308 || a == 0x318 || a == 0x328 || a == 0x338
					    || a == 0x348 || a == 0x358 || a == 0x368) {
						if (card->port != (unsigned short) a) {
							if (!request_region((unsigned short) a, ICN_PORTLEN, "icn-isdn")) {
								printk(KERN_WARNING
								       "icn: (%s) ports 0x%03x-0x%03x in use.\n",
								       CID, (int) a, (int) a + ICN_PORTLEN);
								return -EINVAL;
							}
							release_region((unsigned short) a, ICN_PORTLEN);
							icn_stopcard(card);
							spin_lock_irqsave(&card->lock, flags);
							if (card->rvalid)
								release_region(card->port, ICN_PORTLEN);
							card->port = (unsigned short) a;
							card->rvalid = 0;
							if (card->doubleS0) {
								card->other->port = (unsigned short) a;
								card->other->rvalid = 0;
							}
							spin_unlock_irqrestore(&card->lock, flags);
							printk(KERN_INFO
							       "icn: (%s) port set to 0x%03x\n",
							CID, card->port);
						}
					} else
						return -EINVAL;
					break;
				case ICN_IOCTL_GETPORT:
					return (int) card->port;
				case ICN_IOCTL_GETDOUBLE:
					return (int) card->doubleS0;
				case ICN_IOCTL_DEBUGVAR:
					if (copy_to_user(arg,
							 &card,
							 sizeof(ulong)))
						return -EFAULT;
					a += sizeof(ulong);
					{
						ulong l = (ulong) & dev;
						if (copy_to_user(arg,
								 &l,
								 sizeof(ulong)))
							return -EFAULT;
					}
					return 0;
				case ICN_IOCTL_LOADBOOT:
					if (dev.firstload) {
						icn_disable_cards();
						dev.firstload = 0;
					}
					icn_stopcard(card);
					return (icn_loadboot(arg, card));
				case ICN_IOCTL_LOADPROTO:
					icn_stopcard(card);
					if ((i = (icn_loadproto(arg, card))))
						return i;
					if (card->doubleS0)
						i = icn_loadproto(arg + ICN_CODE_STAGE2, card->other);
					return i;
					break;
				case ICN_IOCTL_ADDCARD:
					if (!dev.firstload)
						return -EBUSY;
					if (copy_from_user(&cdef,
							   arg,
							   sizeof(cdef)))
						return -EFAULT;
					return (icn_addcard(cdef.port, cdef.id1, cdef.id2));
					break;
				case ICN_IOCTL_LEASEDCFG:
					if (a) {
						if (!card->leased) {
							card->leased = 1;
							while (card->ptype == ISDN_PTYPE_UNKNOWN) {
								msleep_interruptible(ICN_BOOT_TIMEOUT1);
							}
							msleep_interruptible(ICN_BOOT_TIMEOUT1);
							sprintf(cbuf, "00;FV2ON\n01;EAZ%c\n02;EAZ%c\n",
								(a & 1)?'1':'C', (a & 2)?'2':'C');
							i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
							printk(KERN_INFO
							       "icn: (%s) Leased-line mode enabled\n",
							       CID);
							cmd.command = ISDN_STAT_RUN;
							cmd.driver = card->myid;
							cmd.arg = 0;
							card->interface.statcallb(&cmd);
						}
					} else {
						if (card->leased) {
							card->leased = 0;
							sprintf(cbuf, "00;FV2OFF\n");
							i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
							printk(KERN_INFO
							       "icn: (%s) Leased-line mode disabled\n",
							       CID);
							cmd.command = ISDN_STAT_RUN;
							cmd.driver = card->myid;
							cmd.arg = 0;
							card->interface.statcallb(&cmd);
						}
					}
					return 0;
				default:
					return -EINVAL;
			}
			break;
		case ISDN_CMD_DIAL:
			if (!(card->flags & ICN_FLAGS_RUNNING))
				return -ENODEV;
			if (card->leased)
				break;
			if ((c->arg & 255) < ICN_BCH) {
				char *p;
				char dial[50];
				char dcode[4];

				a = c->arg;
				p = c->parm.setup.phone;
				if (*p == 's' || *p == 'S') {
					/* Dial for SPV */
					p++;
					strcpy(dcode, "SCA");
				} else
					/* Normal Dial */
					strcpy(dcode, "CAL");
				strcpy(dial, p);
				sprintf(cbuf, "%02d;D%s_R%s,%02d,%02d,%s\n", (int) (a + 1),
					dcode, dial, c->parm.setup.si1,
				c->parm.setup.si2, c->parm.setup.eazmsn);
				i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
			}
			break;
		case ISDN_CMD_ACCEPTD:
			if (!(card->flags & ICN_FLAGS_RUNNING))
				return -ENODEV;
			if (c->arg < ICN_BCH) {
				a = c->arg + 1;
				if (card->fw_rev >= 300) {
					switch (card->l2_proto[a - 1]) {
						case ISDN_PROTO_L2_X75I:
							sprintf(cbuf, "%02d;BX75\n", (int) a);
							break;
						case ISDN_PROTO_L2_HDLC:
							sprintf(cbuf, "%02d;BTRA\n", (int) a);
							break;
					}
					i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
				}
				sprintf(cbuf, "%02d;DCON_R\n", (int) a);
				i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
			}
			break;
		case ISDN_CMD_ACCEPTB:
			if (!(card->flags & ICN_FLAGS_RUNNING))
				return -ENODEV;
			if (c->arg < ICN_BCH) {
				a = c->arg + 1;
				if (card->fw_rev >= 300)
					switch (card->l2_proto[a - 1]) {
						case ISDN_PROTO_L2_X75I:
							sprintf(cbuf, "%02d;BCON_R,BX75\n", (int) a);
							break;
						case ISDN_PROTO_L2_HDLC:
							sprintf(cbuf, "%02d;BCON_R,BTRA\n", (int) a);
							break;
				} else
					sprintf(cbuf, "%02d;BCON_R\n", (int) a);
				i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
			}
			break;
		case ISDN_CMD_HANGUP:
			if (!(card->flags & ICN_FLAGS_RUNNING))
				return -ENODEV;
			if (c->arg < ICN_BCH) {
				a = c->arg + 1;
				sprintf(cbuf, "%02d;BDIS_R\n%02d;DDIS_R\n", (int) a, (int) a);
				i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
			}
			break;
		case ISDN_CMD_SETEAZ:
			if (!(card->flags & ICN_FLAGS_RUNNING))
				return -ENODEV;
			if (card->leased)
				break;
			if (c->arg < ICN_BCH) {
				a = c->arg + 1;
				if (card->ptype == ISDN_PTYPE_EURO) {
					sprintf(cbuf, "%02d;MS%s%s\n", (int) a,
						c->parm.num[0] ? "N" : "ALL", c->parm.num);
				} else
					sprintf(cbuf, "%02d;EAZ%s\n", (int) a,
						c->parm.num[0] ? (char *)(c->parm.num) : "0123456789");
				i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
			}
			break;
		case ISDN_CMD_CLREAZ:
			if (!(card->flags & ICN_FLAGS_RUNNING))
				return -ENODEV;
			if (card->leased)
				break;
			if (c->arg < ICN_BCH) {
				a = c->arg + 1;
				if (card->ptype == ISDN_PTYPE_EURO)
					sprintf(cbuf, "%02d;MSNC\n", (int) a);
				else
					sprintf(cbuf, "%02d;EAZC\n", (int) a);
				i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
			}
			break;
		case ISDN_CMD_SETL2:
			if (!(card->flags & ICN_FLAGS_RUNNING))
				return -ENODEV;
			if ((c->arg & 255) < ICN_BCH) {
				a = c->arg;
				switch (a >> 8) {
					case ISDN_PROTO_L2_X75I:
						sprintf(cbuf, "%02d;BX75\n", (int) (a & 255) + 1);
						break;
					case ISDN_PROTO_L2_HDLC:
						sprintf(cbuf, "%02d;BTRA\n", (int) (a & 255) + 1);
						break;
					default:
						return -EINVAL;
				}
				i = icn_writecmd(cbuf, strlen(cbuf), 0, card);
				card->l2_proto[a & 255] = (a >> 8);
			}
			break;
		case ISDN_CMD_SETL3:
			if (!(card->flags & ICN_FLAGS_RUNNING))
				return -ENODEV;
			return 0;
		default:
			return -EINVAL;
	}
	return 0;
}

/*
 * Find card with given driverId
 */
static inline icn_card *
icn_findcard(int driverid)
{
	icn_card *p = cards;

	while (p) {
		if (p->myid == driverid)
			return p;
		p = p->next;
	}
	return (icn_card *) 0;
}

/*
 * Wrapper functions for interface to linklevel
 */
static int
if_command(isdn_ctrl * c)
{
	icn_card *card = icn_findcard(c->driver);

	if (card)
		return (icn_command(c, card));
	printk(KERN_ERR
	       "icn: if_command %d called with invalid driverId %d!\n",
	       c->command, c->driver);
	return -ENODEV;
}

static int
if_writecmd(const u_char __user *buf, int len, int id, int channel)
{
	icn_card *card = icn_findcard(id);

	if (card) {
		if (!(card->flags & ICN_FLAGS_RUNNING))
			return -ENODEV;
		return (icn_writecmd(buf, len, 1, card));
	}
	printk(KERN_ERR
	       "icn: if_writecmd called with invalid driverId!\n");
	return -ENODEV;
}

static int
if_readstatus(u_char __user *buf, int len, int id, int channel)
{
	icn_card *card = icn_findcard(id);

	if (card) {
		if (!(card->flags & ICN_FLAGS_RUNNING))
			return -ENODEV;
		return (icn_readstatus(buf, len, card));
	}
	printk(KERN_ERR
	       "icn: if_readstatus called with invalid driverId!\n");
	return -ENODEV;
}

static int
if_sendbuf(int id, int channel, int ack, struct sk_buff *skb)
{
	icn_card *card = icn_findcard(id);

	if (card) {
		if (!(card->flags & ICN_FLAGS_RUNNING))
			return -ENODEV;
		return (icn_sendbuf(channel, ack, skb, card));
	}
	printk(KERN_ERR
	       "icn: if_sendbuf called with invalid driverId!\n");
	return -ENODEV;
}

/*
 * Allocate a new card-struct, initialize it
 * link it into cards-list and register it at linklevel.
 */
static icn_card *
icn_initcard(int port, char *id)
{
	icn_card *card;
	int i;

	if (!(card = kzalloc(sizeof(icn_card), GFP_KERNEL))) {
		printk(KERN_WARNING
		       "icn: (%s) Could not allocate card-struct.\n", id);
		return (icn_card *) 0;
	}
	spin_lock_init(&card->lock);
	card->port = port;
	card->interface.owner = THIS_MODULE;
	card->interface.hl_hdrlen = 1;
	card->interface.channels = ICN_BCH;
	card->interface.maxbufsize = 4000;
	card->interface.command = if_command;
	card->interface.writebuf_skb = if_sendbuf;
	card->interface.writecmd = if_writecmd;
	card->interface.readstat = if_readstatus;
	card->interface.features = ISDN_FEATURE_L2_X75I |
	    ISDN_FEATURE_L2_HDLC |
	    ISDN_FEATURE_L3_TRANS |
	    ISDN_FEATURE_P_UNKNOWN;
	card->ptype = ISDN_PTYPE_UNKNOWN;
	strlcpy(card->interface.id, id, sizeof(card->interface.id));
	card->msg_buf_write = card->msg_buf;
	card->msg_buf_read = card->msg_buf;
	card->msg_buf_end = &card->msg_buf[sizeof(card->msg_buf) - 1];
	for (i = 0; i < ICN_BCH; i++) {
		card->l2_proto[i] = ISDN_PROTO_L2_X75I;
		skb_queue_head_init(&card->spqueue[i]);
	}
	card->next = cards;
	cards = card;
	if (!register_isdn(&card->interface)) {
		cards = cards->next;
		printk(KERN_WARNING
		       "icn: Unable to register %s\n", id);
		kfree(card);
		return (icn_card *) 0;
	}
	card->myid = card->interface.channels;
	sprintf(card->regname, "icn-isdn (%s)", card->interface.id);
	return card;
}

static int
icn_addcard(int port, char *id1, char *id2)
{
	icn_card *card;
	icn_card *card2;

	if (!(card = icn_initcard(port, id1))) {
		return -EIO;
	}
	if (!strlen(id2)) {
		printk(KERN_INFO
		       "icn: (%s) ICN-2B, port 0x%x added\n",
		       card->interface.id, port);
		return 0;
	}
	if (!(card2 = icn_initcard(port, id2))) {
		printk(KERN_INFO
		       "icn: (%s) half ICN-4B, port 0x%x added\n",
		       card2->interface.id, port);
		return 0;
	}
	card->doubleS0 = 1;
	card->secondhalf = 0;
	card->other = card2;
	card2->doubleS0 = 1;
	card2->secondhalf = 1;
	card2->other = card;
	printk(KERN_INFO
	       "icn: (%s and %s) ICN-4B, port 0x%x added\n",
	       card->interface.id, card2->interface.id, port);
	return 0;
}

#ifndef MODULE
static int __init
icn_setup(char *line)
{
	char *p, *str;
	int	ints[3];
	static char sid[20];
	static char sid2[20];

	str = get_options(line, 2, ints);
	if (ints[0])
		portbase = ints[1];
	if (ints[0] > 1)
		membase = (unsigned long)ints[2];
	if (str && *str) {
		strcpy(sid, str);
		icn_id = sid;
		if ((p = strchr(sid, ','))) {
			*p++ = 0;
			strcpy(sid2, p);
			icn_id2 = sid2;
		}
	}
	return(1);
}
__setup("icn=", icn_setup);
#endif /* MODULE */

static int __init icn_init(void)
{
	char *p;
	char rev[10];

	memset(&dev, 0, sizeof(icn_dev));
	dev.memaddr = (membase & 0x0ffc000);
	dev.channel = -1;
	dev.mcard = NULL;
	dev.firstload = 1;
	spin_lock_init(&dev.devlock);

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, " ??? ");
	printk(KERN_NOTICE "ICN-ISDN-driver Rev%smem=0x%08lx\n", rev,
	       dev.memaddr);
	return (icn_addcard(portbase, icn_id, icn_id2));
}

static void __exit icn_exit(void)
{
	isdn_ctrl cmd;
	icn_card *card = cards;
	icn_card *last, *tmpcard;
	int i;
	unsigned long flags;

	icn_stopallcards();
	while (card) {
		cmd.command = ISDN_STAT_UNLOAD;
		cmd.driver = card->myid;
		card->interface.statcallb(&cmd);
		spin_lock_irqsave(&card->lock, flags);
		if (card->rvalid) {
			OUTB_P(0, ICN_RUN);	/* Reset Controller     */
			OUTB_P(0, ICN_MAPRAM);	/* Disable RAM          */
			if (card->secondhalf || (!card->doubleS0)) {
				release_region(card->port, ICN_PORTLEN);
				card->rvalid = 0;
			}
			for (i = 0; i < ICN_BCH; i++)
				icn_free_queue(card, i);
		}
		tmpcard = card->next;
		spin_unlock_irqrestore(&card->lock, flags);
		card = tmpcard;
	}
	card = cards;
	cards = NULL;
	while (card) {
		last = card;
		card = card->next;
		kfree(last);
	}
	if (dev.mvalid) {
		iounmap(dev.shmem);
		release_mem_region(dev.memaddr, 0x4000);
	}
	printk(KERN_NOTICE "ICN-ISDN-driver unloaded\n");
}

module_init(icn_init);
module_exit(icn_exit);
