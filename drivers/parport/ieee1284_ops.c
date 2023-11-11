// SPDX-License-Identifier: GPL-2.0
/* IEEE-1284 operations for parport.
 *
 * This file is for generic IEEE 1284 operations.  The idea is that
 * they are used by the low-level drivers.  If they have a special way
 * of doing something, they can provide their own routines (and put
 * the function pointers in port->ops); if not, they can just use these
 * as a fallback.
 *
 * Note: Make no assumptions about hardware or architecture in this file!
 *
 * Author: Tim Waugh <tim@cyberelk.demon.co.uk>
 * Fixed AUTOFD polarity in ecp_forward_to_reverse().  Fred Barnes, 1999
 * Software emulated EPP fixes, Fred Barnes, 04/2001.
 */


#include <linux/module.h>
#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>

#undef DEBUG /* undef me for production */

#ifdef CONFIG_LP_CONSOLE
#undef DEBUG /* Don't want a garbled console */
#endif

/***                                *
 * One-way data transfer functions. *
 *                                ***/

/* Compatibility mode. */
size_t parport_ieee1284_write_compat (struct parport *port,
				      const void *buffer, size_t len,
				      int flags)
{
	int no_irq = 1;
	ssize_t count = 0;
	const unsigned char *addr = buffer;
	unsigned char byte;
	struct pardevice *dev = port->physport->cad;
	unsigned char ctl = (PARPORT_CONTROL_SELECT
			     | PARPORT_CONTROL_INIT);

	if (port->irq != PARPORT_IRQ_NONE) {
		parport_enable_irq (port);
		no_irq = 0;
	}

	port->physport->ieee1284.phase = IEEE1284_PH_FWD_DATA;
	parport_write_control (port, ctl);
	parport_data_forward (port);
	while (count < len) {
		unsigned long expire = jiffies + dev->timeout;
		long wait = msecs_to_jiffies(10);
		unsigned char mask = (PARPORT_STATUS_ERROR
				      | PARPORT_STATUS_BUSY);
		unsigned char val = (PARPORT_STATUS_ERROR
				     | PARPORT_STATUS_BUSY);

		/* Wait until the peripheral's ready */
		do {
			/* Is the peripheral ready yet? */
			if (!parport_wait_peripheral (port, mask, val))
				/* Skip the loop */
				goto ready;

			/* Is the peripheral upset? */
			if ((parport_read_status (port) &
			     (PARPORT_STATUS_PAPEROUT |
			      PARPORT_STATUS_SELECT |
			      PARPORT_STATUS_ERROR))
			    != (PARPORT_STATUS_SELECT |
				PARPORT_STATUS_ERROR))
				/* If nFault is asserted (i.e. no
				 * error) and PAPEROUT and SELECT are
				 * just red herrings, give the driver
				 * a chance to check it's happy with
				 * that before continuing. */
				goto stop;

			/* Have we run out of time? */
			if (!time_before (jiffies, expire))
				break;

			/* Yield the port for a while.  If this is the
                           first time around the loop, don't let go of
                           the port.  This way, we find out if we have
                           our interrupt handler called. */
			if (count && no_irq) {
				parport_release (dev);
				schedule_timeout_interruptible(wait);
				parport_claim_or_block (dev);
			}
			else
				/* We must have the device claimed here */
				parport_wait_event (port, wait);

			/* Is there a signal pending? */
			if (signal_pending (current))
				break;

			/* Wait longer next time. */
			wait *= 2;
		} while (time_before (jiffies, expire));

		if (signal_pending (current))
			break;

		pr_debug("%s: Timed out\n", port->name);
		break;

	ready:
		/* Write the character to the data lines. */
		byte = *addr++;
		parport_write_data (port, byte);
		udelay (1);

		/* Pulse strobe. */
		parport_write_control (port, ctl | PARPORT_CONTROL_STROBE);
		udelay (1); /* strobe */

		parport_write_control (port, ctl);
		udelay (1); /* hold */

		/* Assume the peripheral received it. */
		count++;

                /* Let another process run if it needs to. */
		if (time_before (jiffies, expire))
			if (!parport_yield_blocking (dev)
			    && need_resched())
				schedule ();
	}
 stop:
	port->physport->ieee1284.phase = IEEE1284_PH_FWD_IDLE;

	return count;
}

