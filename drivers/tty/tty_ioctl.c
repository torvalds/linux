// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 * Modified by Fred N. van Kempen, 01/29/93, to add line disciplines
 * which can be dynamically activated and de-activated by the line
 * discipline handling modules (like SLIP).
 */

#include <linux/bits.h>
#include <linux/types.h>
#include <linux/termios.h>
#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/tty.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/compat.h>
#include <linux/termios_internal.h>
#include "tty.h"

#include <asm/io.h>
#include <linux/uaccess.h>

#undef	DEBUG

/*
 * Internal flag options for termios setting behavior
 */
#define TERMIOS_FLUSH	BIT(0)
#define TERMIOS_WAIT	BIT(1)
#define TERMIOS_TERMIO	BIT(2)
#define TERMIOS_OLD	BIT(3)


/**
 *	tty_chars_in_buffer	-	characters pending
 *	@tty: terminal
 *
 *	Return the number of bytes of data in the device private
 *	output queue. If no private method is supplied there is assumed
 *	to be no queue on the device.
 */

unsigned int tty_chars_in_buffer(struct tty_struct *tty)
{
	if (tty->ops->chars_in_buffer)
		return tty->ops->chars_in_buffer(tty);
	return 0;
}
EXPORT_SYMBOL(tty_chars_in_buffer);

/**
 *	tty_write_room		-	write queue space
 *	@tty: terminal
 *
 *	Return the number of bytes that can be queued to this device
 *	at the present time. The result should be treated as a guarantee
 *	and the driver cannot offer a value it later shrinks by more than
 *	the number of bytes written. If no method is provided 2K is always
 *	returned and data may be lost as there will be no flow control.
 */
 
unsigned int tty_write_room(struct tty_struct *tty)
{
	if (tty->ops->write_room)
		return tty->ops->write_room(tty);
	return 2048;
}
EXPORT_SYMBOL(tty_write_room);

/**
 *	tty_driver_flush_buffer	-	discard internal buffer
 *	@tty: terminal
 *
 *	Discard the internal output buffer for this device. If no method
 *	is provided then either the buffer cannot be hardware flushed or
 *	there is no buffer driver side.
 */
void tty_driver_flush_buffer(struct tty_struct *tty)
{
	if (tty->ops->flush_buffer)
		tty->ops->flush_buffer(tty);
}
EXPORT_SYMBOL(tty_driver_flush_buffer);

/**
 *	tty_unthrottle		-	flow control
 *	@tty: terminal
 *
 *	Indicate that a tty may continue transmitting data down the stack.
 *	Takes the termios rwsem to protect against parallel throttle/unthrottle
 *	and also to ensure the driver can consistently reference its own
 *	termios data at this point when implementing software flow control.
 *
 *	Drivers should however remember that the stack can issue a throttle,
 *	then change flow control method, then unthrottle.
 */

void tty_unthrottle(struct tty_struct *tty)
{
	down_write(&tty->termios_rwsem);
	if (test_and_clear_bit(TTY_THROTTLED, &tty->flags) &&
	    tty->ops->unthrottle)
		tty->ops->unthrottle(tty);
	tty->flow_change = 0;
	up_write(&tty->termios_rwsem);
}
EXPORT_SYMBOL(tty_unthrottle);

/**
 *	tty_throttle_safe	-	flow control
 *	@tty: terminal
 *
 *	Indicate that a tty should stop transmitting data down the stack.
 *	tty_throttle_safe will only attempt throttle if tty->flow_change is
 *	TTY_THROTTLE_SAFE. Prevents an accidental throttle due to race
 *	conditions when throttling is conditional on factors evaluated prior to
 *	throttling.
 *
 *	Returns 0 if tty is throttled (or was already throttled)
 */

int tty_throttle_safe(struct tty_struct *tty)
{
	int ret = 0;

	mutex_lock(&tty->throttle_mutex);
	if (!tty_throttled(tty)) {
		if (tty->flow_change != TTY_THROTTLE_SAFE)
			ret = 1;
		else {
			set_bit(TTY_THROTTLED, &tty->flags);
			if (tty->ops->throttle)
				tty->ops->throttle(tty);
		}
	}
	mutex_unlock(&tty->throttle_mutex);

	return ret;
}

