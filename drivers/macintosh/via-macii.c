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

/* ADB command byte structure */
#define ADDR_MASK	0xF0
#define CMD_MASK	0x0F
#define OP_MASK		0x0C
#define TALK		0x0C

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
} macii_state;

static struct adb_request *current_req; /* first request struct in the queue */
static struct adb_request *last_req;     /* last request struct in the queue */
static unsigned char reply_buf[16];        /* storage for autopolled replies */
static unsigned char *reply_ptr;     /* next byte in reply_buf or req->reply */
static bool reading_reply;       /* store reply in reply_buf else req->reply */
static int data_index;      /* index of the next byte to send from req->data */
static int reply_len; /* number of bytes received in reply_buf or req->reply */
static int status;          /* VIA's ADB status bits captured upon interrupt */
static bool bus_timeout;                   /* no data was sent by the device */
static bool srq_asserted;    /* have to poll for the device that asserted it */
static u8 last_cmd;              /* the most recent command byte transmitted */
static u8 last_talk_cmd;    /* the most recent Talk command byte transmitted */
static u8 last_poll_cmd; /* the most recent Talk R0 command byte transmitted */
static unsigned int autopoll_devs;  /* bits set are device addresses to poll */

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
static int macii_init(void)
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

	/* Shift register on input */
	via[ACR] = (via[ACR] & ~SR_CTRL) | SR_EXT;

	/* Wipe any pending data and int */
	x = via[SR];

	return 0;
}