/* Nibble mode. */
size_t parport_ieee1284_read_nibble (struct parport *port, 
				     void *buffer, size_t len,
				     int flags)
{
#ifndef CONFIG_PARPORT_1284
	return 0;
#else
	unsigned char *buf = buffer;
	int i;
	unsigned char byte = 0;

	len *= 2; /* in nibbles */
	for (i=0; i < len; i++) {
		unsigned char nibble;

		/* Does the error line indicate end of data? */
		if (((i & 1) == 0) &&
		    (parport_read_status(port) & PARPORT_STATUS_ERROR)) {
			goto end_of_data;
		}

		/* Event 7: Set nAutoFd low. */
		parport_frob_control (port,
				      PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_AUTOFD);

		/* Event 9: nAck goes low. */
		port->ieee1284.phase = IEEE1284_PH_REV_DATA;
		if (parport_wait_peripheral (port,
					     PARPORT_STATUS_ACK, 0)) {
			/* Timeout -- no more data? */
			pr_debug("%s: Nibble timeout at event 9 (%d bytes)\n",
				 port->name, i / 2);
			parport_frob_control (port, PARPORT_CONTROL_AUTOFD, 0);
			break;
		}


		/* Read a nibble. */
		nibble = parport_read_status (port) >> 3;
		nibble &= ~8;
		if ((nibble & 0x10) == 0)
			nibble |= 8;
		nibble &= 0xf;

		/* Event 10: Set nAutoFd high. */
		parport_frob_control (port, PARPORT_CONTROL_AUTOFD, 0);

		/* Event 11: nAck goes high. */
		if (parport_wait_peripheral (port,
					     PARPORT_STATUS_ACK,
					     PARPORT_STATUS_ACK)) {
			/* Timeout -- no more data? */
			pr_debug("%s: Nibble timeout at event 11\n",
				 port->name);
			break;
		}

		if (i & 1) {
			/* Second nibble */
			byte |= nibble << 4;
			*buf++ = byte;
		} else 
			byte = nibble;
	}

	if (i == len) {
		/* Read the last nibble without checking data avail. */
		if (parport_read_status (port) & PARPORT_STATUS_ERROR) {
		end_of_data:
			pr_debug("%s: No more nibble data (%d bytes)\n",
				 port->name, i / 2);

			/* Go to reverse idle phase. */
			parport_frob_control (port,
					      PARPORT_CONTROL_AUTOFD,
					      PARPORT_CONTROL_AUTOFD);
			port->physport->ieee1284.phase = IEEE1284_PH_REV_IDLE;
		}
		else
			port->physport->ieee1284.phase = IEEE1284_PH_HBUSY_DAVAIL;
	}

	return i/2;
#endif /* IEEE1284 support */
}

