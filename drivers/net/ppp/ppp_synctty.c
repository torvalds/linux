// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PPP synchronous tty channel driver for Linux.
 *
 * This is a ppp channel driver that can be used with tty device drivers
 * that are frame oriented, such as synchronous HDLC devices.
 *
 * Complete PPP frames without encoding/decoding are exchanged between
 * the channel driver and the device driver.
 *
 * The async map IOCTL codes are implemented to keep the user mode
 * applications happy if they call them. Synchronous PPP does not use
 * the async maps.
 *
 * Copyright 1999 Paul Mackerras.
 *
 * Also touched by the grubby hands of Paul Fulghum paulkf@microgate.com
 *
 * This driver provides the encapsulation and framing for sending
 * and receiving PPP frames over sync serial lines.  It relies on
 * the generic PPP layer to give it frames to send and to process
 * received frames.  It implements the PPP line discipline.
 *
 * Part of the code in this driver was inspired by the old async-only
 * PPP driver, written by Michael Callahan and Al Longyear, and
 * subsequently hacked by Paul Mackerras.
 *
 * ==FILEVERSION 20040616==
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/tty.h>
#include <linux/netdevice.h>
#include <linux/poll.h>
#include <linux/ppp_defs.h>
#include <linux/ppp-ioctl.h>
#include <linux/ppp_channel.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/refcount.h>
#include <asm/unaligned.h>
#include <linux/uaccess.h>

#define PPP_VERSION	"2.4.2"

/* Structure for storing local state. */
struct syncppp {
	struct tty_struct *tty;
	unsigned int	flags;
	unsigned int	rbits;
	int		mru;
	spinlock_t	xmit_lock;
	spinlock_t	recv_lock;
	unsigned long	xmit_flags;
	u32		xaccm[8];
	u32		raccm;
	unsigned int	bytes_sent;
	unsigned int	bytes_rcvd;

	struct sk_buff	*tpkt;
	unsigned long	last_xmit;

	struct sk_buff_head rqueue;

	struct tasklet_struct tsk;

	refcount_t	refcnt;
	struct completion dead_cmp;
	struct ppp_channel chan;	/* interface to generic ppp layer */
};

/* Bit numbers in xmit_flags */
#define XMIT_WAKEUP	0
#define XMIT_FULL	1

/* Bits in rbits */
#define SC_RCV_BITS	(SC_RCV_B7_1|SC_RCV_B7_0|SC_RCV_ODDP|SC_RCV_EVNP)

#define PPPSYNC_MAX_RQLEN	32	/* arbitrary */

/*
 * Prototypes.
 */
static struct sk_buff* ppp_sync_txmunge(struct syncppp *ap, struct sk_buff *);
static int ppp_sync_send(struct ppp_channel *chan, struct sk_buff *skb);
static int ppp_sync_ioctl(struct ppp_channel *chan, unsigned int cmd,
			  unsigned long arg);
static void ppp_sync_process(unsigned long arg);
static int ppp_sync_push(struct syncppp *ap);
static void ppp_sync_flush_output(struct syncppp *ap);
static void ppp_sync_input(struct syncppp *ap, const unsigned char *buf,
			   char *flags, int count);

static const struct ppp_channel_ops sync_ops = {
	.start_xmit = ppp_sync_send,
	.ioctl      = ppp_sync_ioctl,
};

/*
 * Utility procedure to print a buffer in hex/ascii
 */
static void
ppp_print_buffer (const char *name, const __u8 *buf, int count)
{
	if (name != NULL)
		printk(KERN_DEBUG "ppp_synctty: %s, count = %d\n", name, count);

	print_hex_dump_bytes("", DUMP_PREFIX_NONE, buf, count);
}


/*
 * Routines implementing the synchronous PPP line discipline.
 */

/*
 * We have a potential race on dereferencing tty->disc_data,
 * because the tty layer provides no locking at all - thus one
 * cpu could be running ppp_synctty_receive while another
 * calls ppp_synctty_close, which zeroes tty->disc_data and
 * frees the memory that ppp_synctty_receive is using.  The best
 * way to fix this is to use a rwlock in the tty struct, but for now
 * we use a single global rwlock for all ttys in ppp line discipline.
 *
 * FIXME: Fixed in tty_io nowadays.
 */
static DEFINE_RWLOCK(disc_data_lock);

