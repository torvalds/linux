/*
 * Device driver for the IIsi-style ADB on some Mac LC and II-class machines
 *
 * Based on via-cuda.c and via-macii.c, as well as the original
 * adb-bus.c, which in turn is somewhat influenced by (but uses no
 * code from) the NetBSD HWDIRECT ADB code.  Original IIsi driver work
 * was done by Robert Thompson and integrated into the old style
 * driver by Michael Schmitz.
 *
 * Original sources (c) Alan Cox, Paul Mackerras, and others.
 *
 * Rewritten for Unified ADB by David Huggins-Daines <dhd@debian.org>
 * 
 * 7/13/2000- extensive changes by Andrew McPherson <andrew@macduff.dhs.org>
 * Works about 30% of the time now.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_via.h>

static volatile unsigned char *via;

/* VIA registers - spaced 0x200 bytes apart - only the ones we actually use */
#define RS		0x200		/* skip between registers */
#define B		0		/* B-side data */
#define A		RS		/* A-side data */
#define DIRB		(2*RS)		/* B-side direction (1=output) */
#define DIRA		(3*RS)		/* A-side direction (1=output) */
#define SR		(10*RS)		/* Shift register */
#define ACR		(11*RS)		/* Auxiliary control register */
#define IFR		(13*RS)		/* Interrupt flag register */
#define IER		(14*RS)		/* Interrupt enable register */

/* Bits in B data register: all active low */
#define TREQ		0x08		/* Transfer request (input) */
#define TACK		0x10		/* Transfer acknowledge (output) */
#define TIP		0x20		/* Transfer in progress (output) */
#define ST_MASK		0x30		/* mask for selecting ADB state bits */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */
#define SR_DATA		0x08		/* Shift register data */
#define SR_CLOCK	0x10		/* Shift register clock */

#define ADB_DELAY 150

#undef DEBUG_MACIISI_ADB

static struct adb_request* current_req;
static struct adb_request* last_req;
static unsigned char maciisi_rbuf[16];
static unsigned char *reply_ptr;
static int data_index;
static int reading_reply;
static int reply_len;
static int tmp;
static int need_sync;

static enum maciisi_state {
    idle,
    sending,
    reading,
} maciisi_state;

static int maciisi_probe(void);
static int maciisi_init(void);
static int maciisi_send_request(struct adb_request* req, int sync);
static void maciisi_sync(struct adb_request *req);
static int maciisi_write(struct adb_request* req);
static irqreturn_t maciisi_interrupt(int irq, void* arg);
static void maciisi_input(unsigned char *buf, int nb);
static int maciisi_init_via(void);
static void maciisi_poll(void);
static int maciisi_start(void);

struct adb_driver via_maciisi_driver = {
	"Mac IIsi",
	maciisi_probe,
	maciisi_init,
	maciisi_send_request,
	NULL, /* maciisi_adb_autopoll, */
	maciisi_poll,
	NULL /* maciisi_reset_adb_bus */
};

static int
maciisi_probe(void)
{
	if (macintosh_config->adb_type != MAC_ADB_IISI)
		return -ENODEV;

	via = via1;
	return 0;
}

static int
maciisi_init(void)
{
	int err;

	if (via == NULL)
		return -ENODEV;

	if ((err = maciisi_init_via())) {
		printk(KERN_ERR "maciisi_init: maciisi_init_via() failed, code %d\n", err);
		via = NULL;
		return err;
	}

	if (request_irq(IRQ_MAC_ADB, maciisi_interrupt, IRQ_FLG_LOCK | IRQ_FLG_FAST, 
			"ADB", maciisi_interrupt)) {
		printk(KERN_ERR "maciisi_init: can't get irq %d\n", IRQ_MAC_ADB);
		return -EAGAIN;
	}

	printk("adb: Mac IIsi driver v0.2 for Unified ADB.\n");
	return 0;
}

