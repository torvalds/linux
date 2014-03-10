/*
 *
 * Copyright 1999 Digi International (www.digi.com)
 *     Gene Olson    <Gene_Olson at digi dot com>
 *     James Puzzo   <jamesp at digi dot com>
 *     Jeff Randall
 *     Scott Kilau   <scottk at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 */

/*
 *
 *  Filename:
 *
 *     dgrp_tty.c
 *
 *  Description:
 *
 *     This file implements the tty driver functionality for the
 *     RealPort driver software.
 *
 *  Author:
 *
 *     James A. Puzzo
 *     Ann-Marie Westgate
 *
 */

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include "dgrp_common.h"

#ifndef _POSIX_VDISABLE
#define   _POSIX_VDISABLE ('\0')
#endif

/*
 *	forward declarations
 */

static void drp_param(struct ch_struct *);
static void dgrp_tty_close(struct tty_struct *, struct file *);

/* ioctl helper functions */
static int set_modem_info(struct ch_struct *, unsigned int, unsigned int *);
static int get_modem_info(struct ch_struct *, unsigned int *);
static void dgrp_set_custom_speed(struct ch_struct *, int);
static int dgrp_tty_digigetedelay(struct tty_struct *, int *);
static int dgrp_tty_digisetedelay(struct tty_struct *, int *);
static int dgrp_send_break(struct ch_struct *, int);

static ushort  tty_to_ch_flags(struct tty_struct *, char);
static tcflag_t ch_to_tty_flags(unsigned short, char);

static void dgrp_tty_input_start(struct tty_struct *);
static void dgrp_tty_input_stop(struct tty_struct *);

static void drp_wmove(struct ch_struct *, int, void*, int);

static int dgrp_tty_open(struct tty_struct *, struct file *);
static void dgrp_tty_close(struct tty_struct *, struct file *);
static int dgrp_tty_write(struct tty_struct *, const unsigned char *, int);
static int dgrp_tty_write_room(struct tty_struct *);
static void dgrp_tty_flush_buffer(struct tty_struct *);
static int dgrp_tty_chars_in_buffer(struct tty_struct *);
static int dgrp_tty_ioctl(struct tty_struct *, unsigned int, unsigned long);
static void dgrp_tty_set_termios(struct tty_struct *, struct ktermios *);
static void dgrp_tty_stop(struct tty_struct *);
static void dgrp_tty_start(struct tty_struct *);
static void dgrp_tty_throttle(struct tty_struct *);
static void dgrp_tty_unthrottle(struct tty_struct *);
static void dgrp_tty_hangup(struct tty_struct *);
static int dgrp_tty_put_char(struct tty_struct *, unsigned char);
static int dgrp_tty_tiocmget(struct tty_struct *);
static int dgrp_tty_tiocmset(struct tty_struct *, unsigned int, unsigned int);
static int dgrp_tty_send_break(struct tty_struct *, int);
static void dgrp_tty_send_xchar(struct tty_struct *, char);

/*
 *	tty defines
 */
#define	SERIAL_TYPE_NORMAL	1
#define	SERIAL_TYPE_CALLOUT	2
#define	SERIAL_TYPE_XPRINT	3


/*
 *	tty globals/statics
 */


#define PORTSERVER_DIVIDEND	1843200

/*
 *  Default transparent print information.
 */
static struct digi_struct digi_init = {
	.digi_flags   = DIGI_COOK,	/* Flags			*/
	.digi_maxcps  = 100,		/* Max CPS			*/
	.digi_maxchar = 50,		/* Max chars in print queue	*/
	.digi_bufsize = 100,		/* Printer buffer size		*/
	.digi_onlen   = 4,		/* size of printer on string	*/
	.digi_offlen  = 4,		/* size of printer off string	*/
	.digi_onstr   = "\033[5i",	/* ANSI printer on string	*/
	.digi_offstr  = "\033[4i",	/* ANSI printer off string	*/
	.digi_term    = "ansi"		/* default terminal type	*/
};

/*
 *	Define a local default termios struct. All ports will be created
 *	with this termios initially.
 *
 *	This defines a raw port at 9600 baud, 8 data bits, no parity,
 *	1 stop bit.
 */
static struct ktermios DefaultTermios = {
	.c_iflag = (ICRNL | IXON),
	.c_oflag = (OPOST | ONLCR),
	.c_cflag = (B9600 | CS8 | CREAD | HUPCL | CLOCAL),
	.c_lflag = (ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL
		    | ECHOKE | IEXTEN),
	.c_cc    = INIT_C_CC,
	.c_line  = 0,
};

/* Define our tty operations struct */
static const struct tty_operations dgrp_tty_ops = {
	.open            = dgrp_tty_open,
	.close           = dgrp_tty_close,
	.write           = dgrp_tty_write,
	.write_room      = dgrp_tty_write_room,
	.flush_buffer    = dgrp_tty_flush_buffer,
	.chars_in_buffer = dgrp_tty_chars_in_buffer,
	.flush_chars     = NULL,
	.ioctl           = dgrp_tty_ioctl,
	.set_termios     = dgrp_tty_set_termios,
	.stop            = dgrp_tty_stop,
	.start           = dgrp_tty_start,
	.throttle        = dgrp_tty_throttle,
	.unthrottle      = dgrp_tty_unthrottle,
	.hangup          = dgrp_tty_hangup,
	.put_char        = dgrp_tty_put_char,
	.tiocmget        = dgrp_tty_tiocmget,
	.tiocmset        = dgrp_tty_tiocmset,
	.break_ctl       = dgrp_tty_send_break,
	.send_xchar      = dgrp_tty_send_xchar
};


static int calc_baud_rate(struct un_struct *un)
{
	int i;
	int brate;

	struct baud_rates {
		unsigned int rate;
		unsigned int cflag;
	};

	static struct baud_rates baud_rates[] = {
		{ 921600, B921600 },
		{ 460800, B460800 },
		{ 230400, B230400 },
		{ 115200, B115200 },
		{  57600, B57600  },
		{  38400, B38400  },
		{  19200, B19200  },
		{   9600, B9600   },
		{   4800, B4800   },
		{   2400, B2400   },
		{   1200, B1200   },
		{    600, B600    },
		{    300, B300    },
		{    200, B200    },
		{    150, B150    },
		{    134, B134    },
		{    110, B110    },
		{     75, B75     },
		{     50, B50     },
		{      0, B9600  }
	};

	brate = C_BAUD(un->un_tty);

	for (i = 0; baud_rates[i].rate; i++) {
		if (baud_rates[i].cflag == brate)
			break;
	}

	return baud_rates[i].rate;
}

static int calc_fastbaud_rate(struct un_struct *un, struct ktermios *uts)
{
	int i;
	int brate;

	ulong bauds[2][16] = {
		{ /* fastbaud*/
			0,      57600,	 76800,	115200,
			131657, 153600, 230400, 460800,
			921600, 1200,   1800,	2400,
			4800,   9600,	19200,	38400 },
		{ /* fastbaud & CBAUDEX */
			0,      57600,	115200,	230400,
			460800, 150,    200,    921600,
			600,    1200,   1800,	2400,
			4800,   9600,	19200,	38400 }
	};

	brate = C_BAUD(un->un_tty) & 0xff;

	i = (uts->c_cflag & CBAUDEX) ? 1 : 0;


	if ((i >= 0) && (i < 2) && (brate >= 0) && (brate < 16))
		brate = bauds[i][brate];
	else
		brate = 0;

	return brate;
}

/**
 * drp_param() -- send parameter values to be sent to the node
 * @ch: channel structure of port to modify
 *
 * Interprets the tty and modem changes made by an application
 * program (by examining the termios structures) and sets up
 * parameter values to be sent to the node.
 */
static void drp_param(struct ch_struct *ch)
{
	struct nd_struct *nd;
	struct un_struct *un;
	int   brate;
	int   mflow;
	int   xflag;
	int   iflag;
	struct ktermios *tts, *pts, *uts;

	nd = ch->ch_nd;

	/*
	 *  If the terminal device is open, use it to set up all tty
	 *  modes and functions.  Otherwise use the printer device.
	 */

	if (ch->ch_tun.un_open_count) {

		un = &ch->ch_tun;
		tts = &ch->ch_tun.un_tty->termios;

		/*
		 *  If both devices are open, copy critical line
		 *  parameters from the tty device to the printer,
		 *  so that if the tty is closed, the printer will
		 *  continue without disruption.
		 */

		if (ch->ch_pun.un_open_count) {

			pts = &ch->ch_pun.un_tty->termios;

			pts->c_cflag ^=
				(pts->c_cflag ^ tts->c_cflag) &
				(CBAUD  | CSIZE | CSTOPB | CREAD | PARENB |
				 PARODD | HUPCL | CLOCAL);

			pts->c_iflag ^=
				(pts->c_iflag ^ tts->c_iflag) &
				(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK |
				 ISTRIP | IXON   | IXANY  | IXOFF);

			pts->c_cc[VSTART] = tts->c_cc[VSTART];
			pts->c_cc[VSTOP] = tts->c_cc[VSTOP];
		}
	} else if (ch->ch_pun.un_open_count == 0) {
		pr_warn("%s - ch_pun.un_open_count shouldn't be 0\n",
		       __func__);
		return;
	} else {
		un = &ch->ch_pun;
	}

	uts = &un->un_tty->termios;

	/*
	 * Determine if FAST writes can be performed.
	 */

	if ((ch->ch_digi.digi_flags & DIGI_COOK) != 0 &&
	    (ch->ch_tun.un_open_count != 0)  &&
	    !((un->un_tty)->ldisc->ops->flags & LDISC_FLAG_DEFINED) &&
	    !(L_XCASE(un->un_tty))) {
		ch->ch_flag |= CH_FAST_WRITE;
	} else {
		ch->ch_flag &= ~CH_FAST_WRITE;
	}

	/*
	 *  If FAST writes can be performed, and OPOST is on in the
	 *  terminal device, do OPOST handling in the server.
	 */

	if ((ch->ch_flag & CH_FAST_WRITE) &&
	      O_OPOST(un->un_tty) != 0) {
		int oflag = tty_to_ch_flags(un->un_tty, 'o');

		/* add to ch_ocook any processing flags set in the termio */
		ch->ch_ocook |= oflag & (OF_OLCUC |
					 OF_ONLCR |
					 OF_OCRNL |
					 OF_ONLRET |
					 OF_TABDLY);

		/*
		 * the hpux driver clears any flags set in ch_ocook
		 * from the termios oflag.  It is STILL reported though
		 * by a TCGETA
		 */

		oflag = ch_to_tty_flags(ch->ch_ocook, 'o');
		uts->c_oflag &= ~oflag;

	} else {
		/* clear the ch->ch_ocook flag */
		int oflag = ch_to_tty_flags(ch->ch_ocook, 'o');
		uts->c_oflag |= oflag;
		ch->ch_ocook = 0;
	}

	ch->ch_oflag = ch->ch_ocook;


	ch->ch_flag &= ~CH_FAST_READ;

	/*
	 *  Generate channel flags
	 */

	if (C_BAUD(un->un_tty) == B0) {
		if (!(ch->ch_flag & CH_BAUD0)) {
			/* TODO : the HPUX driver flushes line */
			/* TODO : discipline, I assume I don't have to */

			ch->ch_tout = ch->ch_tin;
			ch->ch_rout = ch->ch_rin;

			ch->ch_break_time = 0;

			ch->ch_send |= RR_TX_FLUSH | RR_RX_FLUSH;

			ch->ch_mout &= ~(DM_DTR | DM_RTS);

			ch->ch_flag |= CH_BAUD0;
		}
	} else if (ch->ch_custom_speed) {
		ch->ch_brate = PORTSERVER_DIVIDEND / ch->ch_custom_speed;

		if (ch->ch_flag & CH_BAUD0) {
			ch->ch_mout |= DM_DTR | DM_RTS;

			ch->ch_flag &= ~CH_BAUD0;
		}
	} else {
		/*
		 * Baud rate mapping.
		 *
		 * If FASTBAUD isn't on, we can scan the new baud rate list
		 * as required.
		 *
		 * However, if FASTBAUD is on, we must go to the old
		 * baud rate mapping that existed many many moons ago,
		 * for compatibility reasons.
		 */

		if (!(ch->ch_digi.digi_flags & DIGI_FAST))
			brate = calc_baud_rate(un);
		else
			brate = calc_fastbaud_rate(un, uts);

		if (brate == 0)
			brate = 9600;

		ch->ch_brate = PORTSERVER_DIVIDEND / brate;

		if (ch->ch_flag & CH_BAUD0) {
			ch->ch_mout |= DM_DTR | DM_RTS;

			ch->ch_flag &= ~CH_BAUD0;
		}
	}

	/*
	 *  Generate channel cflags from the termio.
	 */

	ch->ch_cflag = tty_to_ch_flags(un->un_tty, 'c');

	/*
	 *  Generate channel iflags from the termio.
	 */

	iflag = (int) tty_to_ch_flags(un->un_tty, 'i');

	if (START_CHAR(un->un_tty) == _POSIX_VDISABLE ||
	    STOP_CHAR(un->un_tty) == _POSIX_VDISABLE) {
		iflag &= ~(IF_IXON | IF_IXANY | IF_IXOFF);
	}

	ch->ch_iflag = iflag;

	/*
	 *  Generate flow control characters
	 */

	/*
	 * From the POSIX.1 spec (7.1.2.6): "If {_POSIX_VDISABLE}
	 * is defined for the terminal device file, and the value
	 * of one of the changeable special control characters (see
	 * 7.1.1.9) is {_POSIX_VDISABLE}, that function shall be
	 * disabled, that is, no input data shall be recognized as
	 * the disabled special character."
	 *
	 * OK, so we don't ever assign S/DXB XON or XOFF to _POSIX_VDISABLE.
	 */

	if (uts->c_cc[VSTART] != _POSIX_VDISABLE)
		ch->ch_xon = uts->c_cc[VSTART];
	if (uts->c_cc[VSTOP] != _POSIX_VDISABLE)
		ch->ch_xoff = uts->c_cc[VSTOP];

	ch->ch_lnext = (uts->c_cc[VLNEXT] == _POSIX_VDISABLE ? 0 :
			uts->c_cc[VLNEXT]);

	/*
	 * Also, if either c_cc[START] or c_cc[STOP] is set to
	 * _POSIX_VDISABLE, we can't really do software flow
	 * control--in either direction--so we turn it off as
	 * far as S/DXB is concerned.  In essence, if you disable
	 * one, you disable the other too.
	 */
	if ((uts->c_cc[VSTART] == _POSIX_VDISABLE) ||
	    (uts->c_cc[VSTOP] == _POSIX_VDISABLE))
		ch->ch_iflag &= ~(IF_IXOFF | IF_IXON);

	/*
	 *  Update xflags.
	 */

	xflag = 0;

	if (ch->ch_digi.digi_flags & DIGI_AIXON)
		xflag = XF_XIXON;

	if ((ch->ch_xxon == _POSIX_VDISABLE) ||
	    (ch->ch_xxoff == _POSIX_VDISABLE))
		xflag &= ~XF_XIXON;

	ch->ch_xflag = xflag;


	/*
	 *  Figure effective DCD value.
	 */

	if (C_CLOCAL(un->un_tty))
		ch->ch_flag |= CH_CLOCAL;
	else
		ch->ch_flag &= ~CH_CLOCAL;

	/*
	 *  Check modem signals
	 */

	dgrp_carrier(ch);

	/*
	 *  Get hardware handshake value.
	 */

	mflow = 0;

	if (C_CRTSCTS(un->un_tty))
		mflow |= (DM_RTS | DM_CTS);

	if (ch->ch_digi.digi_flags & RTSPACE)
		mflow |= DM_RTS;

	if (ch->ch_digi.digi_flags & DTRPACE)
		mflow |= DM_DTR;

	if (ch->ch_digi.digi_flags & CTSPACE)
		mflow |= DM_CTS;

	if (ch->ch_digi.digi_flags & DSRPACE)
		mflow |= DM_DSR;

	if (ch->ch_digi.digi_flags & DCDPACE)
		mflow |= DM_CD;

	if (ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE)
		mflow |= DM_RTS_TOGGLE;

	ch->ch_mflow = mflow;

	/*
	 *  Send the changes to the server.
	 */

	ch->ch_flag |= CH_PARAM;
	(ch->ch_nd)->nd_tx_work = 1;

	if (waitqueue_active(&ch->ch_flag_wait))
		wake_up_interruptible(&ch->ch_flag_wait);
}