static struct syncppp *sp_get(struct tty_struct *tty)
{
	struct syncppp *ap;

	read_lock(&disc_data_lock);
	ap = tty->disc_data;
	if (ap != NULL)
		refcount_inc(&ap->refcnt);
	read_unlock(&disc_data_lock);
	return ap;
}

static void sp_put(struct syncppp *ap)
{
	if (refcount_dec_and_test(&ap->refcnt))
		complete(&ap->dead_cmp);
}

/*
 * Called when a tty is put into sync-PPP line discipline.
 */
static int
ppp_sync_open(struct tty_struct *tty)
{
	struct syncppp *ap;
	int err;
	int speed;

	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;

	ap = kzalloc(sizeof(*ap), GFP_KERNEL);
	err = -ENOMEM;
	if (!ap)
		goto out;

	/* initialize the syncppp structure */
	ap->tty = tty;
	ap->mru = PPP_MRU;
	spin_lock_init(&ap->xmit_lock);
	spin_lock_init(&ap->recv_lock);
	ap->xaccm[0] = ~0U;
	ap->xaccm[3] = 0x60000000U;
	ap->raccm = ~0U;

	skb_queue_head_init(&ap->rqueue);
	tasklet_init(&ap->tsk, ppp_sync_process, (unsigned long) ap);

	refcount_set(&ap->refcnt, 1);
	init_completion(&ap->dead_cmp);

	ap->chan.private = ap;
	ap->chan.ops = &sync_ops;
	ap->chan.mtu = PPP_MRU;
	ap->chan.hdrlen = 2;	/* for A/C bytes */
	speed = tty_get_baud_rate(tty);
	ap->chan.speed = speed;
	err = ppp_register_channel(&ap->chan);
	if (err)
		goto out_free;

	tty->disc_data = ap;
	tty->receive_room = 65536;
	return 0;

 out_free:
	kfree(ap);
 out:
	return err;
}

/*
 * Called when the tty is put into another line discipline
 * or it hangs up.  We have to wait for any cpu currently
 * executing in any of the other ppp_synctty_* routines to
 * finish before we can call ppp_unregister_channel and free
 * the syncppp struct.  This routine must be called from
 * process context, not interrupt or softirq context.
 */
static void
ppp_sync_close(struct tty_struct *tty)
{
	struct syncppp *ap;

	write_lock_irq(&disc_data_lock);
	ap = tty->disc_data;
	tty->disc_data = NULL;
	write_unlock_irq(&disc_data_lock);
	if (!ap)
		return;

	/*
	 * We have now ensured that nobody can start using ap from now
	 * on, but we have to wait for all existing users to finish.
	 * Note that ppp_unregister_channel ensures that no calls to
	 * our channel ops (i.e. ppp_sync_send/ioctl) are in progress
	 * by the time it returns.
	 */
	if (!refcount_dec_and_test(&ap->refcnt))
		wait_for_completion(&ap->dead_cmp);
	tasklet_kill(&ap->tsk);

	ppp_unregister_channel(&ap->chan);
	skb_queue_purge(&ap->rqueue);
	kfree_skb(ap->tpkt);
	kfree(ap);
}

/*
 * Called on tty hangup in process context.
 *
 * Wait for I/O to driver to complete and unregister PPP channel.
 * This is already done by the close routine, so just call that.
 */
static int ppp_sync_hangup(struct tty_struct *tty)
{
	ppp_sync_close(tty);
	return 0;
}

/*
 * Read does nothing - no data is ever available this way.
 * Pppd reads and writes packets via /dev/ppp instead.
 */
static ssize_t
ppp_sync_read(struct tty_struct *tty, struct file *file,
	       unsigned char __user *buf, size_t count)
{
	return -EAGAIN;
}

/*
 * Write on the tty does nothing, the packets all come in
 * from the ppp generic stuff.
 */
static ssize_t
ppp_sync_write(struct tty_struct *tty, struct file *file,
		const unsigned char *buf, size_t count)
{
	return -EAGAIN;
}

