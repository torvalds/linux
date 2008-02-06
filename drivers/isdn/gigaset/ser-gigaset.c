/* This is the serial hardware link layer (HLL) for the Gigaset 307x isdn
 * DECT base (aka Sinus 45 isdn) using the RS232 DECT data module M101,
 * written as a line discipline.
 *
 * =====================================================================
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 * =====================================================================
 */

#include "gigaset.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/poll.h>

/* Version Information */
#define DRIVER_AUTHOR "Tilman Schmidt"
#define DRIVER_DESC "Serial Driver for Gigaset 307x using Siemens M101"

#define GIGASET_MINORS     1
#define GIGASET_MINOR      0
#define GIGASET_MODULENAME "ser_gigaset"
#define GIGASET_DEVNAME    "ttyGS"

/* length limit according to Siemens 3070usb-protokoll.doc ch. 2.1 */
#define IF_WRITEBUF 264

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_GIGASET_M101);

static int startmode = SM_ISDN;
module_param(startmode, int, S_IRUGO);
MODULE_PARM_DESC(startmode, "initial operation mode");
static int cidmode = 1;
module_param(cidmode, int, S_IRUGO);
MODULE_PARM_DESC(cidmode, "stay in CID mode when idle");

static struct gigaset_driver *driver;

struct ser_cardstate {
	struct platform_device	dev;
	struct tty_struct	*tty;
	atomic_t		refcnt;
	struct mutex		dead_mutex;
};

static struct platform_driver device_driver = {
	.driver = {
		.name = GIGASET_MODULENAME,
	},
};

static void flush_send_queue(struct cardstate *);

/* transmit data from current open skb
 * result: number of bytes sent or error code < 0
 */
static int write_modem(struct cardstate *cs)
{
	struct tty_struct *tty = cs->hw.ser->tty;
	struct bc_state *bcs = &cs->bcs[0];	/* only one channel */
	struct sk_buff *skb = bcs->tx_skb;
	int sent;

	if (!tty || !tty->driver || !skb)
		return -EFAULT;

	if (!skb->len) {
		dev_kfree_skb_any(skb);
		bcs->tx_skb = NULL;
		return -EINVAL;
	}

	set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	sent = tty->driver->write(tty, skb->data, skb->len);
	gig_dbg(DEBUG_OUTPUT, "write_modem: sent %d", sent);
	if (sent < 0) {
		/* error */
		flush_send_queue(cs);
		return sent;
	}
	skb_pull(skb, sent);
	if (!skb->len) {
		/* skb sent completely */
		gigaset_skb_sent(bcs, skb);

		gig_dbg(DEBUG_INTR, "kfree skb (Adr: %lx)!",
			(unsigned long) skb);
		dev_kfree_skb_any(skb);
		bcs->tx_skb = NULL;
	}
	return sent;
}

/*
 * transmit first queued command buffer
 * result: number of bytes sent or error code < 0
 */
static int send_cb(struct cardstate *cs)
{
	struct tty_struct *tty = cs->hw.ser->tty;
	struct cmdbuf_t *cb, *tcb;
	unsigned long flags;
	int sent = 0;

	if (!tty || !tty->driver)
		return -EFAULT;

	cb = cs->cmdbuf;
	if (!cb)
		return 0;	/* nothing to do */

	if (cb->len) {
		set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
		sent = tty->driver->write(tty, cb->buf + cb->offset, cb->len);
		if (sent < 0) {
			/* error */
			gig_dbg(DEBUG_OUTPUT, "send_cb: write error %d", sent);
			flush_send_queue(cs);
			return sent;
		}
		cb->offset += sent;
		cb->len -= sent;
		gig_dbg(DEBUG_OUTPUT, "send_cb: sent %d, left %u, queued %u",
			sent, cb->len, cs->cmdbytes);
	}

	while (cb && !cb->len) {
		spin_lock_irqsave(&cs->cmdlock, flags);
		cs->cmdbytes -= cs->curlen;
		tcb = cb;
		cs->cmdbuf = cb = cb->next;
		if (cb) {
			cb->prev = NULL;
			cs->curlen = cb->len;
		} else {
			cs->lastcmdbuf = NULL;
			cs->curlen = 0;
		}
		spin_unlock_irqrestore(&cs->cmdlock, flags);

		if (tcb->wake_tasklet)
			tasklet_schedule(tcb->wake_tasklet);
		kfree(tcb);
	}
	return sent;
}