/* Flush data from the ADB controller */
static void
maciisi_stfu(void)
{
	int status = via[B] & (TIP|TREQ);

	if (status & TREQ) {
#ifdef DEBUG_MACIISI_ADB
		printk (KERN_DEBUG "maciisi_stfu called with TREQ high!\n");
#endif
		return;
	}
	
	udelay(ADB_DELAY);
	via[ACR] &= ~SR_OUT;
	via[IER] = IER_CLR | SR_INT;

	udelay(ADB_DELAY);

	status = via[B] & (TIP|TREQ);

	if (!(status & TREQ))
	{
		via[B] |= TIP;

		while(1)
		{
			int poll_timeout = ADB_DELAY * 5;
			/* Poll for SR interrupt */
			while (!(via[IFR] & SR_INT) && poll_timeout-- > 0)
				status = via[B] & (TIP|TREQ);

			tmp = via[SR]; /* Clear shift register */
#ifdef DEBUG_MACIISI_ADB
			printk(KERN_DEBUG "maciisi_stfu: status %x timeout %d data %x\n",
			       status, poll_timeout, tmp);
#endif	
			if(via[B] & TREQ)
				break;
	
			/* ACK on-off */
			via[B] |= TACK;
			udelay(ADB_DELAY);
			via[B] &= ~TACK;
		}

		/* end frame */
		via[B] &= ~TIP;
		udelay(ADB_DELAY);
	}

	via[IER] = IER_SET | SR_INT;	
}

/* All specifically VIA-related initialization goes here */
static int
maciisi_init_via(void)
{
	int	i;
	
	/* Set the lines up. We want TREQ as input TACK|TIP as output */
	via[DIRB] = (via[DIRB] | TACK | TIP) & ~TREQ;
	/* Shift register on input */
	via[ACR]  = (via[ACR] & ~SR_CTRL) | SR_EXT;
#ifdef DEBUG_MACIISI_ADB
	printk(KERN_DEBUG "maciisi_init_via: initial status %x\n", via[B] & (TIP|TREQ));
#endif
	/* Wipe any pending data and int */
	tmp = via[SR];
	/* Enable keyboard interrupts */
	via[IER] = IER_SET | SR_INT;
	/* Set initial state: idle */
	via[B] &= ~(TACK|TIP);
	/* Clear interrupt bit */
	via[IFR] = SR_INT;

	for(i = 0; i < 60; i++) {
		udelay(ADB_DELAY);
		maciisi_stfu();
		udelay(ADB_DELAY);
		if(via[B] & TREQ)
			break;
	}
	if (i == 60)
		printk(KERN_ERR "maciisi_init_via: bus jam?\n");

	maciisi_state = idle;
	need_sync = 0;

	return 0;
}

/* Send a request, possibly waiting for a reply */
static int
maciisi_send_request(struct adb_request* req, int sync)
{
	int i;

#ifdef DEBUG_MACIISI_ADB
	static int dump_packet = 0;
#endif

	if (via == NULL) {
		req->complete = 1;
		return -ENXIO;
	}

#ifdef DEBUG_MACIISI_ADB
	if (dump_packet) {
		printk(KERN_DEBUG "maciisi_send_request:");
		for (i = 0; i < req->nbytes; i++) {
			printk(" %.2x", req->data[i]);
		}
		printk(" sync %d\n", sync);
	}
#endif

	req->reply_expected = 1;
	
	i = maciisi_write(req);
	if (i)
	{
		/* Normally, if a packet requires syncing, that happens at the end of
		 * maciisi_send_request. But if the transfer fails, it will be restarted
		 * by maciisi_interrupt(). We use need_sync to tell maciisi_interrupt
		 * when to sync a packet that it sends out.
		 * 
		 * Suggestions on a better way to do this are welcome.
		 */
		if(i == -EBUSY && sync)
			need_sync = 1;
		else
			need_sync = 0;
		return i;
	}
	if(sync)
		maciisi_sync(req);
	
	return 0;
}

