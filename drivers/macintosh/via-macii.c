/*
 * Device driver for the via ADB on (many) Mac II-class machines
 *
 * Based on the original ADB keyboard handler Copyright (c) 1997 Alan Cox
 * Also derived from code Copyright (C) 1996 Paul Mackerras.
 *
 * With various updates provided over the years by Michael Schmitz,
 * Guideo Koerber and others.
 *
 * Rewrite for Unified ADB by Joshua M. Thompson (funaho@jurai.org)
 *
 * 1999-08-02 (jmt) - Initial rewrite for Unified ADB.
 * 2000-03-29 Tony Mantler <tonym@mac.linux-m68k.org>
 * 				- Big overhaul, should actually work now.
 */
 
#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/adb.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/machw.h>
#include <asm/mac_via.h>
#include <asm/io.h>
#include <asm/system.h>

static volatile unsigned char *via;

/* VIA registers - spaced 0x200 bytes apart */
#define RS		0x200		/* skip between registers */
#define B		0		/* B-side data */
#define A		RS		/* A-side data */
#define DIRB		(2*RS)		/* B-side direction (1=output) */
#define DIRA		(3*RS)		/* A-side direction (1=output) */
#define T1CL		(4*RS)		/* Timer 1 ctr/latch (low 8 bits) */
#define T1CH		(5*RS)		/* Timer 1 counter (high 8 bits) */
#define T1LL		(6*RS)		/* Timer 1 latch (low 8 bits) */
#define T1LH		(7*RS)		/* Timer 1 latch (high 8 bits) */
#define T2CL		(8*RS)		/* Timer 2 ctr/latch (low 8 bits) */
#define T2CH		(9*RS)		/* Timer 2 counter (high 8 bits) */
#define SR		(10*RS)		/* Shift register */
#define ACR		(11*RS)		/* Auxiliary control register */
#define PCR		(12*RS)		/* Peripheral control register */
#define IFR		(13*RS)		/* Interrupt flag register */
#define IER		(14*RS)		/* Interrupt enable register */
#define ANH		(15*RS)		/* A-side data, no handshake */

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

/* ADB transaction states according to GMHW */
#define ST_CMD		0x00		/* ADB state: command byte */
#define ST_EVEN		0x10		/* ADB state: even data byte */
#define ST_ODD		0x20		/* ADB state: odd data byte */
#define ST_IDLE		0x30		/* ADB state: idle, nothing to send */

static int  macii_init_via(void);
static void macii_start(void);
static irqreturn_t macii_interrupt(int irq, void *arg);
static void macii_retransmit(int);
static void macii_queue_poll(void);

static int macii_probe(void);
static int macii_init(void);
static int macii_send_request(struct adb_request *req, int sync);
static int macii_write(struct adb_request *req);
static int macii_autopoll(int devs);
static void macii_poll(void);
static int macii_reset_bus(void);

struct adb_driver via_macii_driver = {
	"Mac II",
	macii_probe,
	macii_init,
	macii_send_request,
	macii_autopoll,
	macii_poll,
	macii_reset_bus
};

static enum macii_state {
	idle,
	sending,
	reading,
	read_done,
	awaiting_reply
} macii_state;

static int need_poll    = 0;
static int command_byte = 0;
static int last_reply   = 0;
static int last_active  = 0;

static struct adb_request *current_req;
static struct adb_request *last_req;
static struct adb_request *retry_req;
static unsigned char reply_buf[16];
static unsigned char *reply_ptr;
static int reply_len;
static int reading_reply;
static int data_index;
static int first_byte;
static int prefix_len;
static int status = ST_IDLE|TREQ;
static int last_status;
static int driver_running = 0;

/* debug level 10 required for ADB logging (should be && debug_adb, ideally) */

/* Check for MacII style ADB */
static int macii_probe(void)
{
	if (macintosh_config->adb_type != MAC_ADB_II) return -ENODEV;

	via = via1;

	printk("adb: Mac II ADB Driver v1.0 for Unified ADB\n");
	return 0;
}

/* Initialize the driver */
int macii_init(void)
{
	unsigned long flags;
	int err;
	
	local_irq_save(flags);
	
	err = macii_init_via();
	if (err) return err;

	err = request_irq(IRQ_MAC_ADB, macii_interrupt, IRQ_FLG_LOCK, "ADB",
			  macii_interrupt);
	if (err) return err;

	macii_state = idle;
	local_irq_restore(flags);
	return 0;
}