/*
 * send queue tasklet
 * If there is already a skb opened, put data to the transfer buffer
 * by calling "write_modem".
 * Otherwise take a new skb out of the queue.
 */
static void gigaset_modem_fill(unsigned long data)
{
	struct cardstate *cs = (struct cardstate *) data;
	struct bc_state *bcs;
	int sent = 0;

	if (!cs || !(bcs = cs->bcs)) {
		gig_dbg(DEBUG_OUTPUT, "%s: no cardstate", __func__);
		return;
	}
	if (!bcs->tx_skb) {
		/* no skb is being sent; send command if any */
		sent = send_cb(cs);
		gig_dbg(DEBUG_OUTPUT, "%s: send_cb -> %d", __func__, sent);
		if (sent)
			/* something sent or error */
			return;

		/* no command to send; get skb */
		if (!(bcs->tx_skb = skb_dequeue(&bcs->squeue)))
			/* no skb either, nothing to do */
			return;

		gig_dbg(DEBUG_INTR, "Dequeued skb (Adr: %lx)",
			(unsigned long) bcs->tx_skb);
	}

	/* send skb */
	gig_dbg(DEBUG_OUTPUT, "%s: tx_skb", __func__);
	if (write_modem(cs) < 0)
		gig_dbg(DEBUG_OUTPUT, "%s: write_modem failed", __func__);
}

/*
 * throw away all data queued for sending
 */
static void flush_send_queue(struct cardstate *cs)
{
	struct sk_buff *skb;
	struct cmdbuf_t *cb;
	unsigned long flags;

	/* command queue */
	spin_lock_irqsave(&cs->cmdlock, flags);
	while ((cb = cs->cmdbuf) != NULL) {
		cs->cmdbuf = cb->next;
		if (cb->wake_tasklet)
			tasklet_schedule(cb->wake_tasklet);
		kfree(cb);
	}
	cs->cmdbuf = cs->lastcmdbuf = NULL;
	cs->cmdbytes = cs->curlen = 0;
	spin_unlock_irqrestore(&cs->cmdlock, flags);

	/* data queue */
	if (cs->bcs->tx_skb)
		dev_kfree_skb_any(cs->bcs->tx_skb);
	while ((skb = skb_dequeue(&cs->bcs->squeue)) != NULL)
		dev_kfree_skb_any(skb);
}


/* Gigaset Driver Interface */
/* ======================== */

/*
 * queue an AT command string for transmission to the Gigaset device
 * parameters:
 *	cs		controller state structure
 *	buf		buffer containing the string to send
 *	len		number of characters to send
 *	wake_tasklet	tasklet to run when transmission is complete, or NULL
 * return value:
 *	number of bytes queued, or error code < 0
 */
static int gigaset_write_cmd(struct cardstate *cs, const unsigned char *buf,
                             int len, struct tasklet_struct *wake_tasklet)
{
	struct cmdbuf_t *cb;
	unsigned long flags;

	gigaset_dbg_buffer(cs->mstate != MS_LOCKED ?
	                     DEBUG_TRANSCMD : DEBUG_LOCKCMD,
	                   "CMD Transmit", len, buf);

	if (len <= 0)
		return 0;

	if (!(cb = kmalloc(sizeof(struct cmdbuf_t) + len, GFP_ATOMIC))) {
		dev_err(cs->dev, "%s: out of memory!\n", __func__);
		return -ENOMEM;
	}

	memcpy(cb->buf, buf, len);
	cb->len = len;
	cb->offset = 0;
	cb->next = NULL;
	cb->wake_tasklet = wake_tasklet;

	spin_lock_irqsave(&cs->cmdlock, flags);
	cb->prev = cs->lastcmdbuf;
	if (cs->lastcmdbuf)
		cs->lastcmdbuf->next = cb;
	else {
		cs->cmdbuf = cb;
		cs->curlen = len;
	}
	cs->cmdbytes += len;
	cs->lastcmdbuf = cb;
	spin_unlock_irqrestore(&cs->cmdlock, flags);

	spin_lock_irqsave(&cs->lock, flags);
	if (cs->connected)
		tasklet_schedule(&cs->write_tasklet);
	spin_unlock_irqrestore(&cs->lock, flags);
	return len;
}