static int
ppp_synctty_ioctl(struct tty_struct *tty, struct file *file,
		  unsigned int cmd, unsigned long arg)
{
	struct syncppp *ap = sp_get(tty);
	int __user *p = (int __user *)arg;
	int err, val;

	if (!ap)
		return -ENXIO;
	err = -EFAULT;
	switch (cmd) {
	case PPPIOCGCHAN:
		err = -EFAULT;
		if (put_user(ppp_channel_index(&ap->chan), p))
			break;
		err = 0;
		break;

	case PPPIOCGUNIT:
		err = -EFAULT;
		if (put_user(ppp_unit_number(&ap->chan), p))
			break;
		err = 0;
		break;

	case TCFLSH:
		/* flush our buffers and the serial port's buffer */
		if (arg == TCIOFLUSH || arg == TCOFLUSH)
			ppp_sync_flush_output(ap);
		err = n_tty_ioctl_helper(tty, file, cmd, arg);
		break;

	case FIONREAD:
		val = 0;
		if (put_user(val, p))
			break;
		err = 0;
		break;

	default:
		err = tty_mode_ioctl(tty, file, cmd, arg);
		break;
	}

	sp_put(ap);
	return err;
}

/* No kernel lock - fine */
static __poll_t
ppp_sync_poll(struct tty_struct *tty, struct file *file, poll_table *wait)
{
	return 0;
}

/* May sleep, don't call from interrupt level or with interrupts disabled */
static void
ppp_sync_receive(struct tty_struct *tty, const unsigned char *buf,
		  char *cflags, int count)
{
	struct syncppp *ap = sp_get(tty);
	unsigned long flags;

	if (!ap)
		return;
	spin_lock_irqsave(&ap->recv_lock, flags);
	ppp_sync_input(ap, buf, cflags, count);
	spin_unlock_irqrestore(&ap->recv_lock, flags);
	if (!skb_queue_empty(&ap->rqueue))
		tasklet_schedule(&ap->tsk);
	sp_put(ap);
	tty_unthrottle(tty);
}

static void
ppp_sync_wakeup(struct tty_struct *tty)
{
	struct syncppp *ap = sp_get(tty);

	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	if (!ap)
		return;
	set_bit(XMIT_WAKEUP, &ap->xmit_flags);
	tasklet_schedule(&ap->tsk);
	sp_put(ap);
}


static struct tty_ldisc_ops ppp_sync_ldisc = {
	.owner	= THIS_MODULE,
	.magic	= TTY_LDISC_MAGIC,
	.name	= "pppsync",
	.open	= ppp_sync_open,
	.close	= ppp_sync_close,
	.hangup	= ppp_sync_hangup,
	.read	= ppp_sync_read,
	.write	= ppp_sync_write,
	.ioctl	= ppp_synctty_ioctl,
	.poll	= ppp_sync_poll,
	.receive_buf = ppp_sync_receive,
	.write_wakeup = ppp_sync_wakeup,
};

static int __init
ppp_sync_init(void)
{
	int err;

	err = tty_register_ldisc(N_SYNC_PPP, &ppp_sync_ldisc);
	if (err != 0)
		printk(KERN_ERR "PPP_sync: error %d registering line disc.\n",
		       err);
	return err;
}

/*
 * The following routines provide the PPP channel interface.
 */
static int
ppp_sync_ioctl(struct ppp_channel *chan, unsigned int cmd, unsigned long arg)
{
	struct syncppp *ap = chan->private;
	int err, val;
	u32 accm[8];
	void __user *argp = (void __user *)arg;
	u32 __user *p = argp;

	err = -EFAULT;
	switch (cmd) {
	case PPPIOCGFLAGS:
		val = ap->flags | ap->rbits;
		if (put_user(val, (int __user *) argp))
			break;
		err = 0;
		break;
	case PPPIOCSFLAGS:
		if (get_user(val, (int __user *) argp))
			break;
		ap->flags = val & ~SC_RCV_BITS;
		spin_lock_irq(&ap->recv_lock);
		ap->rbits = val & SC_RCV_BITS;
		spin_unlock_irq(&ap->recv_lock);
		err = 0;
		break;

	case PPPIOCGASYNCMAP:
		if (put_user(ap->xaccm[0], p))
			break;
		err = 0;
		break;
	case PPPIOCSASYNCMAP:
		if (get_user(ap->xaccm[0], p))
			break;
		err = 0;
		break;

	case PPPIOCGRASYNCMAP:
		if (put_user(ap->raccm, p))
			break;
		err = 0;
		break;
	case PPPIOCSRASYNCMAP:
		if (get_user(ap->raccm, p))
			break;
		err = 0;
		break;

	case PPPIOCGXASYNCMAP:
		if (copy_to_user(argp, ap->xaccm, sizeof(ap->xaccm)))
			break;
		err = 0;
		break;
	case PPPIOCSXASYNCMAP:
		if (copy_from_user(accm, argp, sizeof(accm)))
			break;
		accm[2] &= ~0x40000000U;	/* can't escape 0x5e */
		accm[3] |= 0x60000000U;		/* must escape 0x7d, 0x7e */
		memcpy(ap->xaccm, accm, sizeof(ap->xaccm));
		err = 0;
		break;

	case PPPIOCGMRU:
		if (put_user(ap->mru, (int __user *) argp))
			break;
		err = 0;
		break;
	case PPPIOCSMRU:
		if (get_user(val, (int __user *) argp))
			break;
		if (val < PPP_MRU)
			val = PPP_MRU;
		ap->mru = val;
		err = 0;
		break;

	default:
		err = -ENOTTY;
	}
	return err;
}