/*
 * This function is just used as a callback for timeouts
 * waiting on the ch_sleep flag.
 */
static void wake_up_drp_sleep_timer(unsigned long ptr)
{
	struct ch_struct *ch = (struct ch_struct *) ptr;
	if (ch)
		wake_up(&ch->ch_sleep);
}


/*
 * Set up our own sleep that can't be cancelled
 * until our timeout occurs.
 */
static void drp_my_sleep(struct ch_struct *ch)
{
	struct timer_list drp_wakeup_timer;
	DECLARE_WAITQUEUE(wait, current);

	/*
	 * First make sure we're ready to receive the wakeup.
	 */

	add_wait_queue(&ch->ch_sleep, &wait);
	current->state = TASK_UNINTERRUPTIBLE;

	/*
	 * Since we are uninterruptible, set a timer to
	 * unset the uninterruptable state in 1 second.
	 */

	init_timer(&drp_wakeup_timer);
	drp_wakeup_timer.function = wake_up_drp_sleep_timer;
	drp_wakeup_timer.data = (unsigned long) ch;
	drp_wakeup_timer.expires = jiffies + (1 * HZ);
	add_timer(&drp_wakeup_timer);

	schedule();

	del_timer(&drp_wakeup_timer);

	remove_wait_queue(&ch->ch_sleep, &wait);
}

/*
 * dgrp_tty_open()
 *
 * returns:
 *    -EBUSY    - this is a callout device and the normal device is active
 *              - there is an error in opening the tty
 *    -ENODEV   - the channel does not exist
 *    -EAGAIN   - we are in the middle of hanging up or closing
 *              - IMMEDIATE_OPEN fails
 *    -ENXIO or -EAGAIN
 *              - if the port is outside physical range
 *    -EINTR    - the open is interrupted
 *
 */
static int dgrp_tty_open(struct tty_struct *tty, struct file *file)
{
	int    retval = 0;
	struct nd_struct  *nd;
	struct ch_struct *ch;
	struct un_struct  *un;
	int    port;
	int    delay_error;
	int    otype;
	int    unf;
	int    wait_carrier;
	int    category;
	int    counts_were_incremented = 0;
	ulong lock_flags;
	DECLARE_WAITQUEUE(wait, current);

	/*
	 * Do some initial checks to see if the node and port exist
	 */

	nd = nd_struct_get(MAJOR(tty_devnum(tty)));
	port = PORT_NUM(MINOR(tty_devnum(tty)));
	category = OPEN_CATEGORY(MINOR(tty_devnum(tty)));

	if (!nd)
		return -ENODEV;

	if (port >= CHAN_MAX)
		return -ENODEV;

	/*
	 *  The channel exists.
	 */

	ch = nd->nd_chan + port;

	un = IS_PRINT(MINOR(tty_devnum(tty))) ? &ch->ch_pun : &ch->ch_tun;
	un->un_tty = tty;
	tty->driver_data = un;

	/*
	 * If we are in the middle of hanging up,
	 * then return an error
	 */
	if (tty_hung_up_p(file)) {
		retval = ((un->un_flag & UN_HUP_NOTIFY) ?
			   -EAGAIN : -ERESTARTSYS);
		goto done;
	}

	/*
	 * If the port is in the middle of closing, then block
	 * until it is done, then try again.
	 */
	retval = wait_event_interruptible(un->un_close_wait,
			((un->un_flag & UN_CLOSING) == 0));

	if (retval)
		goto done;

	/*
	 * If the port is in the middle of a reopen after a network disconnect,
	 * wait until it is done, then try again.
	 */
	retval = wait_event_interruptible(ch->ch_flag_wait,
			((ch->ch_flag & CH_PORT_GONE) == 0));

	if (retval)
		goto done;

	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */

	if (tty->driver->subtype == SERIAL_TYPE_CALLOUT) {
		if (un->un_flag & UN_NORMAL_ACTIVE) {
			retval = -EBUSY;
			goto done;
		} else {
			un->un_flag |= UN_CALLOUT_ACTIVE;
		}
	}

	/*
	 *  Loop waiting until the open can be successfully completed.
	 */

	spin_lock_irqsave(&nd->nd_lock, lock_flags);

	nd->nd_tx_work = 1;

	for (;;) {
		wait_carrier = 0;

		/*
		 * Determine the open type from the flags provided.
		 */

		/*
		 * If the port is not enabled, then exit
		 */
		if (test_bit(TTY_IO_ERROR, &tty->flags)) {
			/* there was an error in opening the tty */
			if (un->un_flag & UN_CALLOUT_ACTIVE)
				retval = -EBUSY;
			else
				un->un_flag |= UN_NORMAL_ACTIVE;
			goto unlock;
		}

		if (file->f_flags & O_NONBLOCK) {

			/*
			 * if the O_NONBLOCK is set, errors on read and write
			 * must return -EAGAIN immediately and NOT sleep
			 * on the waitqs.
			 */
			otype = OTYPE_IMMEDIATE;
			delay_error = -EAGAIN;

		} else if (!OPEN_WAIT_AVAIL(category) ||
			  (file->f_flags & O_NDELAY) != 0) {
			otype = OTYPE_IMMEDIATE;
			delay_error = -EBUSY;

		} else if (!OPEN_WAIT_CARRIER(category) ||
			  ((ch->ch_digi.digi_flags & DIGI_FORCEDCD) != 0) ||
			  C_CLOCAL(tty)) {
			otype = OTYPE_PERSISTENT;
			delay_error = 0;

		} else {
			otype = OTYPE_INCOMING;
			delay_error = 0;
		}

		/*
		 * Handle port currently outside physical port range.
		 */

		if (port >= nd->nd_chan_count) {
			if (otype == OTYPE_IMMEDIATE) {
				retval = (nd->nd_state == NS_READY) ?
						-ENXIO : -EAGAIN;
				goto unlock;
			}
		}

		/*
		 *  Handle port not currently open.
		 */

		else if (ch->ch_open_count == 0) {
			/*
			 * Return an error when an Incoming Open
			 * response indicates the port is busy.
			 */

			if (ch->ch_open_error != 0 && otype == ch->ch_otype) {
				retval = (ch->ch_open_error <= 2) ?
					  delay_error : -ENXIO;
				goto unlock;
			}

			/*
			 * Fail any new Immediate open if we do not have
			 * a normal connection to the server.
			 */

			if (nd->nd_state != NS_READY &&
			    otype == OTYPE_IMMEDIATE) {
				retval = -EAGAIN;
				goto unlock;
			}

			/*
			 * If a Realport open of the correct type has
			 * succeeded, complete the open.
			 */

			if (ch->ch_state == CS_READY && ch->ch_otype == otype)
				break;
		}

		/*
		 * Handle port already open and active as a device
		 * of same category.
		 */

		else if ((ch->ch_category == category) ||
			  IS_PRINT(MINOR(tty_devnum(tty)))) {
			/*
			 * Fail if opening the device now would
			 * violate exclusive use.
			 */
			unf = ch->ch_tun.un_flag | ch->ch_pun.un_flag;

			if ((file->f_flags & O_EXCL) || (unf & UN_EXCL)) {
				retval = -EBUSY;
				goto unlock;
			}

			/*
			 * If the open device is in the hangup state, all
			 * system calls fail except close().
			 */

			/* TODO : check on hangup_p calls */

			if (ch->ch_flag & CH_HANGUP) {
				retval = -ENXIO;
				goto unlock;
			}

			/*
			 * If the port is ready, and carrier is ignored
			 * or present, then complete the open.
			 */

			if (ch->ch_state == CS_READY &&
			    (otype != OTYPE_INCOMING ||
			    ch->ch_flag & CH_VIRT_CD))
				break;

			wait_carrier = 1;
		}

		/*
		 *  Handle port active with a different category device.
		 */

		else {
			if (otype == OTYPE_IMMEDIATE) {
				retval = delay_error;
				goto unlock;
			}
		}

		/*
		 * Wait until conditions change, then take another
		 * try at the open.
		 */

		ch->ch_wait_count[otype]++;

		if (wait_carrier)
			ch->ch_wait_carrier++;

		/*
		 * Prepare the task to accept the wakeup, then
		 * release our locks and release control.
		 */

		add_wait_queue(&ch->ch_flag_wait, &wait);
		current->state = TASK_INTERRUPTIBLE;

		spin_unlock_irqrestore(&nd->nd_lock, lock_flags);

		/*
		 * Give up control, we'll come back if we're
		 * interrupted or are woken up.
		 */
		schedule();
		remove_wait_queue(&ch->ch_flag_wait, &wait);

		spin_lock_irqsave(&nd->nd_lock, lock_flags);

		current->state = TASK_RUNNING;

		ch->ch_wait_count[otype]--;

		if (wait_carrier)
			ch->ch_wait_carrier--;

		nd->nd_tx_work = 1;

		if (signal_pending(current)) {
			retval = -EINTR;
			goto unlock;
		}
	} /* end for(;;) */

	/*
	 *  The open has succeeded.  No turning back.
	 */
	counts_were_incremented = 1;
	un->un_open_count++;
	ch->ch_open_count++;

	/*
	 * Initialize the channel, if it's not already open.
	 */

	if (ch->ch_open_count == 1) {
		ch->ch_flag = 0;
		ch->ch_inwait = 0;
		ch->ch_category = category;
		ch->ch_pscan_state = 0;

		/* TODO : find out what PS-1 bug Gene was referring to */
		/* TODO : in the following comment. */

		ch->ch_send = RR_TX_START | RR_RX_START;  /* PS-1 bug */

		if (C_CLOCAL(tty) ||
		    ch->ch_s_mlast & DM_CD ||
		    ch->ch_digi.digi_flags & DIGI_FORCEDCD)
			ch->ch_flag |= CH_VIRT_CD;
		else if (OPEN_FORCES_CARRIER(category))
			ch->ch_flag |= CH_VIRT_CD;

	}

	/*
	 *  Initialize the unit, if it is not already open.
	 */

	if (un->un_open_count == 1) {
		/*
		 *  Since all terminal options are always sticky in Linux,
		 *  we don't need the UN_STICKY flag to be handled specially.
		 */
		/* clears all the digi flags, leaves serial flags */
		un->un_flag &= ~UN_DIGI_MASK;

		if (file->f_flags & O_EXCL)
			un->un_flag |= UN_EXCL;

		/* TODO : include "session" and "pgrp" */

		/*
		 *  In Linux, all terminal parameters are intended to be sticky.
		 *  as a result, we "remove" the code which once reset the ports
		 *  to sane values.
		 */

		drp_param(ch);

	}

	un->un_flag |= UN_INITIALIZED;

	retval = 0;

unlock:

	spin_unlock_irqrestore(&nd->nd_lock, lock_flags);

done:
	/*
	 * Linux does a close for every open, even failed ones!
	 */
	if (!counts_were_incremented) {
		un->un_open_count++;
		ch->ch_open_count++;
	}

	if (retval)
		dev_err(tty->dev, "tty open bad return (%i)\n", retval);

	return retval;
}