/**
 *	tty_unthrottle_safe	-	flow control
 *	@tty: terminal
 *
 *	Similar to tty_unthrottle() but will only attempt unthrottle
 *	if tty->flow_change is TTY_UNTHROTTLE_SAFE. Prevents an accidental
 *	unthrottle due to race conditions when unthrottling is conditional
 *	on factors evaluated prior to unthrottling.
 *
 *	Returns 0 if tty is unthrottled (or was already unthrottled)
 */

int tty_unthrottle_safe(struct tty_struct *tty)
{
	int ret = 0;

	mutex_lock(&tty->throttle_mutex);
	if (tty_throttled(tty)) {
		if (tty->flow_change != TTY_UNTHROTTLE_SAFE)
			ret = 1;
		else {
			clear_bit(TTY_THROTTLED, &tty->flags);
			if (tty->ops->unthrottle)
				tty->ops->unthrottle(tty);
		}
	}
	mutex_unlock(&tty->throttle_mutex);

	return ret;
}

/**
 *	tty_wait_until_sent	-	wait for I/O to finish
 *	@tty: tty we are waiting for
 *	@timeout: how long we will wait
 *
 *	Wait for characters pending in a tty driver to hit the wire, or
 *	for a timeout to occur (eg due to flow control)
 *
 *	Locking: none
 */

void tty_wait_until_sent(struct tty_struct *tty, long timeout)
{
	if (!timeout)
		timeout = MAX_SCHEDULE_TIMEOUT;

	timeout = wait_event_interruptible_timeout(tty->write_wait,
			!tty_chars_in_buffer(tty), timeout);
	if (timeout <= 0)
		return;

	if (timeout == MAX_SCHEDULE_TIMEOUT)
		timeout = 0;

	if (tty->ops->wait_until_sent)
		tty->ops->wait_until_sent(tty, timeout);
}
EXPORT_SYMBOL(tty_wait_until_sent);


/*
 *		Termios Helper Methods
 */

static void unset_locked_termios(struct tty_struct *tty, const struct ktermios *old)
{
	struct ktermios *termios = &tty->termios;
	struct ktermios *locked  = &tty->termios_locked;
	int	i;

#define NOSET_MASK(x, y, z) (x = ((x) & ~(z)) | ((y) & (z)))

	NOSET_MASK(termios->c_iflag, old->c_iflag, locked->c_iflag);
	NOSET_MASK(termios->c_oflag, old->c_oflag, locked->c_oflag);
	NOSET_MASK(termios->c_cflag, old->c_cflag, locked->c_cflag);
	NOSET_MASK(termios->c_lflag, old->c_lflag, locked->c_lflag);
	termios->c_line = locked->c_line ? old->c_line : termios->c_line;
	for (i = 0; i < NCCS; i++)
		termios->c_cc[i] = locked->c_cc[i] ?
			old->c_cc[i] : termios->c_cc[i];
	/* FIXME: What should we do for i/ospeed */
}

/**
 *	tty_termios_copy_hw	-	copy hardware settings
 *	@new: New termios
 *	@old: Old termios
 *
 *	Propagate the hardware specific terminal setting bits from
 *	the old termios structure to the new one. This is used in cases
 *	where the hardware does not support reconfiguration or as a helper
 *	in some cases where only minimal reconfiguration is supported
 */

void tty_termios_copy_hw(struct ktermios *new, const struct ktermios *old)
{
	/* The bits a dumb device handles in software. Smart devices need
	   to always provide a set_termios method */
	new->c_cflag &= HUPCL | CREAD | CLOCAL;
	new->c_cflag |= old->c_cflag & ~(HUPCL | CREAD | CLOCAL);
	new->c_ispeed = old->c_ispeed;
	new->c_ospeed = old->c_ospeed;
}
EXPORT_SYMBOL(tty_termios_copy_hw);

/**
 *	tty_termios_hw_change	-	check for setting change
 *	@a: termios
 *	@b: termios to compare
 *
 *	Check if any of the bits that affect a dumb device have changed
 *	between the two termios structures, or a speed change is needed.
 */