/*
 * This is called at softirq level to deliver received packets
 * to the ppp_generic code, and to tell the ppp_generic code
 * if we can accept more output now.
 */
static void ppp_sync_process(unsigned long arg)
{
	struct syncppp *ap = (struct syncppp *) arg;
	struct sk_buff *skb;

	/* process received packets */
	while ((skb = skb_dequeue(&ap->rqueue)) != NULL) {
		if (skb->len == 0) {
			/* zero length buffers indicate error */
			ppp_input_error(&ap->chan, 0);
			kfree_skb(skb);
		}
		else
			ppp_input(&ap->chan, skb);
	}

	/* try to push more stuff out */
	if (test_bit(XMIT_WAKEUP, &ap->xmit_flags) && ppp_sync_push(ap))
		ppp_output_wakeup(&ap->chan);
}

/*
 * Procedures for encapsulation and framing.
 */

static struct sk_buff*
ppp_sync_txmunge(struct syncppp *ap, struct sk_buff *skb)
{
	int proto;
	unsigned char *data;
	int islcp;

	data  = skb->data;
	proto = get_unaligned_be16(data);

	/* LCP packets with codes between 1 (configure-request)
	 * and 7 (code-reject) must be sent as though no options
	 * have been negotiated.
	 */
	islcp = proto == PPP_LCP && 1 <= data[2] && data[2] <= 7;

	/* compress protocol field if option enabled */
	if (data[0] == 0 && (ap->flags & SC_COMP_PROT) && !islcp)
		skb_pull(skb,1);

	/* prepend address/control fields if necessary */
	if ((ap->flags & SC_COMP_AC) == 0 || islcp) {
		if (skb_headroom(skb) < 2) {
			struct sk_buff *npkt = dev_alloc_skb(skb->len + 2);
			if (npkt == NULL) {
				kfree_skb(skb);
				return NULL;
			}
			skb_reserve(npkt,2);
			skb_copy_from_linear_data(skb,
				      skb_put(npkt, skb->len), skb->len);
			consume_skb(skb);
			skb = npkt;
		}
		skb_push(skb,2);
		skb->data[0] = PPP_ALLSTATIONS;
		skb->data[1] = PPP_UI;
	}

	ap->last_xmit = jiffies;

	if (skb && ap->flags & SC_LOG_OUTPKT)
		ppp_print_buffer ("send buffer", skb->data, skb->len);

	return skb;
}

/*
 * Transmit-side routines.
 */

/*
 * Send a packet to the peer over an sync tty line.
 * Returns 1 iff the packet was accepted.
 * If the packet was not accepted, we will call ppp_output_wakeup
 * at some later time.
 */
static int
ppp_sync_send(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct syncppp *ap = chan->private;

	ppp_sync_push(ap);

	if (test_and_set_bit(XMIT_FULL, &ap->xmit_flags))
		return 0;	/* already full */
	skb = ppp_sync_txmunge(ap, skb);
	if (skb != NULL)
		ap->tpkt = skb;
	else
		clear_bit(XMIT_FULL, &ap->xmit_flags);

	ppp_sync_push(ap);
	return 1;
}

/*
 * Push as much data as possible out to the tty.
 */