/*
 * dgrp_tty_close() -- close function for tty_operations
 */
static void dgrp_tty_close(struct tty_struct *tty, struct file *file)
{
	struct ch_struct *ch;
	struct un_struct *un;
	struct nd_struct *nd;
	int	tpos;
	int	port;
	int	err = 0;
	int	s = 0;
	ulong  waketime;
	ulong  lock_flags;
	int sent_printer_offstr = 0;

	port = PORT_NUM(MINOR(tty_devnum(tty)));

	un = tty->driver_data;

	if (!un)
		return;

	ch = un->un_ch;

	if (!ch)
		return;

	nd = ch->ch_nd;

	if (!nd)
		return;

	spin_lock_irqsave(&nd->nd_lock, lock_flags);


	/* Used to be on channel basis, now we check on a unit basis. */
	if (un->un_open_count != 1)
		goto unlock;

	/*
	 * OK, its the last close on the unit
	 */
	un->un_flag |= UN_CLOSING;

	/*
	 * Notify the discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;

	/*
	 * Wait for output to drain only if this is
	 * the last close against the channel
	 */

	if (ch->ch_open_count == 1) {
		/*
		 * If its the print device, we need to ensure at all costs that
		 * the offstr will fit. If it won't, flush our tbuf.
		 */
		if (IS_PRINT(MINOR(tty_devnum(tty))) &&
		    (((ch->ch_tout - ch->ch_tin - 1) & TBUF_MASK) <
		    ch->ch_digi.digi_offlen))
			ch->ch_tin = ch->ch_tout;

		/*
		 * Turn off the printer.  Don't bother checking to see if its
		 * IS_PRINT... Since this is the last close the flag is going
		 * to be cleared, so we MUST make sure the offstr gets inserted
		 * into tbuf.
		 */

		if ((ch->ch_flag & CH_PRON) != 0) {
			drp_wmove(ch, 0, ch->ch_digi.digi_offstr,
				  ch->ch_digi.digi_offlen);
			ch->ch_flag &= ~CH_PRON;
			sent_printer_offstr = 1;
		}
	}

	/*
	 *  Wait until either the output queue has drained, or we see
	 *  absolutely no progress for 15 seconds.
	 */

	tpos = ch->ch_s_tpos;

	waketime = jiffies + 15 * HZ;

	for (;;) {

		/*
		 *  Make sure the port still exists.
		 */

		if (port >= nd->nd_chan_count) {
			err = 1;
			break;
		}

		if (signal_pending(current)) {
			err = 1;
			break;
		}

		/*
		 * If the port is idle (not opened on the server), we have
		 * no way of draining/flushing/closing the port on that server.
		 * So break out of loop.
		 */
		if (ch->ch_state == CS_IDLE)
			break;

		nd->nd_tx_work = 1;

		/*
		 *  Exit if the queues for this unit are empty,
		 *  and either the other unit is still open or all
		 *  data has drained.
		 */

		if ((un->un_tty)->ops->chars_in_buffer ?
		    ((un->un_tty)->ops->chars_in_buffer)(un->un_tty) == 0 : 1) {

			/*
			 * We don't need to wait for a buffer to drain
			 * if the other unit is open.
			 */

			if (ch->ch_open_count != un->un_open_count)
				break;

			/*
			 *  The wait is complete when all queues are
			 *  drained, and any break in progress is complete.
			 */

			if (ch->ch_tin == ch->ch_tout &&
			    ch->ch_s_tin == ch->ch_s_tpos &&
			    (ch->ch_send & RR_TX_BREAK) == 0) {
				break;
			}
		}

		/*
		 * Flush TX data and exit the wait if NDELAY is set,
		 * or this is not a DIGI printer, and the close timeout
		 * expires.
		 */

		if ((file->f_flags & (O_NDELAY | O_NONBLOCK)) ||
		    ((long)(jiffies - waketime) >= 0 &&
		      (ch->ch_digi.digi_flags & DIGI_PRINTER) == 0)) {

				/*
				 * If we sent the printer off string, we cannot
				 * flush our internal buffers, or we might lose
				 * the offstr.
				 */
				if (!sent_printer_offstr)
					dgrp_tty_flush_buffer(tty);

				spin_unlock_irqrestore(&nd->nd_lock, lock_flags);
				tty_ldisc_flush(tty);
				spin_lock_irqsave(&nd->nd_lock, lock_flags);
				break;
		}

		/*
		 *  Otherwise take a short nap.
		 */

		ch->ch_flag |= CH_DRAIN;

		spin_unlock_irqrestore(&nd->nd_lock, lock_flags);

		schedule_timeout_interruptible(1);
		s = signal_pending(current);

		spin_lock_irqsave(&nd->nd_lock, lock_flags);

		if (s) {
			/*
			 * If we had sent the printer off string, we now have
			 * some problems.
			 *
			 * The system won't let us sleep since we got an error
			 * back from sleep, presumably because the user did
			 * a ctrl-c...
			 * But we need to ensure that the offstr gets sent!
			 * Thus, we have to do something else besides sleeping.
			 * The plan:
			 * 1) Make this task uninterruptable.
			 * 2) Set up a timer to go off in 1 sec.
			 * 3) Act as tho we just got out of the sleep above.
			 *
			 * Thankfully, in the real world, this just
			 * never happens.
			 */

			if (sent_printer_offstr) {
				spin_unlock_irqrestore(&nd->nd_lock,
						       lock_flags);
				drp_my_sleep(ch);
				spin_lock_irqsave(&nd->nd_lock, lock_flags);
			} else {
				err = 1;
				break;
			}
		}

		/*
		 *  Restart the wait if any progress is seen.
		 */

		if (ch->ch_s_tpos != tpos) {
			tpos = ch->ch_s_tpos;

			/* TODO:  this gives us timeout problems with nist ?? */
			waketime = jiffies + 15 * HZ;
		}
	}

	/*
	 *  Close the line discipline
	 */

	/* this is done in tty_io.c */
	/* if ((un->un_tty)->ldisc.close)
	 *	((un->un_tty)->ldisc.close)(un->un_tty);
	 */

	/* don't do this here */
	/* un->un_flag = 0; */

	/*
	 *  Flush the receive buffer on terminal unit close only.
	 */

	if (!IS_PRINT(MINOR(tty_devnum(tty))))
		ch->ch_rout = ch->ch_rin;


	/*
	 * Don't permit the close to happen until we get any pending
	 * sync request responses.
	 * There could be other ports depending upon the response as well.
	 *
	 * Also, don't permit the close to happen until any parameter
	 * changes have been sent out from the state machine as well.
	 * This is required because of a ditty -a race with -HUPCL
	 * We MUST make sure all channel parameters have been sent to the
	 * Portserver before sending a close.
	 */

	if ((err != 1) && (ch->ch_state != CS_IDLE)) {
		spin_unlock_irqrestore(&nd->nd_lock, lock_flags);
		s = wait_event_interruptible(ch->ch_flag_wait,
			((ch->ch_flag & (CH_WAITING_SYNC | CH_PARAM)) == 0));
		spin_lock_irqsave(&nd->nd_lock, lock_flags);
	}

	/*
	 * Cleanup the channel if last unit open.
	 */

	if (ch->ch_open_count == 1) {
		ch->ch_flag = 0;
		ch->ch_category = 0;
		ch->ch_send = 0;
		ch->ch_expect = 0;
		ch->ch_tout = ch->ch_tin;
		/* (un->un_tty)->device = 0; */

		if (ch->ch_state == CS_READY)
			ch->ch_state = CS_SEND_CLOSE;
	}

	/*
	 * Send the changes to the server
	 */
	if (ch->ch_state != CS_IDLE) {
		ch->ch_flag |= CH_PARAM;
		wake_up_interruptible(&ch->ch_flag_wait);
	}

	nd->nd_tx_work = 1;
	nd->nd_tx_ready = 1;

unlock:
	tty->closing = 0;

	if (ch->ch_open_count <= 0)
		dev_info(tty->dev,
			 "%s - unexpected value for ch->ch_open_count: %i\n",
			 __func__, ch->ch_open_count);
	else
		ch->ch_open_count--;

	if (un->un_open_count <= 0)
		dev_info(tty->dev,
			 "%s - unexpected value for un->un_open_count: %i\n",
			 __func__, un->un_open_count);
	else
		un->un_open_count--;

	un->un_flag &= ~(UN_NORMAL_ACTIVE | UN_CALLOUT_ACTIVE | UN_CLOSING);
	if (waitqueue_active(&un->un_close_wait))
		wake_up_interruptible(&un->un_close_wait);

	spin_unlock_irqrestore(&nd->nd_lock, lock_flags);

	return;

}

