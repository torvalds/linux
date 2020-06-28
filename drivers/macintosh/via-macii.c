// SPDX-License-Identifier: GPL-2.0
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
 *            - Big overhaul, should actually work now.
 * 2006-12-31 Finn Thain - Another overhaul.
 *
 * Suggested reading:
 *   Inside Macintosh, ch. 5 ADB Manager
 *   Guide to the Macinstosh Family Hardware, ch. 8 Apple Desktop Bus
 *   Rockwell R6522 VIA datasheet
 *
 * Apple's "ADB Analyzer" bus sniffer is invaluable:
 *   ftp://ftp.apple.com/developer/Tool_Chest/Devices_-_Hardware/Apple_Desktop_Bus/
 */

#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/adb.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_via.h>

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
#define CTLR_IRQ	0x08		/* Controller rcv status (input) */
#define ST_MASK		0x30		/* mask for selecting ADB state bits */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */

/* ADB transaction states according to GMHW */
#define ST_CMD		0x00		/* ADB state: command byte */
#define ST_EVEN		0x10		/* ADB state: even data byte */
#define ST_ODD		0x20		/* ADB state: odd data byte */
#define ST_IDLE		0x30		/* ADB state: idle, nothing to send */

static int macii_init_via(void);
static void macii_start(void);
static irqreturn_t macii_interrupt(int irq, void *arg);
static void macii_queue_poll(void);

static int macii_probe(void);
static int macii_init(void);
static int macii_send_request(struct adb_request *req, int sync);
static int macii_write(struct adb_request *req);
static int macii_autopoll(int devs);
static void macii_poll(void);
static int macii_reset_bus(void);

struct adb_driver via_macii_driver = {
	.name         = "Mac II",
	.probe        = macii_probe,
	.init         = macii_init,
	.send_request = macii_send_request,
	.autopoll     = macii_autopoll,
	.poll         = macii_poll,
	.reset_bus    = macii_reset_bus,
};

static enum macii_state {
	idle,
	sending,
	reading,
	read_done,
} macii_state;

static struct adb_request *current_req; /* first request struct in the queue */
static struct adb_request *last_req;     /* last request struct in the queue */
static unsigned char reply_buf[16];        /* storage for autopolled replies */
static unsigned char *reply_ptr;     /* next byte in reply_buf or req->reply */
static int reading_reply;        /* store reply in reply_buf else req->reply */
static int data_index;      /* index of the next byte to send from req->data */
static int reply_len; /* number of bytes received in reply_buf or req->reply */
static int status;          /* VIA's ADB status bits captured upon interrupt */
static int last_status;              /* status bits as at previous interrupt */
static int srq_asserted;     /* have to poll for the device that asserted it */
static int command_byte;         /* the most recent command byte transmitted */
static int autopoll_devs;      /* bits set are device addresses to be polled */

/* Check for MacII style ADB */
static int macii_probe(void)
{
	if (macintosh_config->adb_type != MAC_ADB_II)
		return -ENODEV;

	via = via1;

	pr_info("adb: Mac II ADB Driver v1.0 for Unified ADB\n");
	return 0;
}

/* Initialize the driver */
int macii_init(void)
{
	unsigned long flags;
	int err;

	local_irq_save(flags);

	err = macii_init_via();
	if (err)
		goto out;

	err = request_irq(IRQ_MAC_ADB, macii_interrupt, 0, "ADB",
			  macii_interrupt);
	if (err)
		goto out;

	macii_state = idle;
out:
	local_irq_restore(flags);
	return err;
}

/* initialize the hardware */
static int macii_init_via(void)
{
	unsigned char x;

	/* We want CTLR_IRQ as input and ST_EVEN | ST_ODD as output lines. */
	via[DIRB] = (via[DIRB] | ST_EVEN | ST_ODD) & ~CTLR_IRQ;

	/* Set up state: idle */
	via[B] |= ST_IDLE;
	last_status = via[B] & (ST_MASK | CTLR_IRQ);

	/* Shift register on input */
	via[ACR] = (via[ACR] & ~SR_CTRL) | SR_EXT;

	/* Wipe any pending data and int */
	x = via[SR];

	return 0;
}