/* Byte mode. */
size_t parport_ieee1284_read_byte (struct parport *port,
				   void *buffer, size_t len,
				   int flags)
{
#ifndef CONFIG_PARPORT_1284
	return 0;
#else
	unsigned char *buf = buffer;
	ssize_t count = 0;

	for (count = 0; count < len; count++) {
		unsigned char byte;

		/* Data available? */
		if (parport_read_status (port) & PARPORT_STATUS_ERROR) {
			goto end_of_data;
		}

		/* Event 14: Place data bus in high impedance state. */
		parport_data_reverse (port);

		/* Event 7: Set nAutoFd low. */
		parport_frob_control (port,
				      PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_AUTOFD);

		/* Event 9: nAck goes low. */
		port->physport->ieee1284.phase = IEEE1284_PH_REV_DATA;
		if (parport_wait_peripheral (port,
					     PARPORT_STATUS_ACK,
					     0)) {
			/* Timeout -- no more data? */
			parport_frob_control (port, PARPORT_CONTROL_AUTOFD,
						 0);
			pr_debug("%s: Byte timeout at event 9\n", port->name);
			break;
		}

		byte = parport_read_data (port);
		*buf++ = byte;

		/* Event 10: Set nAutoFd high */
		parport_frob_control (port, PARPORT_CONTROL_AUTOFD, 0);

		/* Event 11: nAck goes high. */
		if (parport_wait_peripheral (port,
					     PARPORT_STATUS_ACK,
					     PARPORT_STATUS_ACK)) {
			/* Timeout -- no more data? */
			pr_debug("%s: Byte timeout at event 11\n", port->name);
			break;
		}

		/* Event 16: Set nStrobe low. */
		parport_frob_control (port,
				      PARPORT_CONTROL_STROBE,
				      PARPORT_CONTROL_STROBE);
		udelay (5);

		/* Event 17: Set nStrobe high. */
		parport_frob_control (port, PARPORT_CONTROL_STROBE, 0);
	}

	if (count == len) {
		/* Read the last byte without checking data avail. */
		if (parport_read_status (port) & PARPORT_STATUS_ERROR) {
		end_of_data:
			pr_debug("%s: No more byte data (%zd bytes)\n",
				 port->name, count);

			/* Go to reverse idle phase. */
			parport_frob_control (port,
					      PARPORT_CONTROL_AUTOFD,
					      PARPORT_CONTROL_AUTOFD);
			port->physport->ieee1284.phase = IEEE1284_PH_REV_IDLE;
		}
		else
			port->physport->ieee1284.phase = IEEE1284_PH_HBUSY_DAVAIL;
	}

	return count;
#endif /* IEEE1284 support */
}

/***              *
 * ECP Functions. *
 *              ***/

#ifdef CONFIG_PARPORT_1284

static inline
int ecp_forward_to_reverse (struct parport *port)
{
	int retval;

	/* Event 38: Set nAutoFd low */
	parport_frob_control (port,
			      PARPORT_CONTROL_AUTOFD,
			      PARPORT_CONTROL_AUTOFD);
	parport_data_reverse (port);
	udelay (5);

	/* Event 39: Set nInit low to initiate bus reversal */
	parport_frob_control (port,
			      PARPORT_CONTROL_INIT,
			      0);

	/* Event 40: PError goes low */
	retval = parport_wait_peripheral (port,
					  PARPORT_STATUS_PAPEROUT, 0);

	if (!retval) {
		pr_debug("%s: ECP direction: reverse\n", port->name);
		port->ieee1284.phase = IEEE1284_PH_REV_IDLE;
	} else {
		pr_debug("%s: ECP direction: failed to reverse\n", port->name);
		port->ieee1284.phase = IEEE1284_PH_ECP_DIR_UNKNOWN;
	}

	return retval;
}

static inline
int ecp_reverse_to_forward (struct parport *port)
{
	int retval;

	/* Event 47: Set nInit high */
	parport_frob_control (port,
			      PARPORT_CONTROL_INIT
			      | PARPORT_CONTROL_AUTOFD,
			      PARPORT_CONTROL_INIT
			      | PARPORT_CONTROL_AUTOFD);

	/* Event 49: PError goes high */
	retval = parport_wait_peripheral (port,
					  PARPORT_STATUS_PAPEROUT,
					  PARPORT_STATUS_PAPEROUT);

	if (!retval) {
		parport_data_forward (port);
		pr_debug("%s: ECP direction: forward\n", port->name);
		port->ieee1284.phase = IEEE1284_PH_FWD_IDLE;
	} else {
		pr_debug("%s: ECP direction: failed to switch forward\n",
			 port->name);
		port->ieee1284.phase = IEEE1284_PH_ECP_DIR_UNKNOWN;
	}


	return retval;
}

#endif /* IEEE1284 support */