static void drp_wmove(struct ch_struct *ch, int from_user, void *buf, int count)
{
	int n;
	int ret = 0;

	ch->ch_nd->nd_tx_work = 1;

	n = TBUF_MAX - ch->ch_tin;

	if (count >= n) {
		if (from_user)
			ret = copy_from_user(ch->ch_tbuf + ch->ch_tin,
					     (void __user *) buf, n);
		else
			memcpy(ch->ch_tbuf + ch->ch_tin, buf, n);

		buf = (char *) buf + n;
		count -= n;
		ch->ch_tin = 0;
	}

	if (from_user)
		ret = copy_from_user(ch->ch_tbuf + ch->ch_tin,
				     (void __user *) buf, count);
	else
		memcpy(ch->ch_tbuf + ch->ch_tin, buf, count);

	ch->ch_tin += count;
}


static int dgrp_calculate_txprint_bounds(struct ch_struct *ch, int space,
					 int *un_flag)
{
	clock_t tt;
	clock_t mt;
	unsigned short tmax = 0;

	/*
	 * If the terminal device is busy, reschedule when
	 * the terminal device becomes idle.
	 */

	if (ch->ch_tun.un_open_count != 0 &&
	    ch->ch_tun.un_tty->ops->chars_in_buffer &&
	    ((ch->ch_tun.un_tty->ops->chars_in_buffer)(ch->ch_tun.un_tty) != 0)) {
		*un_flag = UN_PWAIT;
		return 0;
	}

	/*
	 * Assure that whenever there is printer data in the output
	 * buffer, there always remains enough space after it to
	 * turn the printer off.
	 */
	space -= ch->ch_digi.digi_offlen;

	if (space <= 0) {
		*un_flag = UN_EMPTY;
		return 0;
	}

	/*
	 * We measure printer CPS speed by incrementing
	 * ch_cpstime by (HZ / digi_maxcps) for every
	 * character we output, restricting output so
	 * that ch_cpstime never exceeds lbolt.
	 *
	 * However if output has not been done for some
	 * time, lbolt will grow to very much larger than
	 * ch_cpstime, which would allow essentially
	 * unlimited amounts of output until ch_cpstime
	 * finally caught up.   To avoid this, we adjust
	 * cps_time when necessary so the difference
	 * between lbolt and ch_cpstime never results
	 * in sending more than digi_bufsize characters.
	 *
	 * This nicely models a printer with an internal
	 * buffer of digi_bufsize characters.
	 *
	 * Get the time between lbolt and ch->ch_cpstime;
	 */

	tt = jiffies - ch->ch_cpstime;

	/*
	 * Compute the time required to send digi_bufsize
	 * characters.
	 */

	mt = HZ * ch->ch_digi.digi_bufsize / ch->ch_digi.digi_maxcps;

	/*
	 * Compute the number of characters that can be sent
	 * without violating the time constraint.   If the
	 * direct calculation of this number is bigger than
	 * digi_bufsize, limit the number to digi_bufsize,
	 * and adjust cpstime to match.
	 */

	if ((clock_t)(tt + HZ) > (clock_t)(mt + HZ)) {
		tmax = ch->ch_digi.digi_bufsize;
		ch->ch_cpstime = jiffies - mt;
	} else {
		tmax = ch->ch_digi.digi_maxcps * tt / HZ;
	}

	/*
	 * If the time constraint now binds, limit the transmit
	 * count accordingly, and tentatively arrange to be
	 * rescheduled based on time.
	 */

	if (tmax < space) {
		*un_flag = UN_TIME;
		space = tmax;
	}

	/*
	 * Compute the total number of characters we can
	 * output before the total number of characters known
	 * to be in the output queue exceeds digi_maxchar.
	 */

	tmax = (ch->ch_digi.digi_maxchar -
		((ch->ch_tin - ch->ch_tout) & TBUF_MASK) -
		((ch->ch_s_tin - ch->ch_s_tpos) & 0xffff));


	/*
	 * If the digi_maxchar constraint now holds, limit
	 * the transmit count accordingly, and arrange to
	 * be rescheduled when the queue becomes empty.
	 */

	if (space > tmax) {
		*un_flag = UN_EMPTY;
		space = tmax;
	}

	if (space <= 0)
		*un_flag |= UN_EMPTY;

	return space;
}


static int dgrp_tty_write(struct tty_struct *tty,
			  const unsigned char *buf,
			  int count)
{
	struct nd_struct *nd;
	struct un_struct *un;
	struct ch_struct *ch;
	int	space;
	int	n;
	int	t;
	int sendcount;
	int un_flag;
	ulong lock_flags;

	if (tty == NULL)
		return 0;

	un = tty->driver_data;
	if (!un)
		return 0;

	ch = un->un_ch;
	if (!ch)
		return 0;

	nd = ch->ch_nd;
	if (!nd)
		return 0;

	/*
	 * Ignore the request if the channel is not ready.
	 */
	if (ch->ch_state != CS_READY)
		return 0;

	spin_lock_irqsave(&dgrp_poll_data.poll_lock, lock_flags);

	/*
	 * Ignore the request if output is blocked.
	 */
	if ((un->un_flag & (UN_EMPTY | UN_LOW | UN_TIME | UN_PWAIT)) != 0) {
		count = 0;
		goto out;
	}

	/*
	 * Also ignore the request if DPA has this port open,
	 * and is flow controlled on reading more data.
	 */
	if (nd->nd_dpa_debug && nd->nd_dpa_flag & DPA_WAIT_SPACE &&
		nd->nd_dpa_port == MINOR(tty_devnum(ch->ch_tun.un_tty))) {
		count = 0;
		goto out;
	}

	/*
	 *	Limit amount we will write to the amount of space
	 *	available in the channel buffer.
	 */
	sendcount = 0;

	space = (ch->ch_tout - ch->ch_tin - 1) & TBUF_MASK;

	/*
	 * Handle the printer device.
	 */

	un_flag = UN_LOW;

	if (IS_PRINT(MINOR(tty_devnum(tty)))) {
		clock_t tt;
		clock_t mt;
		unsigned short tmax = 0;

		/*
		 * If the terminal device is busy, reschedule when
		 * the terminal device becomes idle.
		 */

		if (ch->ch_tun.un_open_count != 0 &&
		    ((ch->ch_tun.un_tty->ops->chars_in_buffer)(ch->ch_tun.un_tty) != 0)) {
			un->un_flag |= UN_PWAIT;
			count = 0;
			goto out;
		}

		/*
		 * Assure that whenever there is printer data in the output
		 * buffer, there always remains enough space after it to
		 * turn the printer off.
		 */
		space -= ch->ch_digi.digi_offlen;

		/*
		 * Output the printer on string.
		 */

		if ((ch->ch_flag & CH_PRON) == 0) {
			space -= ch->ch_digi.digi_onlen;

			if (space < 0) {
				un->un_flag |= UN_EMPTY;
				(ch->ch_nd)->nd_tx_work = 1;
				count = 0;
				goto out;
			}

			drp_wmove(ch, 0, ch->ch_digi.digi_onstr,
				ch->ch_digi.digi_onlen);

			ch->ch_flag |= CH_PRON;
		}

		/*
		 * We measure printer CPS speed by incrementing
		 * ch_cpstime by (HZ / digi_maxcps) for every
		 * character we output, restricting output so
		 * that ch_cpstime never exceeds lbolt.
		 *
		 * However if output has not been done for some
		 * time, lbolt will grow to very much larger than
		 * ch_cpstime, which would allow essentially
		 * unlimited amounts of output until ch_cpstime
		 * finally caught up.   To avoid this, we adjust
		 * cps_time when necessary so the difference
		 * between lbolt and ch_cpstime never results
		 * in sending more than digi_bufsize characters.
		 *
		 * This nicely models a printer with an internal
		 * buffer of digi_bufsize characters.
		 *
		 * Get the time between lbolt and ch->ch_cpstime;
		 */

		tt = jiffies - ch->ch_cpstime;

		/*
		 * Compute the time required to send digi_bufsize
		 * characters.
		 */

		mt = HZ * ch->ch_digi.digi_bufsize / ch->ch_digi.digi_maxcps;

		/*
		 * Compute the number of characters that can be sent
		 * without violating the time constraint.   If the
		 * direct calculation of this number is bigger than
		 * digi_bufsize, limit the number to digi_bufsize,
		 * and adjust cpstime to match.
		 */

		if ((clock_t)(tt + HZ) > (clock_t)(mt + HZ)) {
			tmax = ch->ch_digi.digi_bufsize;
			ch->ch_cpstime = jiffies - mt;
		} else {
			tmax = ch->ch_digi.digi_maxcps * tt / HZ;
		}

		/*
		 * If the time constraint now binds, limit the transmit
		 * count accordingly, and tentatively arrange to be
		 * rescheduled based on time.
		 */

		if (tmax < space) {
			space = tmax;
			un_flag = UN_TIME;
		}

		/*
		 * Compute the total number of characters we can
		 * output before the total number of characters known
		 * to be in the output queue exceeds digi_maxchar.
		 */

		tmax = (ch->ch_digi.digi_maxchar -
			((ch->ch_tin - ch->ch_tout) & TBUF_MASK) -
			((ch->ch_s_tin - ch->ch_s_tpos) & 0xffff));


		/*
		 * If the digi_maxchar constraint now holds, limit
		 * the transmit count accordingly, and arrange to
		 * be rescheduled when the queue becomes empty.
		 */

		if (space > tmax) {
			space = tmax;
			un_flag = UN_EMPTY;
		}

	}
	/*
	 * Handle the terminal device.
	 */
	else {

		/*
		 * If the printer device is on, turn it off.
		 */

		if ((ch->ch_flag & CH_PRON) != 0) {

			space -= ch->ch_digi.digi_offlen;

			drp_wmove(ch, 0, ch->ch_digi.digi_offstr,
				  ch->ch_digi.digi_offlen);

			ch->ch_flag &= ~CH_PRON;
		}
	}

	/*
	 *	If space is 0 and its because the ch->tbuf
	 *	is full, then Linux will handle a callback when queue
	 *	space becomes available.
	 *	tty_write returns count = 0
	 */

	if (space <= 0) {
		/* the linux tty_io.c handles this if we return 0 */
		/* if (fp->flags & O_NONBLOCK) return -EAGAIN; */

		un->un_flag |= UN_EMPTY;
		(ch->ch_nd)->nd_tx_work = 1;
		count = 0;
		goto out;
	}

	count = min(count, space);

	if (count > 0) {

		un->un_tbusy++;

		/*
		 *	Copy the buffer contents to the ch_tbuf
		 *	being careful to wrap around the circular queue
		 */

		t = TBUF_MAX - ch->ch_tin;
		n = count;

		if (n >= t) {
			memcpy(ch->ch_tbuf + ch->ch_tin, buf, t);
			if (nd->nd_dpa_debug && nd->nd_dpa_port == PORT_NUM(MINOR(tty_devnum(un->un_tty))))
				dgrp_dpa_data(nd, 0, (char *) buf, t);
			buf += t;
			n -= t;
			ch->ch_tin = 0;
			sendcount += n;
		}

		memcpy(ch->ch_tbuf + ch->ch_tin, buf, n);
		if (nd->nd_dpa_debug && nd->nd_dpa_port == PORT_NUM(MINOR(tty_devnum(un->un_tty))))
			dgrp_dpa_data(nd, 0, (char *) buf, n);
		buf += n;
		ch->ch_tin += n;
		sendcount += n;

		un->un_tbusy--;
		(ch->ch_nd)->nd_tx_work = 1;
		if (ch->ch_edelay != DGRP_RTIME) {
			(ch->ch_nd)->nd_tx_ready = 1;
			wake_up_interruptible(&nd->nd_tx_waitq);
		}
	}

	ch->ch_txcount += count;

	if (IS_PRINT(MINOR(tty_devnum(tty)))) {

		/*
		 * Adjust ch_cpstime to account
		 * for the characters just output.
		 */

		if (sendcount > 0) {
			int cc = HZ * sendcount + ch->ch_cpsrem;

			ch->ch_cpstime += cc / ch->ch_digi.digi_maxcps;
			ch->ch_cpsrem   = cc % ch->ch_digi.digi_maxcps;
		}

		/*
		 * If we are now waiting on time, schedule ourself
		 * back when we'll be able to send a block of
		 * digi_maxchar characters.
		 */

		if ((un_flag & UN_TIME) != 0) {
			ch->ch_waketime = (ch->ch_cpstime +
				(ch->ch_digi.digi_maxchar * HZ /
				ch->ch_digi.digi_maxcps));
		}
	}

	/*
	 * If the printer unit is waiting for completion
	 * of terminal output, get him going again.
	 */

	if ((ch->ch_pun.un_flag & UN_PWAIT) != 0)
		(ch->ch_nd)->nd_tx_work = 1;

out:
	spin_unlock_irqrestore(&dgrp_poll_data.poll_lock, lock_flags);

	return count;
}