bool tty_termios_hw_change(const struct ktermios *a, const struct ktermios *b)
{
	if (a->c_ispeed != b->c_ispeed || a->c_ospeed != b->c_ospeed)
		return true;
	if ((a->c_cflag ^ b->c_cflag) & ~(HUPCL | CREAD | CLOCAL))
		return true;
	return false;
}
EXPORT_SYMBOL(tty_termios_hw_change);

/**
 *	tty_get_char_size	-	get size of a character
 *	@cflag: termios cflag value
 *
 *	Get the size (in bits) of a character depending on @cflag's %CSIZE
 *	setting.
 */
unsigned char tty_get_char_size(unsigned int cflag)
{
	switch (cflag & CSIZE) {
	case CS5:
		return 5;
	case CS6:
		return 6;
	case CS7:
		return 7;
	case CS8:
	default:
		return 8;
	}
}
EXPORT_SYMBOL_GPL(tty_get_char_size);

/**
 *	tty_get_frame_size	-	get size of a frame
 *	@cflag: termios cflag value
 *
 *	Get the size (in bits) of a frame depending on @cflag's %CSIZE, %CSTOPB,
 *	and %PARENB setting. The result is a sum of character size, start and
 *	stop bits -- one bit each -- second stop bit (if set), and parity bit
 *	(if set).
 */
unsigned char tty_get_frame_size(unsigned int cflag)
{
	unsigned char bits = 2 + tty_get_char_size(cflag);

	if (cflag & CSTOPB)
		bits++;
	if (cflag & PARENB)
		bits++;
	if (cflag & ADDRB)
		bits++;

	return bits;
}
EXPORT_SYMBOL_GPL(tty_get_frame_size);

/**
 *	tty_set_termios		-	update termios values
 *	@tty: tty to update
 *	@new_termios: desired new value
 *
 *	Perform updates to the termios values set on this terminal.
 *	A master pty's termios should never be set.
 *
 *	Locking: termios_rwsem
 */

int tty_set_termios(struct tty_struct *tty, struct ktermios *new_termios)
{
	struct ktermios old_termios;
	struct tty_ldisc *ld;

	WARN_ON(tty->driver->type == TTY_DRIVER_TYPE_PTY &&
		tty->driver->subtype == PTY_TYPE_MASTER);
	/*
	 *	Perform the actual termios internal changes under lock.
	 */


	/* FIXME: we need to decide on some locking/ordering semantics
	   for the set_termios notification eventually */
	down_write(&tty->termios_rwsem);
	old_termios = tty->termios;
	tty->termios = *new_termios;
	unset_locked_termios(tty, &old_termios);
	/* Reset any ADDRB changes, ADDRB is changed through ->rs485_config() */
	tty->termios.c_cflag ^= (tty->termios.c_cflag ^ old_termios.c_cflag) & ADDRB;

	if (tty->ops->set_termios)
		tty->ops->set_termios(tty, &old_termios);
	else
		tty_termios_copy_hw(&tty->termios, &old_termios);

	ld = tty_ldisc_ref(tty);
	if (ld != NULL) {
		if (ld->ops->set_termios)
			ld->ops->set_termios(tty, &old_termios);
		tty_ldisc_deref(ld);
	}
	up_write(&tty->termios_rwsem);
	return 0;
}
EXPORT_SYMBOL_GPL(tty_set_termios);


/*
 * Translate a "termio" structure into a "termios". Ugh.
 */
__weak int user_termio_to_kernel_termios(struct ktermios *termios,
						struct termio __user *termio)
{
	struct termio v;

	if (copy_from_user(&v, termio, sizeof(struct termio)))
		return -EFAULT;

	termios->c_iflag = (0xffff0000 & termios->c_iflag) | v.c_iflag;
	termios->c_oflag = (0xffff0000 & termios->c_oflag) | v.c_oflag;
	termios->c_cflag = (0xffff0000 & termios->c_cflag) | v.c_cflag;
	termios->c_lflag = (0xffff0000 & termios->c_lflag) | v.c_lflag;
	termios->c_line = (0xffff0000 & termios->c_lflag) | v.c_line;
	memcpy(termios->c_cc, v.c_cc, NCC);
	return 0;
}