/* ECP mode, forward channel, data. */
size_t parport_ieee1284_ecp_write_data (struct parport *port,
					const void *buffer, size_t len,
					int flags)
{
#ifndef CONFIG_PARPORT_1284
	return 0;
#else
	const unsigned char *buf = buffer;
	size_t written;
	int retry;

	port = port->physport;

	if (port->ieee1284.phase != IEEE1284_PH_FWD_IDLE)
		if (ecp_reverse_to_forward (port))
			return 0;

	port->ieee1284.phase = IEEE1284_PH_FWD_DATA;

	/* HostAck high (data, not command) */
	parport_frob_control (port,
			      PARPORT_CONTROL_AUTOFD
			      | PARPORT_CONTROL_STROBE
			      | PARPORT_CONTROL_INIT,
			      PARPORT_CONTROL_INIT);
	for (written = 0; written < len; written++, buf++) {
		unsigned long expire = jiffies + port->cad->timeout;
		unsigned char byte;

		byte = *buf;
	try_again:
		parport_write_data (port, byte);
		parport_frob_control (port, PARPORT_CONTROL_STROBE,
				      PARPORT_CONTROL_STROBE);
		udelay (5);
		for (retry = 0; retry < 100; retry++) {
			if (!parport_wait_peripheral (port,
						      PARPORT_STATUS_BUSY, 0))
				goto success;

			if (signal_pending (current)) {
				parport_frob_control (port,
						      PARPORT_CONTROL_STROBE,
						      0);
				break;
			}
		}

		/* Time for Host Transfer Recovery (page 41 of IEEE1284) */
		pr_debug("%s: ECP transfer stalled!\n", port->name);

		parport_frob_control (port, PARPORT_CONTROL_INIT,
				      PARPORT_CONTROL_INIT);
		udelay (50);
		if (parport_read_status (port) & PARPORT_STATUS_PAPEROUT) {
			/* It's buggered. */
			parport_frob_control (port, PARPORT_CONTROL_INIT, 0);
			break;
		}

		parport_frob_control (port, PARPORT_CONTROL_INIT, 0);
		udelay (50);
		if (!(parport_read_status (port) & PARPORT_STATUS_PAPEROUT))
			break;

		pr_debug("%s: Host transfer recovered\n", port->name);

		if (time_after_eq (jiffies, expire)) break;
		goto try_again;
	success:
		parport_frob_control (port, PARPORT_CONTROL_STROBE, 0);
		udelay (5);
		if (parport_wait_peripheral (port,
					     PARPORT_STATUS_BUSY,
					     PARPORT_STATUS_BUSY))
			/* Peripheral hasn't accepted the data. */
			break;
	}

	port->ieee1284.phase = IEEE1284_PH_FWD_IDLE;

	return written;
#endif /* IEEE1284 support */
}