/*
 *	Put a character into ch->ch_buf
 *
 *	- used by the line discipline for OPOST processing
 */

static int dgrp_tty_put_char(struct tty_struct *tty, unsigned char new_char)
{
	struct un_struct *un;
	struct ch_struct *ch;
	ulong  lock_flags;
	int space;
	int retval = 0;

	if (tty == NULL)
		return 0;

	un = tty->driver_data;
	if (!un)
		return 0;

	ch = un->un_ch;
	if (!ch)
		return 0;

	if (ch->ch_state != CS_READY)
		return 0;

	spin_lock_irqsave(&dgrp_poll_data.poll_lock, lock_flags);


	/*
	 *	If space is 0 and its because the ch->tbuf
	 *	Warn and dump the character, there isn't anything else
	 *	we can do about it.  David_Fries@digi.com
	 */

	space = (ch->ch_tout - ch->ch_tin - 1) & TBUF_MASK;

	un->un_tbusy++;

	/*
	 * Output the printer on string if device is TXPrint.
	 */
	if (IS_PRINT(MINOR(tty_devnum(tty))) && (ch->ch_flag & CH_PRON) == 0) {
		if (space < ch->ch_digi.digi_onlen) {
			un->un_tbusy--;
			goto out;
		}
		space -= ch->ch_digi.digi_onlen;
		drp_wmove(ch, 0, ch->ch_digi.digi_onstr,
			  ch->ch_digi.digi_onlen);
		ch->ch_flag |= CH_PRON;
	}

	/*
	 * Output the printer off string if device is NOT TXPrint.
	 */

	if (!IS_PRINT(MINOR(tty_devnum(tty))) &&
	    ((ch->ch_flag & CH_PRON) != 0)) {
		if (space < ch->ch_digi.digi_offlen) {
			un->un_tbusy--;
			goto out;
		}

		space -= ch->ch_digi.digi_offlen;
		drp_wmove(ch, 0, ch->ch_digi.digi_offstr,
			  ch->ch_digi.digi_offlen);
		ch->ch_flag &= ~CH_PRON;
	}

	if (!space) {
		un->un_tbusy--;
		goto out;
	}

	/*
	 *	Copy the character to the ch_tbuf being
	 *	careful to wrap around the circular queue
	 */
	ch->ch_tbuf[ch->ch_tin] = new_char;
	ch->ch_tin = (1 + ch->ch_tin) & TBUF_MASK;

	if (IS_PRINT(MINOR(tty_devnum(tty)))) {

		/*
		 * Adjust ch_cpstime to account
		 * for the character just output.
		 */

		int cc = HZ + ch->ch_cpsrem;

		ch->ch_cpstime += cc / ch->ch_digi.digi_maxcps;
		ch->ch_cpsrem   = cc % ch->ch_digi.digi_maxcps;

		/*
		 * If we are now waiting on time, schedule ourself
		 * back when we'll be able to send a block of
		 * digi_maxchar characters.
		 */

		ch->ch_waketime = (ch->ch_cpstime +
			(ch->ch_digi.digi_maxchar * HZ /
			ch->ch_digi.digi_maxcps));
	}


	un->un_tbusy--;
	(ch->ch_nd)->nd_tx_work = 1;

	retval = 1;
out:
	spin_unlock_irqrestore(&dgrp_poll_data.poll_lock, lock_flags);
	return retval;
}



/*
 *	Flush TX buffer (make in == out)
 *
 *	check tty_ioctl.c  -- this is called after TCOFLUSH
 */
static void dgrp_tty_flush_buffer(struct tty_struct *tty)
{
	struct un_struct *un;
	struct ch_struct *ch;

	if (!tty)
		return;
	un = tty->driver_data;
	if (!un)
		return;

	ch = un->un_ch;
	if (!ch)
		return;

	ch->ch_tout = ch->ch_tin;
	/* do NOT do this here! */
	/* ch->ch_s_tpos = ch->ch_s_tin = 0; */

	/* send the flush output command now */
	ch->ch_send |= RR_TX_FLUSH;
	(ch->ch_nd)->nd_tx_ready = 1;
	(ch->ch_nd)->nd_tx_work = 1;
	wake_up_interruptible(&(ch->ch_nd)->nd_tx_waitq);

	if (waitqueue_active(&tty->write_wait))
		wake_up_interruptible(&tty->write_wait);

	tty_wakeup(tty);

}

/*
 *	Return space available in Tx buffer
 *	count = ( ch->ch_tout - ch->ch_tin ) mod (TBUF_MAX - 1)
 */
static int dgrp_tty_write_room(struct tty_struct *tty)
{
	struct un_struct *un;
	struct ch_struct *ch;
	int	count;

	if (!tty)
		return 0;

	un = tty->driver_data;
	if (!un)
		return 0;

	ch = un->un_ch;
	if (!ch)
		return 0;

	count = (ch->ch_tout - ch->ch_tin - 1) & TBUF_MASK;

	/* We *MUST* check this, and return 0 if the Printer Unit cannot
	 * take any more data within its time constraints...  If we don't
	 * return 0 and the printer has hit it time constraint, the ld will
	 * call us back doing a put_char, which cannot be rejected!!!
	 */
	if (IS_PRINT(MINOR(tty_devnum(tty)))) {
		int un_flag = 0;
		count = dgrp_calculate_txprint_bounds(ch, count, &un_flag);
		if (count <= 0)
			count = 0;

		ch->ch_pun.un_flag |= un_flag;
		(ch->ch_nd)->nd_tx_work = 1;
	}

	return count;
}

/*
 *	Return number of characters that have not been transmitted yet.
 *	chars_in_buffer = ( ch->ch_tin - ch->ch_tout ) mod (TBUF_MAX - 1)
 *			+ ( ch->ch_s_tin - ch->ch_s_tout ) mod (0xffff)
 *			= number of characters "in transit"
 *
 * Remember that sequence number math is always with a sixteen bit
 * mask, not the TBUF_MASK.
 */

static int dgrp_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct un_struct *un;
	struct ch_struct *ch;
	int	count;
	int	count1;

	if (!tty)
		return 0;

	un = tty->driver_data;
	if (!un)
		return 0;

	ch = un->un_ch;
	if (!ch)
		return 0;

	count1 = count = (ch->ch_tin - ch->ch_tout) & TBUF_MASK;
	count += (ch->ch_s_tin - ch->ch_s_tpos) & 0xffff;
	/* one for tbuf, one for the PS */

	/*
	 * If we are busy transmitting add 1
	 */
	count += un->un_tbusy;

	return count;
}


/*****************************************************************************
 *
 * Helper applications for dgrp_tty_ioctl()
 *
 *****************************************************************************
 */


/**
 * ch_to_tty_flags() -- convert channel flags to termio flags
 * @ch_flag: Digi channel flags
 * @flagtype: type of ch_flag (iflag, oflag or cflag)
 *
 * take the channel flags of the specified type and return the
 * corresponding termio flag
 */
static tcflag_t ch_to_tty_flags(ushort ch_flag, char flagtype)
{
	tcflag_t retval = 0;

	switch (flagtype) {
	case 'i':
		retval = ((ch_flag & IF_IGNBRK) ? IGNBRK : 0)
		     | ((ch_flag & IF_BRKINT) ? BRKINT : 0)
		     | ((ch_flag & IF_IGNPAR) ? IGNPAR : 0)
		     | ((ch_flag & IF_PARMRK) ? PARMRK : 0)
		     | ((ch_flag & IF_INPCK) ? INPCK  : 0)
		     | ((ch_flag & IF_ISTRIP) ? ISTRIP : 0)
		     | ((ch_flag & IF_IXON) ? IXON   : 0)
		     | ((ch_flag & IF_IXANY) ? IXANY  : 0)
		     | ((ch_flag & IF_IXOFF) ? IXOFF  : 0);
		break;

	case 'o':
		retval = ((ch_flag & OF_OLCUC) ? OLCUC : 0)
		     | ((ch_flag & OF_ONLCR) ? ONLCR  : 0)
		     | ((ch_flag & OF_OCRNL) ? OCRNL  : 0)
		     | ((ch_flag & OF_ONOCR) ? ONOCR  : 0)
		     | ((ch_flag & OF_ONLRET) ? ONLRET : 0)
		  /* | ((ch_flag & OF_OTAB3) ? OFILL  : 0) */
		     | ((ch_flag & OF_TABDLY) ? TABDLY : 0);
		break;

	case 'c':
		retval = ((ch_flag & CF_CSTOPB) ? CSTOPB : 0)
		     | ((ch_flag & CF_CREAD) ? CREAD  : 0)
		     | ((ch_flag & CF_PARENB) ? PARENB : 0)
		     | ((ch_flag & CF_PARODD) ? PARODD : 0)
		     | ((ch_flag & CF_HUPCL) ? HUPCL  : 0);

		switch (ch_flag & CF_CSIZE) {
		case CF_CS5:
			retval |= CS5;
			break;
		case CF_CS6:
			retval |= CS6;
			break;
		case CF_CS7:
			retval |= CS7;
			break;
		case CF_CS8:
			retval |= CS8;
			break;
		default:
			retval |= CS8;
			break;
		}
		break;
	case 'x':
		break;
	case 'l':
		break;
	default:
		return 0;
	}

	return retval;
}


/**
 * tty_to_ch_flags() -- convert termio flags to digi channel flags
 * @tty: pointer to a TTY structure holding flag to be converted
 * @flagtype: identifies which flag (iflags, oflags, or cflags) should
 *                 be converted
 *
 * take the termio flag of the specified type and return the
 * corresponding Digi version of the flags
 */
static ushort tty_to_ch_flags(struct tty_struct *tty, char flagtype)
{
	ushort retval = 0;
	tcflag_t tflag = 0;

	switch (flagtype) {
	case 'i':
		tflag  = tty->termios.c_iflag;
		retval = (I_IGNBRK(tty) ? IF_IGNBRK : 0)
		      | (I_BRKINT(tty) ? IF_BRKINT : 0)
		      | (I_IGNPAR(tty) ? IF_IGNPAR : 0)
		      | (I_PARMRK(tty) ? IF_PARMRK : 0)
		      | (I_INPCK(tty)  ? IF_INPCK  : 0)
		      | (I_ISTRIP(tty) ? IF_ISTRIP : 0)
		      | (I_IXON(tty)   ? IF_IXON   : 0)
		      | (I_IXANY(tty)  ? IF_IXANY  : 0)
		      | (I_IXOFF(tty)  ? IF_IXOFF  : 0);
		break;
	case 'o':
		tflag  = tty->termios.c_oflag;
		/*
		 * If OPOST is set, then do the post processing in the
		 * firmware by setting all the processing flags on.
		 * If ~OPOST, then make sure we are not doing any
		 * output processing!!
		 */
		if (!O_OPOST(tty))
			retval = 0;
		else
			retval = (O_OLCUC(tty) ? OF_OLCUC : 0)
			     | (O_ONLCR(tty)  ? OF_ONLCR  : 0)
			     | (O_OCRNL(tty)  ? OF_OCRNL  : 0)
			     | (O_ONOCR(tty)  ? OF_ONOCR  : 0)
			     | (O_ONLRET(tty) ? OF_ONLRET : 0)
			  /* | (O_OFILL(tty)  ? OF_TAB3   : 0) */
			     | (O_TABDLY(tty) ? OF_TABDLY : 0);
		break;
	case 'c':
		tflag  = tty->termios.c_cflag;
		retval = (C_CSTOPB(tty) ? CF_CSTOPB : 0)
		     | (C_CREAD(tty)  ? CF_CREAD  : 0)
		     | (C_PARENB(tty) ? CF_PARENB : 0)
		     | (C_PARODD(tty) ? CF_PARODD : 0)
		     | (C_HUPCL(tty)  ? CF_HUPCL  : 0);
		switch (C_CSIZE(tty)) {
		case CS8:
			retval |= CF_CS8;
			break;
		case CS7:
			retval |= CF_CS7;
			break;
		case CS6:
			retval |= CF_CS6;
			break;
		case CS5:
			retval |= CF_CS5;
			break;
		default:
			retval |= CF_CS8;
			break;
		}
		break;
	case 'x':
		break;
	case 'l':
		break;
	default:
		return 0;
	}

	return retval;
}