/*
 * Translate a "termios" structure into a "termio". Ugh.
 */
__weak int kernel_termios_to_user_termio(struct termio __user *termio,
						struct ktermios *termios)
{
	struct termio v;
	memset(&v, 0, sizeof(struct termio));
	v.c_iflag = termios->c_iflag;
	v.c_oflag = termios->c_oflag;
	v.c_cflag = termios->c_cflag;
	v.c_lflag = termios->c_lflag;
	v.c_line = termios->c_line;
	memcpy(v.c_cc, termios->c_cc, NCC);
	return copy_to_user(termio, &v, sizeof(struct termio));
}

#ifdef TCGETS2
__weak int user_termios_to_kernel_termios(struct ktermios *k,
						 struct termios2 __user *u)
{
	return copy_from_user(k, u, sizeof(struct termios2));
}
__weak int kernel_termios_to_user_termios(struct termios2 __user *u,
						 struct ktermios *k)
{
	return copy_to_user(u, k, sizeof(struct termios2));
}
__weak int user_termios_to_kernel_termios_1(struct ktermios *k,
						   struct termios __user *u)
{
	return copy_from_user(k, u, sizeof(struct termios));
}
__weak int kernel_termios_to_user_termios_1(struct termios __user *u,
						   struct ktermios *k)
{
	return copy_to_user(u, k, sizeof(struct termios));
}

#else

__weak int user_termios_to_kernel_termios(struct ktermios *k,
						 struct termios __user *u)
{
	return copy_from_user(k, u, sizeof(struct termios));
}
__weak int kernel_termios_to_user_termios(struct termios __user *u,
						 struct ktermios *k)
{
	return copy_to_user(u, k, sizeof(struct termios));
}
#endif /* TCGETS2 */

/**
 *	set_termios		-	set termios values for a tty
 *	@tty: terminal device
 *	@arg: user data
 *	@opt: option information
 *
 *	Helper function to prepare termios data and run necessary other
 *	functions before using tty_set_termios to do the actual changes.
 *
 *	Locking:
 *		Called functions take ldisc and termios_rwsem locks
 */

static int set_termios(struct tty_struct *tty, void __user *arg, int opt)
{
	struct ktermios tmp_termios;
	struct tty_ldisc *ld;
	int retval = tty_check_change(tty);

	if (retval)
		return retval;

	down_read(&tty->termios_rwsem);
	tmp_termios = tty->termios;
	up_read(&tty->termios_rwsem);

	if (opt & TERMIOS_TERMIO) {
		if (user_termio_to_kernel_termios(&tmp_termios,
						(struct termio __user *)arg))
			return -EFAULT;
#ifdef TCGETS2
	} else if (opt & TERMIOS_OLD) {
		if (user_termios_to_kernel_termios_1(&tmp_termios,
						(struct termios __user *)arg))
			return -EFAULT;
	} else {
		if (user_termios_to_kernel_termios(&tmp_termios,
						(struct termios2 __user *)arg))
			return -EFAULT;
	}
#else
	} else if (user_termios_to_kernel_termios(&tmp_termios,
					(struct termios __user *)arg))
		return -EFAULT;
#endif

	/* If old style Bfoo values are used then load c_ispeed/c_ospeed
	 * with the real speed so its unconditionally usable */
	tmp_termios.c_ispeed = tty_termios_input_baud_rate(&tmp_termios);
	tmp_termios.c_ospeed = tty_termios_baud_rate(&tmp_termios);

	if (opt & (TERMIOS_FLUSH|TERMIOS_WAIT)) {
retry_write_wait:
		retval = wait_event_interruptible(tty->write_wait, !tty_chars_in_buffer(tty));
		if (retval < 0)
			return retval;

		if (tty_write_lock(tty, false) < 0)
			goto retry_write_wait;

		/* Racing writer? */
		if (tty_chars_in_buffer(tty)) {
			tty_write_unlock(tty);
			goto retry_write_wait;
		}

		ld = tty_ldisc_ref(tty);
		if (ld != NULL) {
			if ((opt & TERMIOS_FLUSH) && ld->ops->flush_buffer)
				ld->ops->flush_buffer(tty);
			tty_ldisc_deref(ld);
		}

		if ((opt & TERMIOS_WAIT) && tty->ops->wait_until_sent) {
			tty->ops->wait_until_sent(tty, 0);
			if (signal_pending(current)) {
				tty_write_unlock(tty);
				return -ERESTARTSYS;
			}
		}

		tty_set_termios(tty, &tmp_termios);

		tty_write_unlock(tty);
	} else {
		tty_set_termios(tty, &tmp_termios);
	}

	/* FIXME: Arguably if tmp_termios == tty->termios AND the
	   actual requested termios was not tmp_termios then we may
	   want to return an error as no user requested change has
	   succeeded */
	return 0;
}