/* ECP mode, reverse channel, data. */
size_t parport_ieee1284_ecp_read_data (struct parport *port,
				       void *buffer, size_t len, int flags)
{
#ifndef CONFIG_PARPORT_1284
	return 0;
#else
	struct pardevice *dev = port->cad;
	unsigned char *buf = buffer;
	int rle_count = 0; /* shut gcc up */
	unsigned char ctl;
	int rle = 0;
	ssize_t count = 0;

	port = port->physport;

	if (port->ieee1284.phase != IEEE1284_PH_REV_IDLE)
		if (ecp_forward_to_reverse (port))
			return 0;

	port->ieee1284.phase = IEEE1284_PH_REV_DATA;

	/* Set HostAck low to start accepting data. */
	ctl = parport_read_control (port);
	ctl &= ~(PARPORT_CONTROL_STROBE | PARPORT_CONTROL_INIT |
		 PARPORT_CONTROL_AUTOFD);
	parport_write_control (port,
			       ctl | PARPORT_CONTROL_AUTOFD);
	while (count < len) {
		unsigned long expire = jiffies + dev->timeout;
		unsigned char byte;
		int command;

		/* Event 43: Peripheral sets nAck low. It can take as
                   long as it wants. */
		while (parport_wait_peripheral (port, PARPORT_STATUS_ACK, 0)) {
			/* The peripheral hasn't given us data in
			   35ms.  If we have data to give back to the
			   caller, do it now. */
			if (count)
				goto out;

			/* If we've used up all the time we were allowed,
			   give up altogether. */
			if (!time_before (jiffies, expire))
				goto out;

			/* Yield the port for a while. */
			if (dev->port->irq != PARPORT_IRQ_NONE) {
				parport_release (dev);
				schedule_timeout_interruptible(msecs_to_jiffies(40));
				parport_claim_or_block (dev);
			}
			else
				/* We must have the device claimed here. */
				parport_wait_event (port, msecs_to_jiffies(40));

			/* Is there a signal pending? */
			if (signal_pending (current))
				goto out;
		}

		/* Is this a command? */
		if (rle)
			/* The last byte was a run-length count, so
                           this can't be as well. */
			command = 0;
		else
			command = (parport_read_status (port) &
				   PARPORT_STATUS_BUSY) ? 1 : 0;

		/* Read the data. */
		byte = parport_read_data (port);

		/* If this is a channel command, rather than an RLE
                   command or a normal data byte, don't accept it. */
		if (command) {
			if (byte & 0x80) {
				pr_debug("%s: stopping short at channel command (%02x)\n",
					 port->name, byte);
				goto out;
			}
			else if (port->ieee1284.mode != IEEE1284_MODE_ECPRLE)
				pr_debug("%s: device illegally using RLE; accepting anyway\n",
					 port->name);

			rle_count = byte + 1;

			/* Are we allowed to read that many bytes? */
			if (rle_count > (len - count)) {
				pr_debug("%s: leaving %d RLE bytes for next time\n",
					 port->name, rle_count);
				break;
			}

			rle = 1;
		}

		/* Event 44: Set HostAck high, acknowledging handshake. */
		parport_write_control (port, ctl);

		/* Event 45: The peripheral has 35ms to set nAck high. */
		if (parport_wait_peripheral (port, PARPORT_STATUS_ACK,
					     PARPORT_STATUS_ACK)) {
			/* It's gone wrong.  Return what data we have
                           to the caller. */
			pr_debug("ECP read timed out at 45\n");

			if (command)
				pr_warn("%s: command ignored (%02x)\n",
					port->name, byte);

			break;
		}

		/* Event 46: Set HostAck low and accept the data. */
		parport_write_control (port,
				       ctl | PARPORT_CONTROL_AUTOFD);

		/* If we just read a run-length count, fetch the data. */
		if (command)
			continue;

		/* If this is the byte after a run-length count, decompress. */
		if (rle) {
			rle = 0;
			memset (buf, byte, rle_count);
			buf += rle_count;
			count += rle_count;
			pr_debug("%s: decompressed to %d bytes\n",
				 port->name, rle_count);
		} else {
			/* Normal data byte. */
			*buf = byte;
			buf++, count++;
		}
	}

 out:
	port->ieee1284.phase = IEEE1284_PH_REV_IDLE;
	return count;
#endif /* IEEE1284 support */
}

/* ECP mode, forward channel, commands. */
size_t parport_ieee1284_ecp_write_addr (struct parport *port,
					const void *buffer, size_t len,
					int flags)
{
#ifndef CONFIG_PARPORT_1284
	return 0;
#else
	const unsigned char *buf = buffer;
	size_t written;
	int retry;

	port = port->physport;

	if (port->ieee1284.phase != IEEE1284_PH_FWD_IDLE)
		if (ecp_reverse_to_forward (port))
			return 0;

	port->ieee1284.phase = IEEE1284_PH_FWD_DATA;

	/* HostAck low (command, not data) */
	parport_frob_control (port,
			      PARPORT_CONTROL_AUTOFD
			      | PARPORT_CONTROL_STROBE
			      | PARPORT_CONTROL_INIT,
			      PARPORT_CONTROL_AUTOFD
			      | PARPORT_CONTROL_INIT);
	for (written = 0; written < len; written++, buf++) {
		unsigned long expire = jiffies + port->cad->timeout;
		unsigned char byte;

		byte = *buf;
	try_again:
		parport_write_data (port, byte);
		parport_frob_control (port, PARPORT_CONTROL_STROBE,
				      PARPORT_CONTROL_STROBE);
		udelay (5);
		for (retry = 0; retry < 100; retry++) {
			if (!parport_wait_peripheral (port,
						      PARPORT_STATUS_BUSY, 0))
				goto success;

			if (signal_pending (current)) {
				parport_frob_control (port,
						      PARPORT_CONTROL_STROBE,
						      0);
				break;
			}
		}

		/* Time for Host Transfer Recovery (page 41 of IEEE1284) */
		pr_debug("%s: ECP transfer stalled!\n", port->name);

		parport_frob_control (port, PARPORT_CONTROL_INIT,
				      PARPORT_CONTROL_INIT);
		udelay (50);
		if (parport_read_status (port) & PARPORT_STATUS_PAPEROUT) {
			/* It's buggered. */
			parport_frob_control (port, PARPORT_CONTROL_INIT, 0);
			break;
		}

		parport_frob_control (port, PARPORT_CONTROL_INIT, 0);
		udelay (50);
		if (!(parport_read_status (port) & PARPORT_STATUS_PAPEROUT))
			break;

		pr_debug("%s: Host transfer recovered\n", port->name);

		if (time_after_eq (jiffies, expire)) break;
		goto try_again;
	success:
		parport_frob_control (port, PARPORT_CONTROL_STROBE, 0);
		udelay (5);
		if (parport_wait_peripheral (port,
					     PARPORT_STATUS_BUSY,
					     PARPORT_STATUS_BUSY))
			/* Peripheral hasn't accepted the data. */
			break;
	}

	port->ieee1284.phase = IEEE1284_PH_FWD_IDLE;

	return written;
#endif /* IEEE1284 support */
}