/*
 * tty_driver.write_room interface routine
 * return number of characters the driver will accept to be written
 * parameter:
 *	controller state structure
 * return value:
 *	number of characters
 */
static int gigaset_write_room(struct cardstate *cs)
{
	unsigned bytes;

	bytes = cs->cmdbytes;
	return bytes < IF_WRITEBUF ? IF_WRITEBUF - bytes : 0;
}

/*
 * tty_driver.chars_in_buffer interface routine
 * return number of characters waiting to be sent
 * parameter:
 *	controller state structure
 * return value:
 *	number of characters
 */
static int gigaset_chars_in_buffer(struct cardstate *cs)
{
	return cs->cmdbytes;
}

/*
 * implementation of ioctl(GIGASET_BRKCHARS)
 * parameter:
 *	controller state structure
 * return value:
 *	-EINVAL (unimplemented function)
 */
static int gigaset_brkchars(struct cardstate *cs, const unsigned char buf[6])
{
	/* not implemented */
	return -EINVAL;
}

/*
 * Open B channel
 * Called by "do_action" in ev-layer.c
 */
static int gigaset_init_bchannel(struct bc_state *bcs)
{
	/* nothing to do for M10x */
	gigaset_bchannel_up(bcs);
	return 0;
}

/*
 * Close B channel
 * Called by "do_action" in ev-layer.c
 */
static int gigaset_close_bchannel(struct bc_state *bcs)
{
	/* nothing to do for M10x */
	gigaset_bchannel_down(bcs);
	return 0;
}

/*
 * Set up B channel structure
 * This is called by "gigaset_initcs" in common.c
 */
static int gigaset_initbcshw(struct bc_state *bcs)
{
	/* unused */
	bcs->hw.ser = NULL;
	return 1;
}

/*
 * Free B channel structure
 * Called by "gigaset_freebcs" in common.c
 */
static int gigaset_freebcshw(struct bc_state *bcs)
{
	/* unused */
	return 1;
}

/*
 * Reinitialize B channel structure
 * This is called by "bcs_reinit" in common.c
 */
static void gigaset_reinitbcshw(struct bc_state *bcs)
{
	/* nothing to do for M10x */
}

/*
 * Free hardware specific device data
 * This will be called by "gigaset_freecs" in common.c
 */
static void gigaset_freecshw(struct cardstate *cs)
{
	tasklet_kill(&cs->write_tasklet);
	if (!cs->hw.ser)
		return;
	dev_set_drvdata(&cs->hw.ser->dev.dev, NULL);
	platform_device_unregister(&cs->hw.ser->dev);
	kfree(cs->hw.ser);
	cs->hw.ser = NULL;
}

static void gigaset_device_release(struct device *dev)
{
	struct platform_device *pdev =
		container_of(dev, struct platform_device, dev);

	/* adapted from platform_device_release() in drivers/base/platform.c */
	//FIXME is this actually necessary?
	kfree(dev->platform_data);
	kfree(pdev->resource);
}

/*
 * Set up hardware specific device data
 * This is called by "gigaset_initcs" in common.c
 */
static int gigaset_initcshw(struct cardstate *cs)
{
	int rc;

	if (!(cs->hw.ser = kzalloc(sizeof(struct ser_cardstate), GFP_KERNEL))) {
		err("%s: out of memory!", __func__);
		return 0;
	}

	cs->hw.ser->dev.name = GIGASET_MODULENAME;
	cs->hw.ser->dev.id = cs->minor_index;
	cs->hw.ser->dev.dev.release = gigaset_device_release;
	if ((rc = platform_device_register(&cs->hw.ser->dev)) != 0) {
		err("error %d registering platform device", rc);
		kfree(cs->hw.ser);
		cs->hw.ser = NULL;
		return 0;
	}
	dev_set_drvdata(&cs->hw.ser->dev.dev, cs);

	tasklet_init(&cs->write_tasklet,
	             &gigaset_modem_fill, (unsigned long) cs);
	return 1;
}