static void copy_termios(struct tty_struct *tty, struct ktermios *kterm)
{
	down_read(&tty->termios_rwsem);
	*kterm = tty->termios;
	up_read(&tty->termios_rwsem);
}

static void copy_termios_locked(struct tty_struct *tty, struct ktermios *kterm)
{
	down_read(&tty->termios_rwsem);
	*kterm = tty->termios_locked;
	up_read(&tty->termios_rwsem);
}

static int get_termio(struct tty_struct *tty, struct termio __user *termio)
{
	struct ktermios kterm;
	copy_termios(tty, &kterm);
	if (kernel_termios_to_user_termio(termio, &kterm))
		return -EFAULT;
	return 0;
}

#ifdef TIOCGETP
/*
 * These are deprecated, but there is limited support..
 *
 * The "sg_flags" translation is a joke..
 */
static int get_sgflags(struct tty_struct *tty)
{
	int flags = 0;

	if (!L_ICANON(tty)) {
		if (L_ISIG(tty))
			flags |= 0x02;		/* cbreak */
		else
			flags |= 0x20;		/* raw */
	}
	if (L_ECHO(tty))
		flags |= 0x08;			/* echo */
	if (O_OPOST(tty))
		if (O_ONLCR(tty))
			flags |= 0x10;		/* crmod */
	return flags;
}