/***              *
 * EPP functions. *
 *              ***/

/* EPP mode, forward channel, data. */
size_t parport_ieee1284_epp_write_data (struct parport *port,
					const void *buffer, size_t len,
					int flags)
{
	unsigned char *bp = (unsigned char *) buffer;
	size_t ret = 0;

	/* set EPP idle state (just to make sure) with strobe low */
	parport_frob_control (port,
			      PARPORT_CONTROL_STROBE |
			      PARPORT_CONTROL_AUTOFD |
			      PARPORT_CONTROL_SELECT |
			      PARPORT_CONTROL_INIT,
			      PARPORT_CONTROL_STROBE |
			      PARPORT_CONTROL_INIT);
	port->ops->data_forward (port);
	for (; len > 0; len--, bp++) {
		/* Event 62: Write data and set autofd low */
		parport_write_data (port, *bp);
		parport_frob_control (port, PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_AUTOFD);

		/* Event 58: wait for busy (nWait) to go high */
		if (parport_poll_peripheral (port, PARPORT_STATUS_BUSY, 0, 10))
			break;

		/* Event 63: set nAutoFd (nDStrb) high */
		parport_frob_control (port, PARPORT_CONTROL_AUTOFD, 0);

		/* Event 60: wait for busy (nWait) to go low */
		if (parport_poll_peripheral (port, PARPORT_STATUS_BUSY,
					     PARPORT_STATUS_BUSY, 5))
			break;

		ret++;
	}

	/* Event 61: set strobe (nWrite) high */
	parport_frob_control (port, PARPORT_CONTROL_STROBE, 0);

	return ret;
}

/* EPP mode, reverse channel, data. */
size_t parport_ieee1284_epp_read_data (struct parport *port,
				       void *buffer, size_t len,
				       int flags)
{
	unsigned char *bp = (unsigned char *) buffer;
	unsigned ret = 0;

	/* set EPP idle state (just to make sure) with strobe high */
	parport_frob_control (port,
			      PARPORT_CONTROL_STROBE |
			      PARPORT_CONTROL_AUTOFD |
			      PARPORT_CONTROL_SELECT |
			      PARPORT_CONTROL_INIT,
			      PARPORT_CONTROL_INIT);
	port->ops->data_reverse (port);
	for (; len > 0; len--, bp++) {
		/* Event 67: set nAutoFd (nDStrb) low */
		parport_frob_control (port,
				      PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_AUTOFD);
		/* Event 58: wait for Busy to go high */
		if (parport_wait_peripheral (port, PARPORT_STATUS_BUSY, 0)) {
			break;
		}

		*bp = parport_read_data (port);

		/* Event 63: set nAutoFd (nDStrb) high */
		parport_frob_control (port, PARPORT_CONTROL_AUTOFD, 0);

		/* Event 60: wait for Busy to go low */
		if (parport_poll_peripheral (port, PARPORT_STATUS_BUSY,
					     PARPORT_STATUS_BUSY, 5)) {
			break;
		}

		ret++;
	}
	port->ops->data_forward (port);

	return ret;
}