/* initialize the hardware */	
static int macii_init_via(void)
{
	unsigned char x;

	/* Set the lines up. We want TREQ as input TACK|TIP as output */
	via[DIRB] = (via[DIRB] | TACK | TIP) & ~TREQ;

	/* Set up state: idle */
	via[B] |= ST_IDLE;
	last_status = via[B] & (ST_MASK|TREQ);

	/* Shift register on input */
	via[ACR] = (via[ACR] & ~SR_CTRL) | SR_EXT;

	/* Wipe any pending data and int */
	x = via[SR];

	return 0;
}

/* Send an ADB poll (Talk Register 0 command, tagged on the front of the request queue) */
static void macii_queue_poll(void)
{
	static int device = 0;
	static int in_poll=0;
	static struct adb_request req;
	unsigned long flags;
	
	if (in_poll) printk("macii_queue_poll: double poll!\n");

	in_poll++;
	if (++device > 15) device = 1;

	adb_request(&req, NULL, ADBREQ_REPLY|ADBREQ_NOSEND, 1,
		    ADB_READREG(device, 0));

	local_irq_save(flags);

	req.next = current_req;
	current_req = &req;

	local_irq_restore(flags);
	macii_start();
	in_poll--;
}

/* Send an ADB retransmit (Talk, appended to the request queue) */
static void macii_retransmit(int device)
{
	static int in_retransmit = 0;
	static struct adb_request rt;
	unsigned long flags;
	
	if (in_retransmit) printk("macii_retransmit: double retransmit!\n");

	in_retransmit++;

	adb_request(&rt, NULL, ADBREQ_REPLY|ADBREQ_NOSEND, 1,
		    ADB_READREG(device, 0));

	local_irq_save(flags);

	if (current_req != NULL) {
		last_req->next = &rt;
		last_req = &rt;
	} else {
		current_req = &rt;
		last_req = &rt;
	}

	if (macii_state == idle) macii_start();

	local_irq_restore(flags);
	in_retransmit--;
}

/* Send an ADB request; if sync, poll out the reply 'till it's done */
static int macii_send_request(struct adb_request *req, int sync)
{
	int i;

	i = macii_write(req);
	if (i) return i;

	if (sync) {
		while (!req->complete) macii_poll();
	}
	return 0;
}

/* Send an ADB request */
static int macii_write(struct adb_request *req)
{
	unsigned long flags;

	if (req->nbytes < 2 || req->data[0] != ADB_PACKET || req->nbytes > 15) {
		req->complete = 1;
		return -EINVAL;
	}
	
	req->next = NULL;
	req->sent = 0;
	req->complete = 0;
	req->reply_len = 0;

	local_irq_save(flags);

	if (current_req != NULL) {
		last_req->next = req;
		last_req = req;
	} else {
		current_req = req;
		last_req = req;
		if (macii_state == idle) macii_start();
	}

	local_irq_restore(flags);
	return 0;
}

/* Start auto-polling */
static int macii_autopoll(int devs)
{
	/* Just ping a random default address */
	if (!(current_req || retry_req))
		macii_retransmit( (last_active < 16 && last_active > 0) ? last_active : 3);
	return 0;
}

/* Prod the chip without interrupts */
static void macii_poll(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (via[IFR] & SR_INT) macii_interrupt(0, NULL);
	local_irq_restore(flags);
}

/* Reset the bus */
static int macii_reset_bus(void)
{
	static struct adb_request req;
	
	/* Command = 0, Address = ignored */
	adb_request(&req, NULL, 0, 1, ADB_BUSRESET);

	return 0;
}

/* Start sending ADB packet */
static void macii_start(void)
{
	unsigned long flags;
	struct adb_request *req;

	req = current_req;
	if (!req) return;
	
	/* assert macii_state == idle */
	if (macii_state != idle) {
		printk("macii_start: called while driver busy (%p %x %x)!\n",
			req, macii_state, (uint) via1[B] & (ST_MASK|TREQ));
		return;
	}

	local_irq_save(flags);
	
	/* 
	 * IRQ signaled ?? (means ADB controller wants to send, or might 
	 * be end of packet if we were reading)
	 */
#if 0 /* FIXME: This is broke broke broke, for some reason */
	if ((via[B] & TREQ) == 0) {
		printk("macii_start: weird poll stuff. huh?\n");
		/*
		 *	FIXME - we need to restart this on a timer
		 *	or a collision at boot hangs us.
		 *	Never set macii_state to idle here, or macii_start 
		 *	won't be called again from send_request!
		 *	(need to re-check other cases ...)
		 */
		/*
		 * if the interrupt handler set the need_poll
		 * flag, it's hopefully a SRQ poll or re-Talk
		 * so we try to send here anyway
		 */
		if (!need_poll) {
			if (console_loglevel == 10)
				printk("macii_start: device busy - retry %p state %d status %x!\n", 
					req, macii_state,
					(uint) via[B] & (ST_MASK|TREQ));
			retry_req = req;
			/* set ADB status here ? */
			local_irq_restore(flags);
			return;
		} else {
			need_poll = 0;
		}
	}
#endif
	/*
	 * Another retry pending? (sanity check)
	 */
	if (retry_req) {
		retry_req = NULL;
	}

	/* Now send it. Be careful though, that first byte of the request */
	/* is actually ADB_PACKET; the real data begins at index 1!	  */
	
	/* store command byte */
	command_byte = req->data[1];
	/* Output mode */
	via[ACR] |= SR_OUT;
	/* Load data */
	via[SR] = req->data[1];
	/* set ADB state to 'command' */
	via[B] = (via[B] & ~ST_MASK) | ST_CMD;

	macii_state = sending;
	data_index = 2;

	local_irq_restore(flags);
}