/*
 * set modem control lines
 * Parameters:
 *	card state structure
 *	modem control line state ([TIOCM_DTR]|[TIOCM_RTS])
 * Called by "gigaset_start" and "gigaset_enterconfigmode" in common.c
 * and by "if_lock" and "if_termios" in interface.c
 */
static int gigaset_set_modem_ctrl(struct cardstate *cs, unsigned old_state, unsigned new_state)
{
	struct tty_struct *tty = cs->hw.ser->tty;
	unsigned int set, clear;

	if (!tty || !tty->driver || !tty->driver->tiocmset)
		return -EFAULT;
	set = new_state & ~old_state;
	clear = old_state & ~new_state;
	if (!set && !clear)
		return 0;
	gig_dbg(DEBUG_IF, "tiocmset set %x clear %x", set, clear);
	return tty->driver->tiocmset(tty, NULL, set, clear);
}

static int gigaset_baud_rate(struct cardstate *cs, unsigned cflag)
{
	return -EINVAL;
}

static int gigaset_set_line_ctrl(struct cardstate *cs, unsigned cflag)
{
	return -EINVAL;
}

static const struct gigaset_ops ops = {
	gigaset_write_cmd,
	gigaset_write_room,
	gigaset_chars_in_buffer,
	gigaset_brkchars,
	gigaset_init_bchannel,
	gigaset_close_bchannel,
	gigaset_initbcshw,
	gigaset_freebcshw,
	gigaset_reinitbcshw,
	gigaset_initcshw,
	gigaset_freecshw,
	gigaset_set_modem_ctrl,
	gigaset_baud_rate,
	gigaset_set_line_ctrl,
	gigaset_m10x_send_skb,	/* asyncdata.c */
	gigaset_m10x_input,	/* asyncdata.c */
};


/* Line Discipline Interface */
/* ========================= */

/* helper functions for cardstate refcounting */
static struct cardstate *cs_get(struct tty_struct *tty)
{
	struct cardstate *cs = tty->disc_data;

	if (!cs || !cs->hw.ser) {
		gig_dbg(DEBUG_ANY, "%s: no cardstate", __func__);
		return NULL;
	}
	atomic_inc(&cs->hw.ser->refcnt);
	return cs;
}

static void cs_put(struct cardstate *cs)
{
	if (atomic_dec_and_test(&cs->hw.ser->refcnt))
		mutex_unlock(&cs->hw.ser->dead_mutex);
}

/*
 * Called by the tty driver when the line discipline is pushed onto the tty.
 * Called in process context.
 */
static int
gigaset_tty_open(struct tty_struct *tty)
{
	struct cardstate *cs;

	gig_dbg(DEBUG_INIT, "Starting HLL for Gigaset M101");

	info(DRIVER_AUTHOR);
	info(DRIVER_DESC);

	if (!driver) {
		err("%s: no driver structure", __func__);
		return -ENODEV;
	}

	/* allocate memory for our device state and intialize it */
	if (!(cs = gigaset_initcs(driver, 1, 1, 0, cidmode,
				  GIGASET_MODULENAME)))
		goto error;

	cs->dev = &cs->hw.ser->dev.dev;
	cs->hw.ser->tty = tty;
	mutex_init(&cs->hw.ser->dead_mutex);
	atomic_set(&cs->hw.ser->refcnt, 1);

	tty->disc_data = cs;

	/* OK.. Initialization of the datastructures and the HW is done.. Now
	 * startup system and notify the LL that we are ready to run
	 */
	if (startmode == SM_LOCKED)
		cs->mstate = MS_LOCKED;
	if (!gigaset_start(cs)) {
		tasklet_kill(&cs->write_tasklet);
		goto error;
	}

	gig_dbg(DEBUG_INIT, "Startup of HLL done");
	mutex_lock(&cs->hw.ser->dead_mutex);
	return 0;

error:
	gig_dbg(DEBUG_INIT, "Startup of HLL failed");
	tty->disc_data = NULL;
	gigaset_freecs(cs);
	return -ENODEV;
}