/* EPP mode, forward channel, addresses. */
size_t parport_ieee1284_epp_write_addr (struct parport *port,
					const void *buffer, size_t len,
					int flags)
{
	unsigned char *bp = (unsigned char *) buffer;
	size_t ret = 0;

	/* set EPP idle state (just to make sure) with strobe low */
	parport_frob_control (port,
			      PARPORT_CONTROL_STROBE |
			      PARPORT_CONTROL_AUTOFD |
			      PARPORT_CONTROL_SELECT |
			      PARPORT_CONTROL_INIT,
			      PARPORT_CONTROL_STROBE |
			      PARPORT_CONTROL_INIT);
	port->ops->data_forward (port);
	for (; len > 0; len--, bp++) {
		/* Event 56: Write data and set nAStrb low. */
		parport_write_data (port, *bp);
		parport_frob_control (port, PARPORT_CONTROL_SELECT,
				      PARPORT_CONTROL_SELECT);

		/* Event 58: wait for busy (nWait) to go high */
		if (parport_poll_peripheral (port, PARPORT_STATUS_BUSY, 0, 10))
			break;

		/* Event 59: set nAStrb high */
		parport_frob_control (port, PARPORT_CONTROL_SELECT, 0);

		/* Event 60: wait for busy (nWait) to go low */
		if (parport_poll_peripheral (port, PARPORT_STATUS_BUSY,
					     PARPORT_STATUS_BUSY, 5))
			break;

		ret++;
	}

	/* Event 61: set strobe (nWrite) high */
	parport_frob_control (port, PARPORT_CONTROL_STROBE, 0);

	return ret;
}

/* EPP mode, reverse channel, addresses. */
size_t parport_ieee1284_epp_read_addr (struct parport *port,
				       void *buffer, size_t len,
				       int flags)
{
	unsigned char *bp = (unsigned char *) buffer;
	unsigned ret = 0;

	/* Set EPP idle state (just to make sure) with strobe high */
	parport_frob_control (port,
			      PARPORT_CONTROL_STROBE |
			      PARPORT_CONTROL_AUTOFD |
			      PARPORT_CONTROL_SELECT |
			      PARPORT_CONTROL_INIT,
			      PARPORT_CONTROL_INIT);
	port->ops->data_reverse (port);
	for (; len > 0; len--, bp++) {
		/* Event 64: set nSelectIn (nAStrb) low */
		parport_frob_control (port, PARPORT_CONTROL_SELECT,
				      PARPORT_CONTROL_SELECT);

		/* Event 58: wait for Busy to go high */
		if (parport_wait_peripheral (port, PARPORT_STATUS_BUSY, 0)) {
			break;
		}

		*bp = parport_read_data (port);

		/* Event 59: set nSelectIn (nAStrb) high */
		parport_frob_control (port, PARPORT_CONTROL_SELECT,
				      0);

		/* Event 60: wait for Busy to go low */
		if (parport_poll_peripheral (port, PARPORT_STATUS_BUSY, 
					     PARPORT_STATUS_BUSY, 5))
			break;

		ret++;
	}
	port->ops->data_forward (port);

	return ret;
}

EXPORT_SYMBOL(parport_ieee1284_ecp_write_data);
EXPORT_SYMBOL(parport_ieee1284_ecp_read_data);
EXPORT_SYMBOL(parport_ieee1284_ecp_write_addr);
EXPORT_SYMBOL(parport_ieee1284_write_compat);
EXPORT_SYMBOL(parport_ieee1284_read_nibble);
EXPORT_SYMBOL(parport_ieee1284_read_byte);
EXPORT_SYMBOL(parport_ieee1284_epp_write_data);
EXPORT_SYMBOL(parport_ieee1284_epp_read_data);
EXPORT_SYMBOL(parport_ieee1284_epp_write_addr);
EXPORT_SYMBOL(parport_ieee1284_epp_read_addr);