/*
 * The notorious ADB interrupt handler - does all of the protocol handling, 
 * except for starting new send operations. Relies heavily on the ADB 
 * controller sending and receiving data, thereby generating SR interrupts
 * for us. This means there has to be always activity on the ADB bus, otherwise
 * the whole process dies and has to be re-kicked by sending TALK requests ...
 * CUDA-based Macs seem to solve this with the autopoll option, for MacII-type
 * ADB the problem isn't solved yet (retransmit of the latest active TALK seems
 * a good choice; either on timeout or on a timer interrupt).
 *
 * The basic ADB state machine was left unchanged from the original MacII code
 * by Alan Cox, which was based on the CUDA driver for PowerMac. 
 * The syntax of the ADB status lines seems to be totally different on MacII, 
 * though. MacII uses the states Command -> Even -> Odd -> Even ->...-> Idle for
 * sending, and Idle -> Even -> Odd -> Even ->...-> Idle for receiving. Start 
 * and end of a receive packet are signaled by asserting /IRQ on the interrupt
 * line. Timeouts are signaled by a sequence of 4 0xFF, with /IRQ asserted on 
 * every other byte. SRQ is probably signaled by 3 or more 0xFF tacked on the 
 * end of a packet. (Thanks to Guido Koerber for eavesdropping on the ADB 
 * protocol with a logic analyzer!!)
 *
 * Note: As of 21/10/97, the MacII ADB part works including timeout detection
 * and retransmit (Talk to the last active device).
 */