/* Send an ADB poll (Talk Register 0 command prepended to the request queue) */
static void macii_queue_poll(void)
{
	/* No point polling the active device as it will never assert SRQ, so
	 * poll the next device in the autopoll list. This could leave us
	 * stuck in a polling loop if an unprobed device is asserting SRQ.
	 * In theory, that could only happen if a device was plugged in after
	 * probing started. Unplugging it again will break the cycle.
	 * (Simply polling the next higher device often ends up polling almost
	 * every device (after wrapping around), which takes too long.)
	 */
	int device_mask;
	int next_device;
	static struct adb_request req;

	if (!autopoll_devs)
		return;

	device_mask = (1 << (((command_byte & 0xF0) >> 4) + 1)) - 1;
	if (autopoll_devs & ~device_mask)
		next_device = ffs(autopoll_devs & ~device_mask) - 1;
	else
		next_device = ffs(autopoll_devs) - 1;

	adb_request(&req, NULL, ADBREQ_NOSEND, 1, ADB_READREG(next_device, 0));

	req.sent = 0;
	req.complete = 0;
	req.reply_len = 0;
	req.next = current_req;

	if (current_req != NULL) {
		current_req = &req;
	} else {
		current_req = &req;
		last_req = &req;
	}
}

/* Send an ADB request; if sync, poll out the reply 'till it's done */
static int macii_send_request(struct adb_request *req, int sync)
{
	int err;

	err = macii_write(req);
	if (err)
		return err;

	if (sync)
		while (!req->complete)
			macii_poll();

	return 0;
}

/* Send an ADB request (append to request queue) */
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
		if (macii_state == idle)
			macii_start();
	}

	local_irq_restore(flags);

	return 0;
}

/* Start auto-polling */
static int macii_autopoll(int devs)
{
	static struct adb_request req;
	unsigned long flags;
	int err = 0;

	local_irq_save(flags);

	/* bit 1 == device 1, and so on. */
	autopoll_devs = devs & 0xFFFE;

	if (autopoll_devs && !current_req) {
		/* Send a Talk Reg 0. The controller will repeatedly transmit
		 * this as long as it is idle.
		 */
		adb_request(&req, NULL, ADBREQ_NOSEND, 1,
		            ADB_READREG(ffs(autopoll_devs) - 1, 0));
		err = macii_write(&req);
	}

	local_irq_restore(flags);
	return err;
}

static inline int need_autopoll(void)
{
	/* Was the last command Talk Reg 0
	 * and is the target on the autopoll list?
	 */
	if ((command_byte & 0x0F) == 0x0C &&
	    ((1 << ((command_byte & 0xF0) >> 4)) & autopoll_devs))
		return 0;
	return 1;
}

/* Prod the chip without interrupts */
static void macii_poll(void)
{
	macii_interrupt(0, NULL);
}

/* Reset the bus */
static int macii_reset_bus(void)
{
	static struct adb_request req;

	/* Command = 0, Address = ignored */
	adb_request(&req, NULL, ADBREQ_NOSEND, 1, ADB_BUSRESET);
	macii_send_request(&req, 1);

	/* Don't want any more requests during the Global Reset low time. */
	udelay(3000);

	return 0;
}

/* Start sending ADB packet */
static void macii_start(void)
{
	struct adb_request *req;

	req = current_req;

	/* Now send it. Be careful though, that first byte of the request
	 * is actually ADB_PACKET; the real data begins at index 1!
	 * And req->nbytes is the number of bytes of real data plus one.
	 */

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
}

/*
 * The notorious ADB interrupt handler - does all of the protocol handling.
 * Relies on the ADB controller sending and receiving data, thereby
 * generating shift register interrupts (SR_INT) for us. This means there has
 * to be activity on the ADB bus. The chip will poll to achieve this.
 *
 * The basic ADB state machine was left unchanged from the original MacII code
 * by Alan Cox, which was based on the CUDA driver for PowerMac.
 * The syntax of the ADB status lines is totally different on MacII,
 * though. MacII uses the states Command -> Even -> Odd -> Even ->...-> Idle
 * for sending and Idle -> Even -> Odd -> Even ->...-> Idle for receiving.
 * Start and end of a receive packet are signalled by asserting /IRQ on the
 * interrupt line (/IRQ means the CTLR_IRQ bit in port B; not to be confused
 * with the VIA shift register interrupt. /IRQ never actually interrupts the
 * processor, it's just an ordinary input.)
 */