/* Poll the ADB chip until the request completes */
static void maciisi_sync(struct adb_request *req)
{
	int count = 0; 

#ifdef DEBUG_MACIISI_ADB
	printk(KERN_DEBUG "maciisi_sync called\n");
#endif

	/* If for some reason the ADB chip shuts up on us, we want to avoid an endless loop. */
	while (!req->complete && count++ < 50) {
		maciisi_poll();
	}
	/* This could be BAD... when the ADB controller doesn't respond
	 * for this long, it's probably not coming back :-( */
	if(count >= 50) /* Hopefully shouldn't happen */
		printk(KERN_ERR "maciisi_send_request: poll timed out!\n");
}

int
maciisi_request(struct adb_request *req, void (*done)(struct adb_request *),
	    int nbytes, ...)
{
	va_list list;
	int i;

	req->nbytes = nbytes;
	req->done = done;
	req->reply_expected = 0;
	va_start(list, nbytes);
	for (i = 0; i < nbytes; i++)
		req->data[i++] = va_arg(list, int);
	va_end(list);

	return maciisi_send_request(req, 1);
}

/* Enqueue a request, and run the queue if possible */
static int
maciisi_write(struct adb_request* req)
{
	unsigned long flags;
	int i;

	/* We will accept CUDA packets - the VIA sends them to us, so
           it figures that we should be able to send them to it */
	if (req->nbytes < 2 || req->data[0] > CUDA_PACKET) {
		printk(KERN_ERR "maciisi_write: packet too small or not an ADB or CUDA packet\n");
		req->complete = 1;
		return -EINVAL;
	}
	req->next = NULL;
	req->sent = 0;
	req->complete = 0;
	req->reply_len = 0;
	
	local_irq_save(flags);

	if (current_req) {
		last_req->next = req;
		last_req = req;
	} else {
		current_req = req;
		last_req = req;
	}
	if (maciisi_state == idle)
	{
		i = maciisi_start();
		if(i != 0)
		{
			local_irq_restore(flags);
			return i;
		}
	}
	else
	{
#ifdef DEBUG_MACIISI_ADB
		printk(KERN_DEBUG "maciisi_write: would start, but state is %d\n", maciisi_state);
#endif
		local_irq_restore(flags);
		return -EBUSY;
	}

	local_irq_restore(flags);

	return 0;
}

static int
maciisi_start(void)
{
	struct adb_request* req;
	int status;

#ifdef DEBUG_MACIISI_ADB
	status = via[B] & (TIP | TREQ);

	printk(KERN_DEBUG "maciisi_start called, state=%d, status=%x, ifr=%x\n", maciisi_state, status, via[IFR]);
#endif

	if (maciisi_state != idle) {
		/* shouldn't happen */
		printk(KERN_ERR "maciisi_start: maciisi_start called when driver busy!\n");
		return -EBUSY;
	}

	req = current_req;
	if (req == NULL)
		return -EINVAL;

	status = via[B] & (TIP|TREQ);
	if (!(status & TREQ)) {
#ifdef DEBUG_MACIISI_ADB
		printk(KERN_DEBUG "maciisi_start: bus busy - aborting\n");
#endif
		return -EBUSY;
	}

	/* Okay, send */
#ifdef DEBUG_MACIISI_ADB
	printk(KERN_DEBUG "maciisi_start: sending\n");
#endif
	/* Set state to active */
	via[B] |= TIP;
	/* ACK off */
	via[B] &= ~TACK;
	/* Delay */
	udelay(ADB_DELAY);
	/* Shift out and send */
	via[ACR] |= SR_OUT;
	via[SR] = req->data[0];
	data_index = 1;
	/* ACK on */
	via[B] |= TACK;
	maciisi_state = sending;

	return 0;
}