static int get_sgttyb(struct tty_struct *tty, struct sgttyb __user *sgttyb)
{
	struct sgttyb tmp;

	down_read(&tty->termios_rwsem);
	tmp.sg_ispeed = tty->termios.c_ispeed;
	tmp.sg_ospeed = tty->termios.c_ospeed;
	tmp.sg_erase = tty->termios.c_cc[VERASE];
	tmp.sg_kill = tty->termios.c_cc[VKILL];
	tmp.sg_flags = get_sgflags(tty);
	up_read(&tty->termios_rwsem);

	return copy_to_user(sgttyb, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

static void set_sgflags(struct ktermios *termios, int flags)
{
	termios->c_iflag = ICRNL | IXON;
	termios->c_oflag = 0;
	termios->c_lflag = ISIG | ICANON;
	if (flags & 0x02) {	/* cbreak */
		termios->c_iflag = 0;
		termios->c_lflag &= ~ICANON;
	}
	if (flags & 0x08) {		/* echo */
		termios->c_lflag |= ECHO | ECHOE | ECHOK |
				    ECHOCTL | ECHOKE | IEXTEN;
	}
	if (flags & 0x10) {		/* crmod */
		termios->c_oflag |= OPOST | ONLCR;
	}
	if (flags & 0x20) {	/* raw */
		termios->c_iflag = 0;
		termios->c_lflag &= ~(ISIG | ICANON);
	}
	if (!(termios->c_lflag & ICANON)) {
		termios->c_cc[VMIN] = 1;
		termios->c_cc[VTIME] = 0;
	}
}

/**
 *	set_sgttyb		-	set legacy terminal values
 *	@tty: tty structure
 *	@sgttyb: pointer to old style terminal structure
 *
 *	Updates a terminal from the legacy BSD style terminal information
 *	structure.
 *
 *	Locking: termios_rwsem
 */

static int set_sgttyb(struct tty_struct *tty, struct sgttyb __user *sgttyb)
{
	int retval;
	struct sgttyb tmp;
	struct ktermios termios;

	retval = tty_check_change(tty);
	if (retval)
		return retval;

	if (copy_from_user(&tmp, sgttyb, sizeof(tmp)))
		return -EFAULT;

	down_write(&tty->termios_rwsem);
	termios = tty->termios;
	termios.c_cc[VERASE] = tmp.sg_erase;
	termios.c_cc[VKILL] = tmp.sg_kill;
	set_sgflags(&termios, tmp.sg_flags);
	/* Try and encode into Bfoo format */
	tty_termios_encode_baud_rate(&termios, termios.c_ispeed,
						termios.c_ospeed);
	up_write(&tty->termios_rwsem);
	tty_set_termios(tty, &termios);
	return 0;
}
#endif

#ifdef TIOCGETC
static int get_tchars(struct tty_struct *tty, struct tchars __user *tchars)
{
	struct tchars tmp;

	down_read(&tty->termios_rwsem);
	tmp.t_intrc = tty->termios.c_cc[VINTR];
	tmp.t_quitc = tty->termios.c_cc[VQUIT];
	tmp.t_startc = tty->termios.c_cc[VSTART];
	tmp.t_stopc = tty->termios.c_cc[VSTOP];
	tmp.t_eofc = tty->termios.c_cc[VEOF];
	tmp.t_brkc = tty->termios.c_cc[VEOL2];	/* what is brkc anyway? */
	up_read(&tty->termios_rwsem);
	return copy_to_user(tchars, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

static int set_tchars(struct tty_struct *tty, struct tchars __user *tchars)
{
	struct tchars tmp;

	if (copy_from_user(&tmp, tchars, sizeof(tmp)))
		return -EFAULT;
	down_write(&tty->termios_rwsem);
	tty->termios.c_cc[VINTR] = tmp.t_intrc;
	tty->termios.c_cc[VQUIT] = tmp.t_quitc;
	tty->termios.c_cc[VSTART] = tmp.t_startc;
	tty->termios.c_cc[VSTOP] = tmp.t_stopc;
	tty->termios.c_cc[VEOF] = tmp.t_eofc;
	tty->termios.c_cc[VEOL2] = tmp.t_brkc;	/* what is brkc anyway? */
	up_write(&tty->termios_rwsem);
	return 0;
}
#endif

#ifdef TIOCGLTC
static int get_ltchars(struct tty_struct *tty, struct ltchars __user *ltchars)
{
	struct ltchars tmp;

	down_read(&tty->termios_rwsem);
	tmp.t_suspc = tty->termios.c_cc[VSUSP];
	/* what is dsuspc anyway? */
	tmp.t_dsuspc = tty->termios.c_cc[VSUSP];
	tmp.t_rprntc = tty->termios.c_cc[VREPRINT];
	/* what is flushc anyway? */
	tmp.t_flushc = tty->termios.c_cc[VEOL2];
	tmp.t_werasc = tty->termios.c_cc[VWERASE];
	tmp.t_lnextc = tty->termios.c_cc[VLNEXT];
	up_read(&tty->termios_rwsem);
	return copy_to_user(ltchars, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

static int set_ltchars(struct tty_struct *tty, struct ltchars __user *ltchars)
{
	struct ltchars tmp;

	if (copy_from_user(&tmp, ltchars, sizeof(tmp)))
		return -EFAULT;

	down_write(&tty->termios_rwsem);
	tty->termios.c_cc[VSUSP] = tmp.t_suspc;
	/* what is dsuspc anyway? */
	tty->termios.c_cc[VEOL2] = tmp.t_dsuspc;
	tty->termios.c_cc[VREPRINT] = tmp.t_rprntc;
	/* what is flushc anyway? */
	tty->termios.c_cc[VEOL2] = tmp.t_flushc;
	tty->termios.c_cc[VWERASE] = tmp.t_werasc;
	tty->termios.c_cc[VLNEXT] = tmp.t_lnextc;
	up_write(&tty->termios_rwsem);
	return 0;
}
#endif

/**
 *	tty_change_softcar	-	carrier change ioctl helper
 *	@tty: tty to update
 *	@enable: enable/disable CLOCAL
 *
 *	Perform a change to the CLOCAL state and call into the driver
 *	layer to make it visible. All done with the termios rwsem
 */

static int tty_change_softcar(struct tty_struct *tty, bool enable)
{
	int ret = 0;
	struct ktermios old;
	tcflag_t bit = enable ? CLOCAL : 0;

	down_write(&tty->termios_rwsem);
	old = tty->termios;
	tty->termios.c_cflag &= ~CLOCAL;
	tty->termios.c_cflag |= bit;
	if (tty->ops->set_termios)
		tty->ops->set_termios(tty, &old);
	if (C_CLOCAL(tty) != bit)
		ret = -EINVAL;
	up_write(&tty->termios_rwsem);
	return ret;
}

/**
 *	tty_mode_ioctl		-	mode related ioctls
 *	@tty: tty for the ioctl
 *	@cmd: command
 *	@arg: ioctl argument
 *
 *	Perform non line discipline specific mode control ioctls. This
 *	is designed to be called by line disciplines to ensure they provide
 *	consistent mode setting.
 */

int tty_mode_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	struct tty_struct *real_tty;
	void __user *p = (void __user *)arg;
	int ret = 0;
	struct ktermios kterm;

	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
		real_tty = tty->link;
	else
		real_tty = tty;

	switch (cmd) {
#ifdef TIOCGETP
	case TIOCGETP:
		return get_sgttyb(real_tty, (struct sgttyb __user *) arg);
	case TIOCSETP:
	case TIOCSETN:
		return set_sgttyb(real_tty, (struct sgttyb __user *) arg);
#endif
#ifdef TIOCGETC
	case TIOCGETC:
		return get_tchars(real_tty, p);
	case TIOCSETC:
		return set_tchars(real_tty, p);
#endif
#ifdef TIOCGLTC
	case TIOCGLTC:
		return get_ltchars(real_tty, p);
	case TIOCSLTC:
		return set_ltchars(real_tty, p);
#endif
	case TCSETSF:
		return set_termios(real_tty, p,  TERMIOS_FLUSH | TERMIOS_WAIT | TERMIOS_OLD);
	case TCSETSW:
		return set_termios(real_tty, p, TERMIOS_WAIT | TERMIOS_OLD);
	case TCSETS:
		return set_termios(real_tty, p, TERMIOS_OLD);
#ifndef TCGETS2
	case TCGETS:
		copy_termios(real_tty, &kterm);
		if (kernel_termios_to_user_termios((struct termios __user *)arg, &kterm))
			ret = -EFAULT;
		return ret;
#else
	case TCGETS:
		copy_termios(real_tty, &kterm);
		if (kernel_termios_to_user_termios_1((struct termios __user *)arg, &kterm))
			ret = -EFAULT;
		return ret;
	case TCGETS2:
		copy_termios(real_tty, &kterm);
		if (kernel_termios_to_user_termios((struct termios2 __user *)arg, &kterm))
			ret = -EFAULT;
		return ret;
	case TCSETSF2:
		return set_termios(real_tty, p,  TERMIOS_FLUSH | TERMIOS_WAIT);
	case TCSETSW2:
		return set_termios(real_tty, p, TERMIOS_WAIT);
	case TCSETS2:
		return set_termios(real_tty, p, 0);
#endif
	case TCGETA:
		return get_termio(real_tty, p);
	case TCSETAF:
		return set_termios(real_tty, p, TERMIOS_FLUSH | TERMIOS_WAIT | TERMIOS_TERMIO);
	case TCSETAW:
		return set_termios(real_tty, p, TERMIOS_WAIT | TERMIOS_TERMIO);
	case TCSETA:
		return set_termios(real_tty, p, TERMIOS_TERMIO);
#ifndef TCGETS2
	case TIOCGLCKTRMIOS:
		copy_termios_locked(real_tty, &kterm);
		if (kernel_termios_to_user_termios((struct termios __user *)arg, &kterm))
			ret = -EFAULT;
		return ret;
	case TIOCSLCKTRMIOS:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		copy_termios_locked(real_tty, &kterm);
		if (user_termios_to_kernel_termios(&kterm,
					       (struct termios __user *) arg))
			return -EFAULT;
		down_write(&real_tty->termios_rwsem);
		real_tty->termios_locked = kterm;
		up_write(&real_tty->termios_rwsem);
		return 0;
#else
	case TIOCGLCKTRMIOS:
		copy_termios_locked(real_tty, &kterm);
		if (kernel_termios_to_user_termios_1((struct termios __user *)arg, &kterm))
			ret = -EFAULT;
		return ret;
	case TIOCSLCKTRMIOS:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		copy_termios_locked(real_tty, &kterm);
		if (user_termios_to_kernel_termios_1(&kterm,
					       (struct termios __user *) arg))
			return -EFAULT;
		down_write(&real_tty->termios_rwsem);
		real_tty->termios_locked = kterm;
		up_write(&real_tty->termios_rwsem);
		return ret;
#endif
#ifdef TCGETX
	case TCGETX:
	case TCSETX:
	case TCSETXW:
	case TCSETXF:
		return -ENOTTY;
#endif
	case TIOCGSOFTCAR:
		copy_termios(real_tty, &kterm);
		ret = put_user((kterm.c_cflag & CLOCAL) ? 1 : 0,
						(int __user *)arg);
		return ret;
	case TIOCSSOFTCAR:
		if (get_user(arg, (unsigned int __user *) arg))
			return -EFAULT;
		return tty_change_softcar(real_tty, arg);
	default:
		return -ENOIOCTLCMD;
	}
}
EXPORT_SYMBOL_GPL(tty_mode_ioctl);


/* Caller guarantees ldisc reference is held */
static int __tty_perform_flush(struct tty_struct *tty, unsigned long arg)
{
	struct tty_ldisc *ld = tty->ldisc;

	switch (arg) {
	case TCIFLUSH:
		if (ld && ld->ops->flush_buffer) {
			ld->ops->flush_buffer(tty);
			tty_unthrottle(tty);
		}
		break;
	case TCIOFLUSH:
		if (ld && ld->ops->flush_buffer) {
			ld->ops->flush_buffer(tty);
			tty_unthrottle(tty);
		}
		fallthrough;
	case TCOFLUSH:
		tty_driver_flush_buffer(tty);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int tty_perform_flush(struct tty_struct *tty, unsigned long arg)
{
	struct tty_ldisc *ld;
	int retval = tty_check_change(tty);
	if (retval)
		return retval;

	ld = tty_ldisc_ref_wait(tty);
	retval = __tty_perform_flush(tty, arg);
	if (ld)
		tty_ldisc_deref(ld);
	return retval;
}
EXPORT_SYMBOL_GPL(tty_perform_flush);

int n_tty_ioctl_helper(struct tty_struct *tty, unsigned int cmd,
		unsigned long arg)
{
	int retval;

	switch (cmd) {
	case TCXONC:
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		switch (arg) {
		case TCOOFF:
			spin_lock_irq(&tty->flow.lock);
			if (!tty->flow.tco_stopped) {
				tty->flow.tco_stopped = true;
				__stop_tty(tty);
			}
			spin_unlock_irq(&tty->flow.lock);
			break;
		case TCOON:
			spin_lock_irq(&tty->flow.lock);
			if (tty->flow.tco_stopped) {
				tty->flow.tco_stopped = false;
				__start_tty(tty);
			}
			spin_unlock_irq(&tty->flow.lock);
			break;
		case TCIOFF:
			if (STOP_CHAR(tty) != __DISABLED_CHAR)
				retval = tty_send_xchar(tty, STOP_CHAR(tty));
			break;
		case TCION:
			if (START_CHAR(tty) != __DISABLED_CHAR)
				retval = tty_send_xchar(tty, START_CHAR(tty));
			break;
		default:
			return -EINVAL;
		}
		return retval;
	case TCFLSH:
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		return __tty_perform_flush(tty, arg);
	default:
		/* Try the mode commands */
		return tty_mode_ioctl(tty, cmd, arg);
	}
}
EXPORT_SYMBOL(n_tty_ioctl_helper);