static int dgrp_tty_send_break(struct tty_struct *tty, int msec)
{
	struct un_struct *un;
	struct ch_struct *ch;
	int ret = -EIO;

	if (!tty)
		return ret;

	un = tty->driver_data;
	if (!un)
		return ret;

	ch = un->un_ch;
	if (!ch)
		return ret;

	dgrp_send_break(ch, msec);
	return 0;
}


/*
 * This routine sends a break character out the serial port.
 *
 * duration is in 1/1000's of a second
 */
static int dgrp_send_break(struct ch_struct *ch, int msec)
{
	ulong x;

	wait_event_interruptible(ch->ch_flag_wait,
		((ch->ch_flag & CH_TX_BREAK) == 0));
	ch->ch_break_time += max(msec, 250);
	ch->ch_send |= RR_TX_BREAK;
	ch->ch_flag |= CH_TX_BREAK;
	(ch->ch_nd)->nd_tx_work = 1;

	x = (msec * HZ) / 1000;
	wake_up_interruptible(&(ch->ch_nd)->nd_tx_waitq);

	return 0;
}


/*
 * Return modem signals to ld.
 */
static int dgrp_tty_tiocmget(struct tty_struct *tty)
{
	unsigned int mlast;
	struct un_struct *un = tty->driver_data;
	struct ch_struct *ch;

	if (!un)
		return -ENODEV;

	ch = un->un_ch;
	if (!ch)
		return -ENODEV;

	mlast = ((ch->ch_s_mlast & ~(DM_RTS | DM_DTR)) |
		(ch->ch_mout & (DM_RTS | DM_DTR)));

	/* defined in /usr/include/asm/termios.h */
	mlast =   ((mlast & DM_RTS) ? TIOCM_RTS : 0)
		| ((mlast & DM_DTR) ? TIOCM_DTR : 0)
		| ((mlast & DM_CD)  ? TIOCM_CAR : 0)
		| ((mlast & DM_RI)  ? TIOCM_RNG : 0)
		| ((mlast & DM_DSR) ? TIOCM_DSR : 0)
		| ((mlast & DM_CTS) ? TIOCM_CTS : 0);

	return mlast;
}


/*
 *      Set modem lines
 */
static int dgrp_tty_tiocmset(struct tty_struct *tty,
			     unsigned int set, unsigned int clear)
{
	ulong lock_flags;
	struct un_struct *un = tty->driver_data;
	struct ch_struct *ch;

	if (!un)
		return -ENODEV;

	ch = un->un_ch;
	if (!ch)
		return -ENODEV;

	if (set & TIOCM_RTS)
		ch->ch_mout |= DM_RTS;

	if (set & TIOCM_DTR)
		ch->ch_mout |= DM_DTR;

	if (clear & TIOCM_RTS)
		ch->ch_mout &= ~(DM_RTS);

	if (clear & TIOCM_DTR)
		ch->ch_mout &= ~(DM_DTR);

	spin_lock_irqsave(&(ch->ch_nd)->nd_lock, lock_flags);
	ch->ch_flag |= CH_PARAM;
	(ch->ch_nd)->nd_tx_work = 1;
	wake_up_interruptible(&ch->ch_flag_wait);

	spin_unlock_irqrestore(&(ch->ch_nd)->nd_lock, lock_flags);

	return 0;
}



/*
 *      Get current modem status
 */
static int get_modem_info(struct ch_struct *ch, unsigned int *value)
{
	unsigned int mlast;

	mlast = ((ch->ch_s_mlast & ~(DM_RTS | DM_DTR)) |
		(ch->ch_mout    &  (DM_RTS | DM_DTR)));

	/* defined in /usr/include/asm/termios.h */
	mlast =   ((mlast & DM_RTS) ? TIOCM_RTS : 0)
		| ((mlast & DM_DTR) ? TIOCM_DTR : 0)
		| ((mlast & DM_CD)  ? TIOCM_CAR : 0)
		| ((mlast & DM_RI)  ? TIOCM_RNG : 0)
		| ((mlast & DM_DSR) ? TIOCM_DSR : 0)
		| ((mlast & DM_CTS) ? TIOCM_CTS : 0);
	return put_user(mlast, (unsigned int __user *) value);
}

/*
 *      Set modem lines
 */
static int set_modem_info(struct ch_struct *ch, unsigned int command,
			  unsigned int *value)
{
	int error;
	unsigned int arg;
	int mval = 0;
	ulong lock_flags;

	error = access_ok(VERIFY_READ, (void __user *) value, sizeof(int));
	if (error == 0)
		return -EFAULT;

	if (get_user(arg, (unsigned int __user *) value))
		return -EFAULT;
	mval |= ((arg & TIOCM_RTS) ? DM_RTS : 0)
		| ((arg & TIOCM_DTR) ? DM_DTR : 0);

	switch (command) {
	case TIOCMBIS:  /* set flags */
		ch->ch_mout |= mval;
		break;
	case TIOCMBIC:  /* clear flags */
		ch->ch_mout &= ~mval;
		break;
	case TIOCMSET:
		ch->ch_mout = mval;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&(ch->ch_nd)->nd_lock, lock_flags);

	ch->ch_flag |= CH_PARAM;
	(ch->ch_nd)->nd_tx_work = 1;
	wake_up_interruptible(&ch->ch_flag_wait);

	spin_unlock_irqrestore(&(ch->ch_nd)->nd_lock, lock_flags);

	return 0;
}


/*
 *  Assign the custom baud rate to the channel structure
 */
static void dgrp_set_custom_speed(struct ch_struct *ch, int newrate)
{
	int testdiv;
	int testrate_high;
	int testrate_low;

	int deltahigh, deltalow;

	if (newrate < 0)
		newrate = 0;

	/*
	 * Since the divisor is stored in a 16-bit integer, we make sure
	 * we don't allow any rates smaller than a 16-bit integer would allow.
	 * And of course, rates above the dividend won't fly.
	 */
	if (newrate && newrate < ((PORTSERVER_DIVIDEND / 0xFFFF) + 1))
		newrate = ((PORTSERVER_DIVIDEND / 0xFFFF) + 1);
	if (newrate && newrate > PORTSERVER_DIVIDEND)
		newrate = PORTSERVER_DIVIDEND;

	while (newrate > 0) {
		testdiv = PORTSERVER_DIVIDEND / newrate;

		/*
		 * If we try to figure out what rate the PortServer would use
		 * with the test divisor, it will be either equal or higher
		 * than the requested baud rate.  If we then determine the
		 * rate with a divisor one higher, we will get the next lower
		 * supported rate below the requested.
		 */
		testrate_high = PORTSERVER_DIVIDEND / testdiv;
		testrate_low  = PORTSERVER_DIVIDEND / (testdiv + 1);

		/*
		 * If the rate for the requested divisor is correct, just
		 * use it and be done.
		 */
		if (testrate_high == newrate)
			break;

		/*
		 * Otherwise, pick the rate that is closer (i.e. whichever rate
		 * has a smaller delta).
		 */
		deltahigh = testrate_high - newrate;
		deltalow = newrate - testrate_low;

		if (deltahigh < deltalow)
			newrate = testrate_high;
		else
			newrate = testrate_low;

		break;
	}

	ch->ch_custom_speed = newrate;

	drp_param(ch);

	return;
}


/*
 # dgrp_tty_digiseta()
 *
 * Ioctl to set the information from ditty.
 *
 * NOTE: DIGI_IXON, DSRPACE, DCDPACE, and DTRPACE are unsupported.  JAR 990922
 */
static int dgrp_tty_digiseta(struct tty_struct *tty,
			     struct digi_struct *new_info)
{
	struct un_struct *un = tty->driver_data;
	struct ch_struct *ch;

	if (!un)
		return -ENODEV;

	ch = un->un_ch;
	if (!ch)
		return -ENODEV;

	if (copy_from_user(&ch->ch_digi, (void __user *) new_info,
			   sizeof(struct digi_struct)))
		return -EFAULT;

	if ((ch->ch_digi.digi_flags & RTSPACE) ||
	    (ch->ch_digi.digi_flags & CTSPACE))
		tty->termios.c_cflag |= CRTSCTS;
	else
		tty->termios.c_cflag &= ~CRTSCTS;

	if (ch->ch_digi.digi_maxcps < 1)
		ch->ch_digi.digi_maxcps = 1;

	if (ch->ch_digi.digi_maxcps > 10000)
		ch->ch_digi.digi_maxcps = 10000;

	if (ch->ch_digi.digi_bufsize < 10)
		ch->ch_digi.digi_bufsize = 10;

	if (ch->ch_digi.digi_maxchar < 1)
		ch->ch_digi.digi_maxchar = 1;

	if (ch->ch_digi.digi_maxchar > ch->ch_digi.digi_bufsize)
		ch->ch_digi.digi_maxchar = ch->ch_digi.digi_bufsize;

	if (ch->ch_digi.digi_onlen > DIGI_PLEN)
		ch->ch_digi.digi_onlen = DIGI_PLEN;

	if (ch->ch_digi.digi_offlen > DIGI_PLEN)
		ch->ch_digi.digi_offlen = DIGI_PLEN;

	/* make the changes now */
	drp_param(ch);

	return 0;
}



/*
 * dgrp_tty_digigetedelay()
 *
 * Ioctl to get the current edelay setting.
 *
 *
 *
 */