/*
 * Called by the tty driver when the line discipline is removed.
 * Called from process context.
 */
static void
gigaset_tty_close(struct tty_struct *tty)
{
	struct cardstate *cs = tty->disc_data;

	gig_dbg(DEBUG_INIT, "Stopping HLL for Gigaset M101");

	if (!cs) {
		gig_dbg(DEBUG_INIT, "%s: no cardstate", __func__);
		return;
	}

	/* prevent other callers from entering ldisc methods */
	tty->disc_data = NULL;

	if (!cs->hw.ser)
		err("%s: no hw cardstate", __func__);
	else {
		/* wait for running methods to finish */
		if (!atomic_dec_and_test(&cs->hw.ser->refcnt))
			mutex_lock(&cs->hw.ser->dead_mutex);
	}

	/* stop operations */
	gigaset_stop(cs);
	tasklet_kill(&cs->write_tasklet);
	flush_send_queue(cs);
	cs->dev = NULL;
	gigaset_freecs(cs);

	gig_dbg(DEBUG_INIT, "Shutdown of HLL done");
}

/*
 * Called by the tty driver when the tty line is hung up.
 * Wait for I/O to driver to complete and unregister ISDN device.
 * This is already done by the close routine, so just call that.
 * Called from process context.
 */
static int gigaset_tty_hangup(struct tty_struct *tty)
{
	gigaset_tty_close(tty);
	return 0;
}

/*
 * Read on the tty.
 * Unused, received data goes only to the Gigaset driver.
 */
static ssize_t
gigaset_tty_read(struct tty_struct *tty, struct file *file,
		 unsigned char __user *buf, size_t count)
{
	return -EAGAIN;
}

/*
 * Write on the tty.
 * Unused, transmit data comes only from the Gigaset driver.
 */
static ssize_t
gigaset_tty_write(struct tty_struct *tty, struct file *file,
		  const unsigned char *buf, size_t count)
{
	return -EAGAIN;
}

/*
 * Ioctl on the tty.
 * Called in process context only.
 * May be re-entered by multiple ioctl calling threads.
 */
static int
gigaset_tty_ioctl(struct tty_struct *tty, struct file *file,
		  unsigned int cmd, unsigned long arg)
{
	struct cardstate *cs = cs_get(tty);
	int rc, val;
	int __user *p = (int __user *)arg;

	if (!cs)
		return -ENXIO;

	switch (cmd) {
	case TCGETS:
	case TCGETA:
		/* pass through to underlying serial device */
		rc = n_tty_ioctl(tty, file, cmd, arg);
		break;

	case TCFLSH:
		/* flush our buffers and the serial port's buffer */
		switch (arg) {
		case TCIFLUSH:
			/* no own input buffer to flush */
			break;
		case TCIOFLUSH:
		case TCOFLUSH:
			flush_send_queue(cs);
			break;
		}
		/* flush the serial port's buffer */
		rc = n_tty_ioctl(tty, file, cmd, arg);
		break;

	case FIONREAD:
		/* unused, always return zero */
		val = 0;
		rc = put_user(val, p);
		break;

	default:
		rc = -ENOIOCTLCMD;
	}

	cs_put(cs);
	return rc;
}

/*
 * Poll on the tty.
 * Unused, always return zero.
 */
static unsigned int
gigaset_tty_poll(struct tty_struct *tty, struct file *file, poll_table *wait)
{
	return 0;
}

/*
 * Called by the tty driver when a block of data has been received.
 * Will not be re-entered while running but other ldisc functions
 * may be called in parallel.
 * Can be called from hard interrupt level as well as soft interrupt
 * level or mainline.
 * Parameters:
 *	tty	tty structure
 *	buf	buffer containing received characters
 *	cflags	buffer containing error flags for received characters (ignored)
 *	count	number of received characters
 */