static irqreturn_t macii_interrupt(int irq, void *arg)
{
	int x, adbdir;
	unsigned long flags;
	struct adb_request *req;

	last_status = status;

	/* prevent races due to SCSI enabling ints */
	local_irq_save(flags);

	if (driver_running) {
		local_irq_restore(flags);
		return IRQ_NONE;
	}

	driver_running = 1;
	
	status = via[B] & (ST_MASK|TREQ);
	adbdir = via[ACR] & SR_OUT;

	switch (macii_state) {
		case idle:
			x = via[SR];
			first_byte = x;
			/* set ADB state = even for first data byte */
			via[B] = (via[B] & ~ST_MASK) | ST_EVEN;

			reply_buf[0] = first_byte; /* was command_byte?? */
			reply_ptr = reply_buf + 1;
			reply_len = 1;
			prefix_len = 1;
			reading_reply = 0;
			
			macii_state = reading;
			break;

		case awaiting_reply:
			/* handshake etc. for II ?? */
			x = via[SR];
			first_byte = x;
			/* set ADB state = even for first data byte */
			via[B] = (via[B] & ~ST_MASK) | ST_EVEN;

			current_req->reply[0] = first_byte;
			reply_ptr = current_req->reply + 1;
			reply_len = 1;
			prefix_len = 1;
			reading_reply = 1;

			macii_state = reading;			
			break;

		case sending:
			req = current_req;
			if (data_index >= req->nbytes) {
				/* print an error message if a listen command has no data */
				if (((command_byte & 0x0C) == 0x08)
				 /* && (console_loglevel == 10) */
				    && (data_index == 2))
					printk("MacII ADB: listen command with no data: %x!\n", 
						command_byte);
				/* reset to shift in */
				via[ACR] &= ~SR_OUT;
				x = via[SR];
				/* set ADB state idle - might get SRQ */
				via[B] = (via[B] & ~ST_MASK) | ST_IDLE;

				req->sent = 1;

				if (req->reply_expected) {
					macii_state = awaiting_reply;
				} else {
					req->complete = 1;
					current_req = req->next;
					if (req->done) (*req->done)(req);
					macii_state = idle;
					if (current_req || retry_req)
						macii_start();
					else
						macii_retransmit((command_byte & 0xF0) >> 4);
				}
			} else {
				via[SR] = req->data[data_index++];

				if ( (via[B] & ST_MASK) == ST_CMD ) {
					/* just sent the command byte, set to EVEN */
					via[B] = (via[B] & ~ST_MASK) | ST_EVEN;
				} else {
					/* invert state bits, toggle ODD/EVEN */
					via[B] ^= ST_MASK;
				}
			}
			break;

		case reading:

			/* timeout / SRQ handling for II hw */
			if( (first_byte == 0xFF && (reply_len-prefix_len)==2 
			     && memcmp(reply_ptr-2,"\xFF\xFF",2)==0) || 
			    ((reply_len-prefix_len)==3 
			     && memcmp(reply_ptr-3,"\xFF\xFF\xFF",3)==0))
			{
				/*
				 * possible timeout (in fact, most probably a 
				 * timeout, since SRQ can't be signaled without
				 * transfer on the bus).
				 * The last three bytes seen were FF, together 
				 * with the starting byte (in case we started
				 * on 'idle' or 'awaiting_reply') this probably
				 * makes four. So this is mostl likely #5!
				 * The timeout signal is a pattern 1 0 1 0 0..
				 * on /INT, meaning we missed it :-(
				 */
				x = via[SR];
				if (x != 0xFF) printk("MacII ADB: mistaken timeout/SRQ!\n");

				if ((status & TREQ) == (last_status & TREQ)) {
					/* Not a timeout. Unsolicited SRQ? weird. */
					/* Terminate the SRQ packet and poll */
					need_poll = 1;
				}
				/* There's no packet to get, so reply is blank */
				via[B] ^= ST_MASK;
				reply_ptr -= (reply_len-prefix_len);
				reply_len = prefix_len;
				macii_state = read_done;
				break;
			} /* end timeout / SRQ handling for II hw. */

			if((reply_len-prefix_len)>3
				&& memcmp(reply_ptr-3,"\xFF\xFF\xFF",3)==0)
			{
				/* SRQ tacked on data packet */
				/* Terminate the packet (SRQ never ends) */
				x = via[SR];
				macii_state = read_done;
				reply_len -= 3;
				reply_ptr -= 3;
				need_poll = 1;
				/* need to continue; next byte not seen else */
			} else {
				/* Sanity check */
				if (reply_len > 15) reply_len = 0;
				/* read byte */
				x = via[SR];
				*reply_ptr = x;
				reply_ptr++;
				reply_len++;
			}
			/* The usual handshake ... */

			/*
			 * NetBSD hints that the next to last byte 
			 * is sent with IRQ !! 
			 * Guido found out it's the last one (0x0),
			 * but IRQ should be asserted already.
			 * Problem with timeout detection: First
			 * transition to /IRQ might be second 
			 * byte of timeout packet! 
			 * Timeouts are signaled by 4x FF.
			 */
			if (((status & TREQ) == 0) && (x == 0x00)) { /* != 0xFF */
				/* invert state bits, toggle ODD/EVEN */
				via[B] ^= ST_MASK;

				/* adjust packet length */
				reply_len--;
				reply_ptr--;
				macii_state = read_done;
			} else {
				/* not caught: ST_CMD */
				/* required for re-entry 'reading'! */
				if ((status & ST_MASK) == ST_IDLE) {
					/* (in)sanity check - set even */
					via[B] = (via[B] & ~ST_MASK) | ST_EVEN;
				} else {
					/* invert state bits */
					via[B] ^= ST_MASK;
				}
			}
			break;

		case read_done:
			x = via[SR];
			if (reading_reply) {
				req = current_req;
				req->reply_len = reply_ptr - req->reply;
				req->complete = 1;
				current_req = req->next;
				if (req->done) (*req->done)(req);
			} else {
				adb_input(reply_buf, reply_ptr - reply_buf, 0);
			}

			/*
			 * remember this device ID; it's the latest we got a 
			 * reply from!
			 */
			last_reply = command_byte;
			last_active = (command_byte & 0xF0) >> 4;

			/* SRQ seen before, initiate poll now */
			if (need_poll) {
				macii_state = idle;
				macii_queue_poll();
				need_poll = 0;
				break;
			}
			
			/* set ADB state to idle */
			via[B] = (via[B] & ~ST_MASK) | ST_IDLE;
			
			/* /IRQ seen, so the ADB controller has data for us */
			if ((via[B] & TREQ) != 0) {
				macii_state = reading;

				reply_buf[0] = command_byte;
				reply_ptr = reply_buf + 1;
				reply_len = 1;
				prefix_len = 1;
				reading_reply = 0;
			} else {
				/* no IRQ, send next packet or wait */
				macii_state = idle;
				if (current_req)
					macii_start();
				else
					macii_retransmit(last_active);
			}
			break;

		default:
		break;
	}
	/* reset mutex and interrupts */
	driver_running = 0;
	local_irq_restore(flags);
	return IRQ_HANDLED;
}