static int dgrp_tty_digigetedelay(struct tty_struct *tty, int *retinfo)
{
	struct un_struct *un;
	struct ch_struct *ch;
	int tmp;

	if (!retinfo)
		return -EFAULT;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;

	if (!un)
		return -ENODEV;

	ch = un->un_ch;
	if (!ch)
		return -ENODEV;

	tmp = ch->ch_edelay;

	if (copy_to_user((void __user *) retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}


/*
 * dgrp_tty_digisetedelay()
 *
 * Ioctl to set the EDELAY setting
 *
 */
static int dgrp_tty_digisetedelay(struct tty_struct *tty, int *new_info)
{
	struct un_struct *un;
	struct ch_struct *ch;
	int new_digi;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;

	if (!un)
		return -ENODEV;

	ch = un->un_ch;
	if (!ch)
		return -ENODEV;

	if (copy_from_user(&new_digi, (void __user *)new_info, sizeof(int)))
		return -EFAULT;

	ch->ch_edelay = new_digi;

	/* make the changes now */
	drp_param(ch);

	return 0;
}


/*
 *	The usual assortment of ioctl's
 *
 *	note:  use tty_check_change to make sure that we are not
 *	changing the state of a terminal when we are not a process
 *	in the forground.  See tty_io.c
 *		rc = tty_check_change(tty);
 *		if (rc) return rc;
 */
static int dgrp_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
			  unsigned long arg)
{
	struct un_struct *un;
	struct ch_struct *ch;
	int rc;
	struct digiflow_struct   dflow;

	if (!tty)
		return -ENODEV;

	un = tty->driver_data;
	if (!un)
		return -ENODEV;

	ch = un->un_ch;
	if (!ch)
		return -ENODEV;

	switch (cmd) {

	/*
	 * Here are all the standard ioctl's that we MUST implement
	 */

	case TCSBRK:
		/*
		 * TCSBRK is SVID version: non-zero arg --> no break
		 * this behaviour is exploited by tcdrain().
		 *
		 * According to POSIX.1 spec (7.2.2.1.2) breaks should be
		 * between 0.25 and 0.5 seconds
		 */

		rc = tty_check_change(tty);
		if (rc)
			return rc;
		tty_wait_until_sent(tty, 0);

		if (!arg)
			rc = dgrp_send_break(ch, 250); /* 1/4 second */

		if (dgrp_tty_chars_in_buffer(tty) != 0)
			return -EINTR;

		return 0;

	case TCSBRKP:
		/* support for POSIX tcsendbreak()
		 *
		 * According to POSIX.1 spec (7.2.2.1.2) breaks should be
		 * between 0.25 and 0.5 seconds so we'll ask for something
		 * in the middle: 0.375 seconds.
		 */
		rc = tty_check_change(tty);
		if (rc)
			return rc;
		tty_wait_until_sent(tty, 0);

		rc = dgrp_send_break(ch, arg ? arg*250 : 250);

		if (dgrp_tty_chars_in_buffer(tty) != 0)
			return -EINTR;
		return 0;

	case TIOCSBRK:
		rc = tty_check_change(tty);
		if (rc)
			return rc;
		tty_wait_until_sent(tty, 0);

		/*
		 * RealPort doesn't support turning on a break unconditionally.
		 * The RealPort device will stop sending a break automatically
		 * after the specified time value that we send in.
		 */
		rc = dgrp_send_break(ch, 250); /* 1/4 second */

		if (dgrp_tty_chars_in_buffer(tty) != 0)
			return -EINTR;
		return 0;

	case TIOCCBRK:
		/*
		 * RealPort doesn't support turning off a break unconditionally.
		 * The RealPort device will stop sending a break automatically
		 * after the specified time value that was sent when turning on
		 * the break.
		 */
		return 0;

	case TIOCMGET:
		rc = access_ok(VERIFY_WRITE, (void __user *) arg,
				 sizeof(unsigned int));
		if (rc == 0)
			return -EFAULT;
		return get_modem_info(ch, (unsigned int *) arg);

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		return set_modem_info(ch, cmd, (unsigned int *) arg);

	/*
	 * Here are any additional ioctl's that we want to implement
	 */

	case TCFLSH:
		/*
		 * The linux tty driver doesn't have a flush
		 * input routine for the driver, assuming all backed
		 * up data is in the line disc. buffers.  However,
		 * we all know that's not the case.  Here, we
		 * act on the ioctl, but then lie and say we didn't
		 * so the line discipline will process the flush
		 * also.
		 */
		rc = tty_check_change(tty);
		if (rc)
			return rc;

		switch (arg) {
		case TCIFLUSH:
		case TCIOFLUSH:
			/* only flush input if this is the only open unit */
			if (!IS_PRINT(MINOR(tty_devnum(tty)))) {
				ch->ch_rout = ch->ch_rin;
				ch->ch_send |= RR_RX_FLUSH;
				(ch->ch_nd)->nd_tx_work = 1;
				(ch->ch_nd)->nd_tx_ready = 1;
				wake_up_interruptible(&(ch->ch_nd)->nd_tx_waitq);
			}
			if (arg == TCIFLUSH)
				break;

		case TCOFLUSH: /* flush output, or the receive buffer */
			/*
			 * This is handled in the tty_ioctl.c code
			 * calling tty_flush_buffer
			 */
			break;

		default:
			/* POSIX.1 says return EINVAL if we got a bad arg */
			return -EINVAL;
		}
		/* pretend we didn't recognize this IOCTL */
		return -ENOIOCTLCMD;

#ifdef TIOCGETP
	case TIOCGETP:
#endif
	/*****************************************
	Linux		HPUX		Function
	TCSETA		TCSETA		- set the termios
	TCSETAF		TCSETAF		- wait for drain first, then set termios
	TCSETAW		TCSETAW		- wait for drain, flush the input queue, then set termios
	- looking at the tty_ioctl code, these command all call our
	tty_set_termios at the driver's end, when a TCSETA* is sent,
	it is expecting the tty to have a termio structure,
	NOT a termios structure.  These two structures differ in size
	and the tty_ioctl code does a conversion before processing them both.
	- we should treat the TCSETAW TCSETAF ioctls the same, and let
	the tty_ioctl code do the conversion stuff.

	TCSETS
	TCSETSF		(none)
	TCSETSW
	- the associated tty structure has a termios structure.
	*****************************************/

	case TCGETS:
	case TCGETA:
		return -ENOIOCTLCMD;

	case TCSETAW:
	case TCSETAF:
	case TCSETSF:
	case TCSETSW:
		/*
		 * The linux tty driver doesn't have a flush
		 * input routine for the driver, assuming all backed
		 * up data is in the line disc. buffers.  However,
		 * we all know that's not the case.  Here, we
		 * act on the ioctl, but then lie and say we didn't
		 * so the line discipline will process the flush
		 * also.
		 */

		/*
		 * Also, now that we have TXPrint, we have to check
		 * if this is the TXPrint device and the terminal
		 * device is open. If so, do NOT run check_change,
		 * as the terminal device is ALWAYS the parent.
		 */
		if (!IS_PRINT(MINOR(tty_devnum(tty))) ||
		    !ch->ch_tun.un_open_count) {
			rc = tty_check_change(tty);
			if (rc)
				return rc;
		}

		/* wait for all the characters in tbuf to drain */
		tty_wait_until_sent(tty, 0);

		if ((cmd == TCSETSF) || (cmd == TCSETAF)) {
			/* flush the contents of the rbuf queue */
			/* TODO:  check if this is print device? */
			ch->ch_send |= RR_RX_FLUSH;
			(ch->ch_nd)->nd_tx_ready = 1;
			(ch->ch_nd)->nd_tx_work = 1;
			wake_up_interruptible(&(ch->ch_nd)->nd_tx_waitq);
			/* do we need to do this?  just to be safe! */
			ch->ch_rout = ch->ch_rin;
		}

		/* pretend we didn't recognize this */
		return -ENOIOCTLCMD;

	case TCXONC:
		/*
		 * The Linux Line Discipline (LD) would do this for us if we
		 * let it, but we have the special firmware options to do this
		 * the "right way" regardless of hardware or software flow
		 * control so we'll do it outselves instead of letting the LD
		 * do it.
		 */
		rc = tty_check_change(tty);
		if (rc)
			return rc;

		switch (arg) {
		case TCOON:
			dgrp_tty_start(tty);
			return 0;
		case TCOOFF:
			dgrp_tty_stop(tty);
			return 0;
		case TCION:
			dgrp_tty_input_start(tty);
			return 0;
		case TCIOFF:
			dgrp_tty_input_stop(tty);
			return 0;
		default:
			return -EINVAL;
		}

	case DIGI_GETA:
		/* get information for ditty */
		if (copy_to_user((struct digi_struct __user *) arg,
				 &ch->ch_digi, sizeof(struct digi_struct)))
			return -EFAULT;
		break;

	case DIGI_SETAW:
	case DIGI_SETAF:
		/* wait for all the characters in tbuf to drain */
		tty_wait_until_sent(tty, 0);

		if (cmd == DIGI_SETAF) {
			/* flush the contents of the rbuf queue */
			/* send down a packet with RR_RX_FLUSH set */
			ch->ch_send |= RR_RX_FLUSH;
			(ch->ch_nd)->nd_tx_ready = 1;
			(ch->ch_nd)->nd_tx_work = 1;
			wake_up_interruptible(&(ch->ch_nd)->nd_tx_waitq);
			/* do we need to do this?  just to be safe! */
			ch->ch_rout = ch->ch_rin;
		}

		/* pretend we didn't recognize this */

	case DIGI_SETA:
		return dgrp_tty_digiseta(tty, (struct digi_struct *) arg);

	case DIGI_SEDELAY:
		return dgrp_tty_digisetedelay(tty, (int *) arg);

	case DIGI_GEDELAY:
		return dgrp_tty_digigetedelay(tty, (int *) arg);

	case DIGI_GETFLOW:
	case DIGI_GETAFLOW:
		if (cmd == (DIGI_GETFLOW)) {
			dflow.startc = tty->termios.c_cc[VSTART];
			dflow.stopc = tty->termios.c_cc[VSTOP];
		} else {
			dflow.startc = ch->ch_xxon;
			dflow.stopc = ch->ch_xxoff;
		}

		if (copy_to_user((char __user *)arg, &dflow, sizeof(dflow)))
			return -EFAULT;
		break;

	case DIGI_SETFLOW:
	case DIGI_SETAFLOW:

		if (copy_from_user(&dflow, (char __user *)arg, sizeof(dflow)))
			return -EFAULT;

		if (cmd == (DIGI_SETFLOW)) {
			tty->termios.c_cc[VSTART] = dflow.startc;
			tty->termios.c_cc[VSTOP] = dflow.stopc;
		} else {
			ch->ch_xxon = dflow.startc;
			ch->ch_xxoff = dflow.stopc;
		}
		break;

	case DIGI_GETCUSTOMBAUD:
		if (put_user(ch->ch_custom_speed, (unsigned int __user *) arg))
			return -EFAULT;
		break;

	case DIGI_SETCUSTOMBAUD:
	{
		int new_rate;

		if (get_user(new_rate, (unsigned int __user *) arg))
			return -EFAULT;
		dgrp_set_custom_speed(ch, new_rate);

		break;
	}

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

/*
 *  This routine allows the tty driver to be notified when
 *  the device's termios setting have changed.  Note that we
 *  should be prepared to accept the case where old == NULL
 *  and try to do something rational.
 *
 *  So we need to make sure that our copies of ch_oflag,
 *  ch_clag, and ch_iflag reflect the tty->termios flags.
 */
static void dgrp_tty_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	struct ktermios *ts;
	struct ch_struct *ch;
	struct un_struct *un;

	/* seems silly, but we have to check all these! */
	if (!tty)
		return;

	un = tty->driver_data;
	if (!un)
		return;

	ts = &tty->termios;

	ch = un->un_ch;
	if (!ch)
		return;

	drp_param(ch);

	/* the CLOCAL flag has just been set */
	if (!(old->c_cflag & CLOCAL) && C_CLOCAL(tty))
		wake_up_interruptible(&un->un_open_wait);
}


/*
 *	Throttle receiving data.  We just set a bit and stop reading
 *	data out of the channel buffer.  It will back up and the
 *	FEP will do whatever is necessary to stop the far end.
 */
static void dgrp_tty_throttle(struct tty_struct *tty)
{
	struct ch_struct *ch;

	if (!tty)
		return;

	ch = ((struct un_struct *) tty->driver_data)->un_ch;
	if (!ch)
		return;

	ch->ch_flag |= CH_RXSTOP;
}


static void dgrp_tty_unthrottle(struct tty_struct *tty)
{
	struct ch_struct *ch;

	if (!tty)
		return;

	ch = ((struct un_struct *) tty->driver_data)->un_ch;
	if (!ch)
		return;

	ch->ch_flag &= ~CH_RXSTOP;
}

/*
 *	Stop the transmitter
 */
static void dgrp_tty_stop(struct tty_struct *tty)
{
	struct ch_struct *ch;

	if (!tty)
		return;

	ch = ((struct un_struct *) tty->driver_data)->un_ch;
	if (!ch)
		return;

	ch->ch_send |= RR_TX_STOP;
	ch->ch_send &= ~RR_TX_START;

	/* make the change NOW! */
	(ch->ch_nd)->nd_tx_ready = 1;
	if (waitqueue_active(&(ch->ch_nd)->nd_tx_waitq))
		wake_up_interruptible(&(ch->ch_nd)->nd_tx_waitq);
}

/*
 *	Start the transmitter
 */
static void dgrp_tty_start(struct tty_struct *tty)
{
	struct ch_struct *ch;

	if (!tty)
		return;

	ch = ((struct un_struct *) tty->driver_data)->un_ch;
	if (!ch)
		return;

	/* TODO: don't do anything if the transmitter is not stopped */

	ch->ch_send |= RR_TX_START;
	ch->ch_send &= ~RR_TX_STOP;

	/* make the change NOW! */
	(ch->ch_nd)->nd_tx_ready = 1;
	(ch->ch_nd)->nd_tx_work = 1;
	if (waitqueue_active(&(ch->ch_nd)->nd_tx_waitq))
		wake_up_interruptible(&(ch->ch_nd)->nd_tx_waitq);

}

/*
 *	Stop the receiver
 */
static void dgrp_tty_input_stop(struct tty_struct *tty)
{
	struct ch_struct *ch;

	if (!tty)
		return;

	ch = ((struct un_struct *) tty->driver_data)->un_ch;
	if (!ch)
		return;

	ch->ch_send |= RR_RX_STOP;
	ch->ch_send &= ~RR_RX_START;
	(ch->ch_nd)->nd_tx_ready = 1;
	if (waitqueue_active(&(ch->ch_nd)->nd_tx_waitq))
		wake_up_interruptible(&(ch->ch_nd)->nd_tx_waitq);

}


static void dgrp_tty_send_xchar(struct tty_struct *tty, char c)
{
	struct un_struct *un;
	struct ch_struct *ch;

	if (!tty)
		return;

	un = tty->driver_data;
	if (!un)
		return;

	ch = un->un_ch;
	if (!ch)
		return;
	if (c == STOP_CHAR(tty))
		ch->ch_send |= RR_RX_STOP;
	else if (c == START_CHAR(tty))
		ch->ch_send |= RR_RX_START;

	ch->ch_nd->nd_tx_ready = 1;
	ch->ch_nd->nd_tx_work = 1;

	return;
}


static void dgrp_tty_input_start(struct tty_struct *tty)
{
	struct ch_struct *ch;

	if (!tty)
		return;

	ch = ((struct un_struct *) tty->driver_data)->un_ch;
	if (!ch)
		return;

	ch->ch_send |= RR_RX_START;
	ch->ch_send &= ~RR_RX_STOP;
	(ch->ch_nd)->nd_tx_ready = 1;
	(ch->ch_nd)->nd_tx_work = 1;
	if (waitqueue_active(&(ch->ch_nd)->nd_tx_waitq))
		wake_up_interruptible(&(ch->ch_nd)->nd_tx_waitq);

}


/*
 *	Hangup the port.  Like a close, but don't wait for output
 *	to drain.
 *
 *	How do we close all the channels that are open?
 */
static void dgrp_tty_hangup(struct tty_struct *tty)
{
	struct ch_struct *ch;
	struct nd_struct *nd;
	struct un_struct *un;

	if (!tty)
		return;

	un = tty->driver_data;
	if (!un)
		return;

	ch = un->un_ch;
	if (!ch)
		return;

	nd = ch->ch_nd;

	if (C_HUPCL(tty)) {
		/* LOWER DTR */
		ch->ch_mout &= ~DM_DTR;
		/* Don't do this here */
		/* ch->ch_flag |= CH_HANGUP; */
		ch->ch_nd->nd_tx_ready = 1;
		ch->ch_nd->nd_tx_work  = 1;
		if (waitqueue_active(&ch->ch_flag_wait))
			wake_up_interruptible(&ch->ch_flag_wait);
	}

}

/************************************************************************/
/*                                                                      */
/*       TTY Initialization/Cleanup Functions                           */
/*                                                                      */
/************************************************************************/

/*
 *	Uninitialize the TTY portion of the supplied node.  Free all
 *      memory and resources associated with this node.  Do it in reverse
 *      allocation order: this might possibly result in less fragmentation
 *      of memory, though I don't know this for sure.
 */
void
dgrp_tty_uninit(struct nd_struct *nd)
{
	unsigned int i;
	char id[3];

	ID_TO_CHAR(nd->nd_ID, id);

	if (nd->nd_ttdriver_flags & SERIAL_TTDRV_REG) {
		tty_unregister_driver(nd->nd_serial_ttdriver);

		kfree(nd->nd_serial_ttdriver->ttys);
		nd->nd_serial_ttdriver->ttys = NULL;

		put_tty_driver(nd->nd_serial_ttdriver);
		nd->nd_ttdriver_flags &= ~SERIAL_TTDRV_REG;
	}

	if (nd->nd_ttdriver_flags & CALLOUT_TTDRV_REG) {
		tty_unregister_driver(nd->nd_callout_ttdriver);

		kfree(nd->nd_callout_ttdriver->ttys);
		nd->nd_callout_ttdriver->ttys = NULL;

		put_tty_driver(nd->nd_callout_ttdriver);
		nd->nd_ttdriver_flags &= ~CALLOUT_TTDRV_REG;
	}

	if (nd->nd_ttdriver_flags & XPRINT_TTDRV_REG) {
		tty_unregister_driver(nd->nd_xprint_ttdriver);

		kfree(nd->nd_xprint_ttdriver->ttys);
		nd->nd_xprint_ttdriver->ttys = NULL;

		put_tty_driver(nd->nd_xprint_ttdriver);
		nd->nd_ttdriver_flags &= ~XPRINT_TTDRV_REG;
	}
	for (i = 0; i < CHAN_MAX; i++)
		tty_port_destroy(&nd->nd_chan[i].port);
}



/*
 *     Initialize the TTY portion of the supplied node.
 */
int
dgrp_tty_init(struct nd_struct *nd)
{
	char id[3];
	int  rc;
	int  i;

	ID_TO_CHAR(nd->nd_ID, id);

	/*
	 *  Initialize the TTDRIVER structures.
	 */

	nd->nd_serial_ttdriver = alloc_tty_driver(CHAN_MAX);
	if (!nd->nd_serial_ttdriver)
		return -ENOMEM;

	sprintf(nd->nd_serial_name,  "tty_dgrp_%s_", id);

	nd->nd_serial_ttdriver->owner = THIS_MODULE;
	nd->nd_serial_ttdriver->name = nd->nd_serial_name;
	nd->nd_serial_ttdriver->name_base = 0;
	nd->nd_serial_ttdriver->major = 0;
	nd->nd_serial_ttdriver->minor_start = 0;
	nd->nd_serial_ttdriver->type = TTY_DRIVER_TYPE_SERIAL;
	nd->nd_serial_ttdriver->subtype = SERIAL_TYPE_NORMAL;
	nd->nd_serial_ttdriver->init_termios = DefaultTermios;
	nd->nd_serial_ttdriver->driver_name = "dgrp";
	nd->nd_serial_ttdriver->flags = (TTY_DRIVER_REAL_RAW |
					 TTY_DRIVER_DYNAMIC_DEV |
					 TTY_DRIVER_HARDWARE_BREAK);

	/* The kernel wants space to store pointers to tty_structs. */
	nd->nd_serial_ttdriver->ttys =
		kzalloc(CHAN_MAX * sizeof(struct tty_struct *), GFP_KERNEL);
	if (!nd->nd_serial_ttdriver->ttys)
		return -ENOMEM;

	tty_set_operations(nd->nd_serial_ttdriver, &dgrp_tty_ops);

	if (!(nd->nd_ttdriver_flags & SERIAL_TTDRV_REG)) {
		/*
		 *   Register tty devices
		 */
		rc = tty_register_driver(nd->nd_serial_ttdriver);
		if (rc < 0) {
			/*
			 * If errno is EBUSY, this means there are no more
			 * slots available to have us auto-majored.
			 * (Which is currently supported up to 256)
			 *
			 * We can still request majors above 256,
			 * we just have to do it manually.
			 */
			if (rc == -EBUSY) {
				int i;
				int max_majors = 1U << (32 - MINORBITS);
				for (i = 256; i < max_majors; i++) {
					nd->nd_serial_ttdriver->major = i;
					rc = tty_register_driver(nd->nd_serial_ttdriver);
					if (rc >= 0)
						break;
				}
				/* Really fail now, since we ran out
				 * of majors to try. */
				if (i == max_majors)
					return rc;

			} else {
				return rc;
			}
		}
		nd->nd_ttdriver_flags |= SERIAL_TTDRV_REG;
	}

	nd->nd_callout_ttdriver = alloc_tty_driver(CHAN_MAX);
	if (!nd->nd_callout_ttdriver)
		return -ENOMEM;

	sprintf(nd->nd_callout_name, "cu_dgrp_%s_",  id);

	nd->nd_callout_ttdriver->owner = THIS_MODULE;
	nd->nd_callout_ttdriver->name = nd->nd_callout_name;
	nd->nd_callout_ttdriver->name_base = 0;
	nd->nd_callout_ttdriver->major = nd->nd_serial_ttdriver->major;
	nd->nd_callout_ttdriver->minor_start = 0x40;
	nd->nd_callout_ttdriver->type = TTY_DRIVER_TYPE_SERIAL;
	nd->nd_callout_ttdriver->subtype = SERIAL_TYPE_CALLOUT;
	nd->nd_callout_ttdriver->init_termios = DefaultTermios;
	nd->nd_callout_ttdriver->driver_name = "dgrp";
	nd->nd_callout_ttdriver->flags = (TTY_DRIVER_REAL_RAW |
					  TTY_DRIVER_DYNAMIC_DEV |
					  TTY_DRIVER_HARDWARE_BREAK);

	/* The kernel wants space to store pointers to tty_structs. */
	nd->nd_callout_ttdriver->ttys =
		kzalloc(CHAN_MAX * sizeof(struct tty_struct *), GFP_KERNEL);
	if (!nd->nd_callout_ttdriver->ttys)
		return -ENOMEM;

	tty_set_operations(nd->nd_callout_ttdriver, &dgrp_tty_ops);

	if (dgrp_register_cudevices) {
		if (!(nd->nd_ttdriver_flags & CALLOUT_TTDRV_REG)) {
			/*
			 *   Register cu devices
			 */
			rc = tty_register_driver(nd->nd_callout_ttdriver);
			if (rc < 0)
				return rc;
			nd->nd_ttdriver_flags |= CALLOUT_TTDRV_REG;
		}
	}


	nd->nd_xprint_ttdriver = alloc_tty_driver(CHAN_MAX);
	if (!nd->nd_xprint_ttdriver)
		return -ENOMEM;

	sprintf(nd->nd_xprint_name,  "pr_dgrp_%s_", id);

	nd->nd_xprint_ttdriver->owner = THIS_MODULE;
	nd->nd_xprint_ttdriver->name = nd->nd_xprint_name;
	nd->nd_xprint_ttdriver->name_base = 0;
	nd->nd_xprint_ttdriver->major = nd->nd_serial_ttdriver->major;
	nd->nd_xprint_ttdriver->minor_start = 0x80;
	nd->nd_xprint_ttdriver->type = TTY_DRIVER_TYPE_SERIAL;
	nd->nd_xprint_ttdriver->subtype = SERIAL_TYPE_XPRINT;
	nd->nd_xprint_ttdriver->init_termios = DefaultTermios;
	nd->nd_xprint_ttdriver->driver_name = "dgrp";
	nd->nd_xprint_ttdriver->flags = (TTY_DRIVER_REAL_RAW |
					 TTY_DRIVER_DYNAMIC_DEV |
					 TTY_DRIVER_HARDWARE_BREAK);

	/* The kernel wants space to store pointers to tty_structs. */
	nd->nd_xprint_ttdriver->ttys =
		kzalloc(CHAN_MAX * sizeof(struct tty_struct *), GFP_KERNEL);
	if (!nd->nd_xprint_ttdriver->ttys)
		return -ENOMEM;

	tty_set_operations(nd->nd_xprint_ttdriver, &dgrp_tty_ops);

	if (dgrp_register_prdevices) {
		if (!(nd->nd_ttdriver_flags & XPRINT_TTDRV_REG)) {
			/*
			 *   Register transparent print devices
			 */
			rc = tty_register_driver(nd->nd_xprint_ttdriver);
			if (rc < 0)
				return rc;
			nd->nd_ttdriver_flags |= XPRINT_TTDRV_REG;
		}
	}

	for (i = 0; i < CHAN_MAX; i++) {
		struct ch_struct *ch = nd->nd_chan + i;

		ch->ch_nd = nd;
		ch->ch_digi = digi_init;
		ch->ch_edelay = 100;
		ch->ch_custom_speed = 0;
		ch->ch_portnum = i;
		ch->ch_tun.un_ch = ch;
		ch->ch_pun.un_ch = ch;
		ch->ch_tun.un_type = SERIAL_TYPE_NORMAL;
		ch->ch_pun.un_type = SERIAL_TYPE_XPRINT;

		init_waitqueue_head(&(ch->ch_flag_wait));
		init_waitqueue_head(&(ch->ch_sleep));

		init_waitqueue_head(&(ch->ch_tun.un_open_wait));
		init_waitqueue_head(&(ch->ch_tun.un_close_wait));

		init_waitqueue_head(&(ch->ch_pun.un_open_wait));
		init_waitqueue_head(&(ch->ch_pun.un_close_wait));
		tty_port_init(&ch->port);
	}
	return 0;
}