void
maciisi_poll(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (via[IFR] & SR_INT) {
		maciisi_interrupt(0, NULL);
	}
	else /* avoid calling this function too quickly in a loop */
		udelay(ADB_DELAY);

	local_irq_restore(flags);
}

/* Shift register interrupt - this is *supposed* to mean that the
   register is either full or empty. In practice, I have no idea what
   it means :( */
static irqreturn_t
maciisi_interrupt(int irq, void* arg)
{
	int status;
	struct adb_request *req;
#ifdef DEBUG_MACIISI_ADB
	static int dump_reply = 0;
#endif
	int i;
	unsigned long flags;

	local_irq_save(flags);

	status = via[B] & (TIP|TREQ);
#ifdef DEBUG_MACIISI_ADB
	printk(KERN_DEBUG "state %d status %x ifr %x\n", maciisi_state, status, via[IFR]);
#endif

	if (!(via[IFR] & SR_INT)) {
		/* Shouldn't happen, we hope */
		printk(KERN_ERR "maciisi_interrupt: called without interrupt flag set\n");
		local_irq_restore(flags);
		return IRQ_NONE;
	}

	/* Clear the interrupt */
	/* via[IFR] = SR_INT; */

 switch_start:
	switch (maciisi_state) {
	case idle:
		if (status & TIP)
			printk(KERN_ERR "maciisi_interrupt: state is idle but TIP asserted!\n");

		if(!reading_reply)
			udelay(ADB_DELAY);
		/* Shift in */
		via[ACR] &= ~SR_OUT;
 		/* Signal start of frame */
		via[B] |= TIP;
		/* Clear the interrupt (throw this value on the floor, it's useless) */
		tmp = via[SR];
		/* ACK adb chip, high-low */
		via[B] |= TACK;
		udelay(ADB_DELAY);
		via[B] &= ~TACK;
		reply_len = 0;
		maciisi_state = reading;
		if (reading_reply) {
			reply_ptr = current_req->reply;
		} else {
			reply_ptr = maciisi_rbuf;
		}
		break;

	case sending:
		/* via[SR]; */
		/* Set ACK off */
		via[B] &= ~TACK;
		req = current_req;

		if (!(status & TREQ)) {
			/* collision */
			printk(KERN_ERR "maciisi_interrupt: send collision\n");
			/* Set idle and input */
			via[ACR] &= ~SR_OUT;
			tmp = via[SR];
			via[B] &= ~TIP;
			/* Must re-send */
			reading_reply = 0;
			reply_len = 0;
			maciisi_state = idle;
			udelay(ADB_DELAY);
			/* process this now, because the IFR has been cleared */
			goto switch_start;
		}

		udelay(ADB_DELAY);

		if (data_index >= req->nbytes) {
			/* Sent the whole packet, put the bus back in idle state */
			/* Shift in, we are about to read a reply (hopefully) */
			via[ACR] &= ~SR_OUT;
			tmp = via[SR];
			/* End of frame */
			via[B] &= ~TIP;
			req->sent = 1;
			maciisi_state = idle;
			if (req->reply_expected) {
				/* Note: only set this once we've
                                   successfully sent the packet */
				reading_reply = 1;
			} else {
				current_req = req->next;
				if (req->done)
					(*req->done)(req);
				/* Do any queued requests now */
				i = maciisi_start();
				if(i == 0 && need_sync) {
					/* Packet needs to be synced */
					maciisi_sync(current_req);
				}
				if(i != -EBUSY)
					need_sync = 0;
			}
		} else {
			/* Sending more stuff */
			/* Shift out */
			via[ACR] |= SR_OUT;
			/* Write */
			via[SR] = req->data[data_index++];
			/* Signal 'byte ready' */
			via[B] |= TACK;
		}
		break;

	case reading:
		/* Shift in */
		/* via[ACR] &= ~SR_OUT; */ /* Not in 2.2 */
		if (reply_len++ > 16) {
			printk(KERN_ERR "maciisi_interrupt: reply too long, aborting read\n");
			via[B] |= TACK;
			udelay(ADB_DELAY);
			via[B] &= ~(TACK|TIP);
			maciisi_state = idle;
			i = maciisi_start();
			if(i == 0 && need_sync) {
				/* Packet needs to be synced */
				maciisi_sync(current_req);
			}
			if(i != -EBUSY)
				need_sync = 0;
			break;
		}
		/* Read data */
		*reply_ptr++ = via[SR];
		status = via[B] & (TIP|TREQ);
		/* ACK on/off */
		via[B] |= TACK;
		udelay(ADB_DELAY);
		via[B] &= ~TACK;	
		if (!(status & TREQ))
			break; /* more stuff to deal with */
		
		/* end of frame */
		via[B] &= ~TIP;
		tmp = via[SR]; /* That's what happens in 2.2 */
		udelay(ADB_DELAY); /* Give controller time to recover */

		/* end of packet, deal with it */
		if (reading_reply) {
			req = current_req;
			req->reply_len = reply_ptr - req->reply;
			if (req->data[0] == ADB_PACKET) {
				/* Have to adjust the reply from ADB commands */
				if (req->reply_len <= 2 || (req->reply[1] & 2) != 0) {
					/* the 0x2 bit indicates no response */
					req->reply_len = 0;
				} else {
					/* leave just the command and result bytes in the reply */
					req->reply_len -= 2;
					memmove(req->reply, req->reply + 2, req->reply_len);
				}
			}
#ifdef DEBUG_MACIISI_ADB
			if (dump_reply) {
				int i;
				printk(KERN_DEBUG "maciisi_interrupt: reply is ");
				for (i = 0; i < req->reply_len; ++i)
					printk(" %.2x", req->reply[i]);
				printk("\n");
			}
#endif
			req->complete = 1;
			current_req = req->next;
			if (req->done)
				(*req->done)(req);
			/* Obviously, we got it */
			reading_reply = 0;
		} else {
			maciisi_input(maciisi_rbuf, reply_ptr - maciisi_rbuf);
		}
		maciisi_state = idle;
		status = via[B] & (TIP|TREQ);
		if (!(status & TREQ)) {
			/* Timeout?! More likely, another packet coming in already */
#ifdef DEBUG_MACIISI_ADB
			printk(KERN_DEBUG "extra data after packet: status %x ifr %x\n",
			       status, via[IFR]);
#endif
#if 0
			udelay(ADB_DELAY);
			via[B] |= TIP;

			maciisi_state = reading;
			reading_reply = 0;
			reply_ptr = maciisi_rbuf;
#else
			/* Process the packet now */
			reading_reply = 0;
			goto switch_start;
#endif
			/* We used to do this... but the controller might actually have data for us */
			/* maciisi_stfu(); */
		}
		else {
			/* Do any queued requests now if possible */
			i = maciisi_start();
			if(i == 0 && need_sync) {
				/* Packet needs to be synced */
				maciisi_sync(current_req);
			}
			if(i != -EBUSY)
				need_sync = 0;
		}
		break;

	default:
		printk("maciisi_interrupt: unknown maciisi_state %d?\n", maciisi_state);
	}
	local_irq_restore(flags);
	return IRQ_HANDLED;
}

static void
maciisi_input(unsigned char *buf, int nb)
{
#ifdef DEBUG_MACIISI_ADB
    int i;
#endif

    switch (buf[0]) {
    case ADB_PACKET:
	    adb_input(buf+2, nb-2, buf[1] & 0x40);
	    break;
    default:
#ifdef DEBUG_MACIISI_ADB
	    printk(KERN_DEBUG "data from IIsi ADB (%d bytes):", nb);
	    for (i = 0; i < nb; ++i)
		    printk(" %.2x", buf[i]);
	    printk("\n");
#endif
	    break;
    }
}