static int
ppp_sync_push(struct syncppp *ap)
{
	int sent, done = 0;
	struct tty_struct *tty = ap->tty;
	int tty_stuffed = 0;

	if (!spin_trylock_bh(&ap->xmit_lock))
		return 0;
	for (;;) {
		if (test_and_clear_bit(XMIT_WAKEUP, &ap->xmit_flags))
			tty_stuffed = 0;
		if (!tty_stuffed && ap->tpkt) {
			set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
			sent = tty->ops->write(tty, ap->tpkt->data, ap->tpkt->len);
			if (sent < 0)
				goto flush;	/* error, e.g. loss of CD */
			if (sent < ap->tpkt->len) {
				tty_stuffed = 1;
			} else {
				consume_skb(ap->tpkt);
				ap->tpkt = NULL;
				clear_bit(XMIT_FULL, &ap->xmit_flags);
				done = 1;
			}
			continue;
		}
		/* haven't made any progress */
		spin_unlock_bh(&ap->xmit_lock);
		if (!(test_bit(XMIT_WAKEUP, &ap->xmit_flags) ||
		      (!tty_stuffed && ap->tpkt)))
			break;
		if (!spin_trylock_bh(&ap->xmit_lock))
			break;
	}
	return done;

flush:
	if (ap->tpkt) {
		kfree_skb(ap->tpkt);
		ap->tpkt = NULL;
		clear_bit(XMIT_FULL, &ap->xmit_flags);
		done = 1;
	}
	spin_unlock_bh(&ap->xmit_lock);
	return done;
}

/*
 * Flush output from our internal buffers.
 * Called for the TCFLSH ioctl.
 */
static void
ppp_sync_flush_output(struct syncppp *ap)
{
	int done = 0;

	spin_lock_bh(&ap->xmit_lock);
	if (ap->tpkt != NULL) {
		kfree_skb(ap->tpkt);
		ap->tpkt = NULL;
		clear_bit(XMIT_FULL, &ap->xmit_flags);
		done = 1;
	}
	spin_unlock_bh(&ap->xmit_lock);
	if (done)
		ppp_output_wakeup(&ap->chan);
}

/*
 * Receive-side routines.
 */

/* called when the tty driver has data for us.
 *
 * Data is frame oriented: each call to ppp_sync_input is considered
 * a whole frame. If the 1st flag byte is non-zero then the whole
 * frame is considered to be in error and is tossed.
 */
static void
ppp_sync_input(struct syncppp *ap, const unsigned char *buf,
		char *flags, int count)
{
	struct sk_buff *skb;
	unsigned char *p;

	if (count == 0)
		return;

	if (ap->flags & SC_LOG_INPKT)
		ppp_print_buffer ("receive buffer", buf, count);

	/* stuff the chars in the skb */
	skb = dev_alloc_skb(ap->mru + PPP_HDRLEN + 2);
	if (!skb) {
		printk(KERN_ERR "PPPsync: no memory (input pkt)\n");
		goto err;
	}
	/* Try to get the payload 4-byte aligned */
	if (buf[0] != PPP_ALLSTATIONS)
		skb_reserve(skb, 2 + (buf[0] & 1));

	if (flags && *flags) {
		/* error flag set, ignore frame */
		goto err;
	} else if (count > skb_tailroom(skb)) {
		/* packet overflowed MRU */
		goto err;
	}

	skb_put_data(skb, buf, count);

	/* strip address/control field if present */
	p = skb->data;
	if (p[0] == PPP_ALLSTATIONS && p[1] == PPP_UI) {
		/* chop off address/control */
		if (skb->len < 3)
			goto err;
		p = skb_pull(skb, 2);
	}

	/* PPP packet length should be >= 2 bytes when protocol field is not
	 * compressed.
	 */
	if (!(p[0] & 0x01) && skb->len < 2)
		goto err;

	/* queue the frame to be processed */
	skb_queue_tail(&ap->rqueue, skb);
	return;

err:
	/* queue zero length packet as error indication */
	if (skb || (skb = dev_alloc_skb(0))) {
		skb_trim(skb, 0);
		skb_queue_tail(&ap->rqueue, skb);
	}
}

static void __exit
ppp_sync_cleanup(void)
{
	if (tty_unregister_ldisc(N_SYNC_PPP) != 0)
		printk(KERN_ERR "failed to unregister Sync PPP line discipline\n");
}

module_init(ppp_sync_init);
module_exit(ppp_sync_cleanup);
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_SYNC_PPP);