static void
gigaset_tty_receive(struct tty_struct *tty, const unsigned char *buf,
		    char *cflags, int count)
{
	struct cardstate *cs = cs_get(tty);
	unsigned tail, head, n;
	struct inbuf_t *inbuf;

	if (!cs)
		return;
	if (!(inbuf = cs->inbuf)) {
		dev_err(cs->dev, "%s: no inbuf\n", __func__);
		cs_put(cs);
		return;
	}

	tail = inbuf->tail;
	head = inbuf->head;
	gig_dbg(DEBUG_INTR, "buffer state: %u -> %u, receive %u bytes",
		head, tail, count);

	if (head <= tail) {
		/* possible buffer wraparound */
		n = min_t(unsigned, count, RBUFSIZE - tail);
		memcpy(inbuf->data + tail, buf, n);
		tail = (tail + n) % RBUFSIZE;
		buf += n;
		count -= n;
	}

	if (count > 0) {
		/* tail < head and some data left */
		n = head - tail - 1;
		if (count > n) {
			dev_err(cs->dev,
				"inbuf overflow, discarding %d bytes\n",
				count - n);
			count = n;
		}
		memcpy(inbuf->data + tail, buf, count);
		tail += count;
	}

	gig_dbg(DEBUG_INTR, "setting tail to %u", tail);
	inbuf->tail = tail;

	/* Everything was received .. Push data into handler */
	gig_dbg(DEBUG_INTR, "%s-->BH", __func__);
	gigaset_schedule_event(cs);
	cs_put(cs);
}

/*
 * Called by the tty driver when there's room for more data to send.
 */
static void
gigaset_tty_wakeup(struct tty_struct *tty)
{
	struct cardstate *cs = cs_get(tty);

	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	if (!cs)
		return;
	tasklet_schedule(&cs->write_tasklet);
	cs_put(cs);
}

static struct tty_ldisc gigaset_ldisc = {
	.owner		= THIS_MODULE,
	.magic		= TTY_LDISC_MAGIC,
	.name		= "ser_gigaset",
	.open		= gigaset_tty_open,
	.close		= gigaset_tty_close,
	.hangup		= gigaset_tty_hangup,
	.read		= gigaset_tty_read,
	.write		= gigaset_tty_write,
	.ioctl		= gigaset_tty_ioctl,
	.poll		= gigaset_tty_poll,
	.receive_buf	= gigaset_tty_receive,
	.write_wakeup	= gigaset_tty_wakeup,
};


/* Initialization / Shutdown */
/* ========================= */

static int __init ser_gigaset_init(void)
{
	int rc;

	gig_dbg(DEBUG_INIT, "%s", __func__);
	if ((rc = platform_driver_register(&device_driver)) != 0) {
		err("error %d registering platform driver", rc);
		return rc;
	}

	/* allocate memory for our driver state and intialize it */
	if (!(driver = gigaset_initdriver(GIGASET_MINOR, GIGASET_MINORS,
					  GIGASET_MODULENAME, GIGASET_DEVNAME,
					  &ops, THIS_MODULE)))
		goto error;

	if ((rc = tty_register_ldisc(N_GIGASET_M101, &gigaset_ldisc)) != 0) {
		err("error %d registering line discipline", rc);
		goto error;
	}

	return 0;

error:
	if (driver) {
		gigaset_freedriver(driver);
		driver = NULL;
	}
	platform_driver_unregister(&device_driver);
	return rc;
}

static void __exit ser_gigaset_exit(void)
{
	int rc;

	gig_dbg(DEBUG_INIT, "%s", __func__);

	if (driver) {
		gigaset_freedriver(driver);
		driver = NULL;
	}

	if ((rc = tty_unregister_ldisc(N_GIGASET_M101)) != 0)
		err("error %d unregistering line discipline", rc);

	platform_driver_unregister(&device_driver);
}

module_init(ser_gigaset_init);
module_exit(ser_gigaset_exit);