/* Send an ADB poll (Talk Register 0 command prepended to the request queue) */
static void macii_queue_poll(void)
{
	static struct adb_request req;
	unsigned char poll_command;
	unsigned int poll_addr;

	/* This only polls devices in the autopoll list, which assumes that
	 * unprobed devices never assert SRQ. That could happen if a device was
	 * plugged in after the adb bus scan. Unplugging it again will resolve
	 * the problem. This behaviour is similar to MacOS.
	 */
	if (!autopoll_devs)
		return;

	/* The device most recently polled may not be the best device to poll
	 * right now. Some other device(s) may have signalled SRQ (the active
	 * device won't do that). Or the autopoll list may have been changed.
	 * Try polling the next higher address.
	 */
	poll_addr = (last_poll_cmd & ADDR_MASK) >> 4;
	if ((srq_asserted && last_cmd == last_poll_cmd) ||
	    !(autopoll_devs & (1 << poll_addr))) {
		unsigned int higher_devs;

		higher_devs = autopoll_devs & -(1 << (poll_addr + 1));
		poll_addr = ffs(higher_devs ? higher_devs : autopoll_devs) - 1;
	}

	/* Send a Talk Register 0 command */
	poll_command = ADB_READREG(poll_addr, 0);

	/* No need to repeat this Talk command. The transceiver will do that
	 * as long as it is idle.
	 */
	if (poll_command == last_cmd)
		return;

	adb_request(&req, NULL, ADBREQ_NOSEND, 1, poll_command);

	req.sent = 0;
	req.complete = 0;
	req.reply_len = 0;
	req.next = current_req;

	if (WARN_ON(current_req)) {
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
	unsigned long flags;

	local_irq_save(flags);

	/* bit 1 == device 1, and so on. */
	autopoll_devs = (unsigned int)devs & 0xFFFE;

	if (!current_req) {
		macii_queue_poll();
		if (current_req && macii_state == idle)
			macii_start();
	}

	local_irq_restore(flags);

	return 0;
}

/* Prod the chip without interrupts */
static void macii_poll(void)
{
	macii_interrupt(0, NULL);
}

/* Reset the bus */
static int macii_reset_bus(void)
{
	struct adb_request req;

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

	/* Output mode */
	via[ACR] |= SR_OUT;
	/* Load data */
	via[SR] = req->data[1];
	/* set ADB state to 'command' */
	via[B] = (via[B] & ~ST_MASK) | ST_CMD;

	macii_state = sending;
	data_index = 2;

	bus_timeout = false;
	srq_asserted = false;
}

/*
 * The notorious ADB interrupt handler - does all of the protocol handling.
 * Relies on the ADB controller sending and receiving data, thereby
 * generating shift register interrupts (SR_INT) for us. This means there has
 * to be activity on the ADB bus. The chip will poll to achieve this.
 *
 * The VIA Port B output signalling works as follows. After the ADB transceiver
 * sees a transition on the PB4 and PB5 lines it will crank over the VIA shift
 * register which eventually raises the SR_INT interrupt. The PB4/PB5 outputs
 * are toggled with each byte as the ADB transaction progresses.
 *
 * Request with no reply expected (and empty transceiver buffer):
 *     CMD -> IDLE
 * Request with expected reply packet (or with buffered autopoll packet):
 *     CMD -> EVEN -> ODD -> EVEN -> ... -> IDLE
 * Unsolicited packet:
 *     IDLE -> EVEN -> ODD -> EVEN -> ... -> IDLE
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

	status = via[B] & (ST_MASK | CTLR_IRQ);

	switch (macii_state) {
	case idle:
		WARN_ON((status & ST_MASK) != ST_IDLE);

		reply_ptr = reply_buf;
		reading_reply = false;

		bus_timeout = false;
		srq_asserted = false;

		x = via[SR];

		if (!(status & CTLR_IRQ)) {
			/* /CTLR_IRQ asserted in idle state means we must
			 * read an autopoll reply from the transceiver buffer.
			 */
			macii_state = reading;
			*reply_ptr = x;
			reply_len = 1;
		} else {
			/* bus timeout */
			reply_len = 0;
			break;
		}

		/* set ADB state = even for first data byte */
		via[B] = (via[B] & ~ST_MASK) | ST_EVEN;
		break;

	case sending:
		req = current_req;

		if (status == (ST_CMD | CTLR_IRQ)) {
			/* /CTLR_IRQ de-asserted after the command byte means
			 * the host can continue with the transaction.
			 */

			/* Store command byte */
			last_cmd = req->data[1];
			if ((last_cmd & OP_MASK) == TALK) {
				last_talk_cmd = last_cmd;
				if ((last_cmd & CMD_MASK) == ADB_READREG(0, 0))
					last_poll_cmd = last_cmd;
			}
		}

		if (status == ST_CMD) {
			/* /CTLR_IRQ asserted after the command byte means we
			 * must read an autopoll reply. The first byte was
			 * lost because the shift register was an output.
			 */
			macii_state = reading;

			reading_reply = false;
			reply_ptr = reply_buf;
			*reply_ptr = last_talk_cmd;
			reply_len = 1;

			/* reset to shift in */
			via[ACR] &= ~SR_OUT;
			x = via[SR];
		} else if (data_index >= req->nbytes) {
			req->sent = 1;

			if (req->reply_expected) {
				macii_state = reading;

				reading_reply = true;
				reply_ptr = req->reply;
				*reply_ptr = req->data[1];
				reply_len = 1;

				via[ACR] &= ~SR_OUT;
				x = via[SR];
			} else if ((req->data[1] & OP_MASK) == TALK) {
				macii_state = reading;

				reading_reply = false;
				reply_ptr = reply_buf;
				*reply_ptr = req->data[1];
				reply_len = 1;

				via[ACR] &= ~SR_OUT;
				x = via[SR];

				req->complete = 1;
				current_req = req->next;
				if (req->done)
					(*req->done)(req);
			} else {
				macii_state = idle;

				req->complete = 1;
				current_req = req->next;
				if (req->done)
					(*req->done)(req);
				break;
			}
		} else {
			via[SR] = req->data[data_index++];
		}

		if ((via[B] & ST_MASK) == ST_CMD) {
			/* just sent the command byte, set to EVEN */
			via[B] = (via[B] & ~ST_MASK) | ST_EVEN;
		} else {
			/* invert state bits, toggle ODD/EVEN */
			via[B] ^= ST_MASK;
		}
		break;

	case reading:
		x = via[SR];
		WARN_ON((status & ST_MASK) == ST_CMD ||
			(status & ST_MASK) == ST_IDLE);

		if (!(status & CTLR_IRQ)) {
			if (status == ST_EVEN && reply_len == 1) {
				bus_timeout = true;
			} else if (status == ST_ODD && reply_len == 2) {
				srq_asserted = true;
			} else {
				macii_state = idle;

				if (bus_timeout)
					reply_len = 0;

				if (reading_reply) {
					struct adb_request *req = current_req;

					req->reply_len = reply_len;

					req->complete = 1;
					current_req = req->next;
					if (req->done)
						(*req->done)(req);
				} else if (reply_len && autopoll_devs &&
					   reply_buf[0] == last_poll_cmd) {
					adb_input(reply_buf, reply_len, 1);
				}
				break;
			}
		}

		if (reply_len < ARRAY_SIZE(reply_buf)) {
			reply_ptr++;
			*reply_ptr = x;
			reply_len++;
		}

		/* invert state bits, toggle ODD/EVEN */
		via[B] ^= ST_MASK;
		break;

	default:
		break;
	}

	if (macii_state == idle) {
		if (!current_req)
			macii_queue_poll();

		if (current_req)
			macii_start();

		if (macii_state == idle) {
			via[ACR] &= ~SR_OUT;
			x = via[SR];
			via[B] = (via[B] & ~ST_MASK) | ST_IDLE;
		}
	}

	local_irq_restore(flags);
	return IRQ_HANDLED;
}