static irqreturn_t macii_interrupt(int irq, void *arg)
{
	int x;
	struct adb_request *req;
	unsigned long flags;

	local_irq_save(flags);

	if (!arg) {
		/* Clear the SR IRQ flag when polling. */
		if (via[IFR] & SR_INT)
			via[IFR] = SR_INT;
		else {
			local_irq_restore(flags);
			return IRQ_NONE;
		}
	}

	last_status = status;
	status = via[B] & (ST_MASK | CTLR_IRQ);

	switch (macii_state) {
	case idle:
		if (reading_reply) {
			reply_ptr = current_req->reply;
		} else {
			WARN_ON(current_req);
			reply_ptr = reply_buf;
		}

		x = via[SR];

		if ((status & CTLR_IRQ) && (x == 0xFF)) {
			/* Bus timeout without SRQ sequence:
			 *     data is "FF" while CTLR_IRQ is "H"
			 */
			reply_len = 0;
			srq_asserted = 0;
			macii_state = read_done;
		} else {
			macii_state = reading;
			*reply_ptr = x;
			reply_len = 1;
		}

		/* set ADB state = even for first data byte */
		via[B] = (via[B] & ~ST_MASK) | ST_EVEN;
		break;

	case sending:
		req = current_req;
		if (data_index >= req->nbytes) {
			req->sent = 1;
			macii_state = idle;

			if (req->reply_expected) {
				reading_reply = 1;
			} else {
				req->complete = 1;
				current_req = req->next;
				if (req->done)
					(*req->done)(req);

				if (current_req)
					macii_start();
				else if (need_autopoll())
					macii_autopoll(autopoll_devs);
			}

			if (macii_state == idle) {
				/* reset to shift in */
				via[ACR] &= ~SR_OUT;
				x = via[SR];
				/* set ADB state idle - might get SRQ */
				via[B] = (via[B] & ~ST_MASK) | ST_IDLE;
			}
		} else {
			via[SR] = req->data[data_index++];

			if ((via[B] & ST_MASK) == ST_CMD) {
				/* just sent the command byte, set to EVEN */
				via[B] = (via[B] & ~ST_MASK) | ST_EVEN;
			} else {
				/* invert state bits, toggle ODD/EVEN */
				via[B] ^= ST_MASK;
			}
		}
		break;

	case reading:
		x = via[SR];
		WARN_ON((status & ST_MASK) == ST_CMD ||
			(status & ST_MASK) == ST_IDLE);

		/* Bus timeout with SRQ sequence:
		 *     data is "XX FF"      while CTLR_IRQ is "L L"
		 * End of packet without SRQ sequence:
		 *     data is "XX...YY 00" while CTLR_IRQ is "L...H L"
		 * End of packet SRQ sequence:
		 *     data is "XX...YY 00" while CTLR_IRQ is "L...L L"
		 * (where XX is the first response byte and
		 * YY is the last byte of valid response data.)
		 */

		srq_asserted = 0;
		if (!(status & CTLR_IRQ)) {
			if (x == 0xFF) {
				if (!(last_status & CTLR_IRQ)) {
					macii_state = read_done;
					reply_len = 0;
					srq_asserted = 1;
				}
			} else if (x == 0x00) {
				macii_state = read_done;
				if (!(last_status & CTLR_IRQ))
					srq_asserted = 1;
			}
		}

		if (macii_state == reading &&
		    reply_len < ARRAY_SIZE(reply_buf)) {
			reply_ptr++;
			*reply_ptr = x;
			reply_len++;
		}

		/* invert state bits, toggle ODD/EVEN */
		via[B] ^= ST_MASK;
		break;

	case read_done:
		x = via[SR];

		if (reading_reply) {
			reading_reply = 0;
			req = current_req;
			req->reply_len = reply_len;
			req->complete = 1;
			current_req = req->next;
			if (req->done)
				(*req->done)(req);
		} else if (reply_len && autopoll_devs)
			adb_input(reply_buf, reply_len, 0);

		macii_state = idle;

		/* SRQ seen before, initiate poll now */
		if (srq_asserted)
			macii_queue_poll();

		if (current_req)
			macii_start();
		else if (need_autopoll())
			macii_autopoll(autopoll_devs);

		if (macii_state == idle)
			via[B] = (via[B] & ~ST_MASK) | ST_IDLE;
		break;

	default:
		break;
	}

	local_irq_restore(flags);
	return IRQ_HANDLED;
}
