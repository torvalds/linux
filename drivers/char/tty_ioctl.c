/*
 *  linux/drivers/char/tty_ioctl.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 * Modified by Fred N. van Kempen, 01/29/93, to add line disciplines
 * which can be dynamically activated and de-activated by the line
 * discipline handling modules (like SLIP).
 */

#include <linux/types.h>
#include <linux/termios.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/tty.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/mutex.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#undef TTY_DEBUG_WAIT_UNTIL_SENT

#undef	DEBUG

/*
 * Internal flag options for termios setting behavior
 */
#define TERMIOS_FLUSH	1
#define TERMIOS_WAIT	2
#define TERMIOS_TERMIO	4
#define TERMIOS_OLD	8


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

void tty_wait_until_sent(struct tty_struct * tty, long timeout)
{
	DECLARE_WAITQUEUE(wait, current);

#ifdef TTY_DEBUG_WAIT_UNTIL_SENT
	char buf[64];
	
	printk(KERN_DEBUG "%s wait until sent...\n", tty_name(tty, buf));
#endif
	if (!tty->driver->chars_in_buffer)
		return;
	add_wait_queue(&tty->write_wait, &wait);
	if (!timeout)
		timeout = MAX_SCHEDULE_TIMEOUT;
	do {
#ifdef TTY_DEBUG_WAIT_UNTIL_SENT
		printk(KERN_DEBUG "waiting %s...(%d)\n", tty_name(tty, buf),
		       tty->driver->chars_in_buffer(tty));
#endif
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current))
			goto stop_waiting;
		if (!tty->driver->chars_in_buffer(tty))
			break;
		timeout = schedule_timeout(timeout);
	} while (timeout);
	if (tty->driver->wait_until_sent)
		tty->driver->wait_until_sent(tty, timeout);
stop_waiting:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&tty->write_wait, &wait);
}

EXPORT_SYMBOL(tty_wait_until_sent);

static void unset_locked_termios(struct ktermios *termios,
				 struct ktermios *old,
				 struct ktermios *locked)
{
	int	i;
	
#define NOSET_MASK(x,y,z) (x = ((x) & ~(z)) | ((y) & (z)))

	if (!locked) {
		printk(KERN_WARNING "Warning?!? termios_locked is NULL.\n");
		return;
	}

	NOSET_MASK(termios->c_iflag, old->c_iflag, locked->c_iflag);
	NOSET_MASK(termios->c_oflag, old->c_oflag, locked->c_oflag);
	NOSET_MASK(termios->c_cflag, old->c_cflag, locked->c_cflag);
	NOSET_MASK(termios->c_lflag, old->c_lflag, locked->c_lflag);
	termios->c_line = locked->c_line ? old->c_line : termios->c_line;
	for (i=0; i < NCCS; i++)
		termios->c_cc[i] = locked->c_cc[i] ?
			old->c_cc[i] : termios->c_cc[i];
	/* FIXME: What should we do for i/ospeed */
}

/*
 * Routine which returns the baud rate of the tty
 *
 * Note that the baud_table needs to be kept in sync with the
 * include/asm/termbits.h file.
 */
static const speed_t baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 460800,
#ifdef __sparc__
	76800, 153600, 307200, 614400, 921600
#else
	500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
	2500000, 3000000, 3500000, 4000000
#endif
};

#ifndef __sparc__
static const tcflag_t baud_bits[] = {
	B0, B50, B75, B110, B134, B150, B200, B300, B600,
	B1200, B1800, B2400, B4800, B9600, B19200, B38400,
	B57600, B115200, B230400, B460800, B500000, B576000,
	B921600, B1000000, B1152000, B1500000, B2000000, B2500000,
	B3000000, B3500000, B4000000
};
#else
static const tcflag_t baud_bits[] = {
	B0, B50, B75, B110, B134, B150, B200, B300, B600,
	B1200, B1800, B2400, B4800, B9600, B19200, B38400,
	B57600, B115200, B230400, B460800, B76800, B153600,
	B307200, B614400, B921600
};
#endif

static int n_baud_table = ARRAY_SIZE(baud_table);

/**
 *	tty_termios_baud_rate
 *	@termios: termios structure
 *
 *	Convert termios baud rate data into a speed. This should be called
 *	with the termios lock held if this termios is a terminal termios
 *	structure. May change the termios data. Device drivers can call this
 *	function but should use ->c_[io]speed directly as they are updated.
 *
 *	Locking: none
 */

speed_t tty_termios_baud_rate(struct ktermios *termios)
{
	unsigned int cbaud;

	cbaud = termios->c_cflag & CBAUD;

#ifdef BOTHER
	/* Magic token for arbitary speed via c_ispeed/c_ospeed */
	if (cbaud == BOTHER)
		return termios->c_ospeed;
#endif
	if (cbaud & CBAUDEX) {
		cbaud &= ~CBAUDEX;

		if (cbaud < 1 || cbaud + 15 > n_baud_table)
			termios->c_cflag &= ~CBAUDEX;
		else
			cbaud += 15;
	}
	return baud_table[cbaud];
}

EXPORT_SYMBOL(tty_termios_baud_rate);

/**
 *	tty_termios_input_baud_rate
 *	@termios: termios structure
 *
 *	Convert termios baud rate data into a speed. This should be called
 *	with the termios lock held if this termios is a terminal termios
 *	structure. May change the termios data. Device drivers can call this
 *	function but should use ->c_[io]speed directly as they are updated.
 *
 *	Locking: none
 */

speed_t tty_termios_input_baud_rate(struct ktermios *termios)
{
#ifdef IBSHIFT
	unsigned int cbaud = (termios->c_cflag >> IBSHIFT) & CBAUD;

	if (cbaud == B0)
		return tty_termios_baud_rate(termios);

	/* Magic token for arbitary speed via c_ispeed*/
	if (cbaud == BOTHER)
		return termios->c_ispeed;

	if (cbaud & CBAUDEX) {
		cbaud &= ~CBAUDEX;

		if (cbaud < 1 || cbaud + 15 > n_baud_table)
			termios->c_cflag &= ~(CBAUDEX << IBSHIFT);
		else
			cbaud += 15;
	}
	return baud_table[cbaud];
#else
	return tty_termios_baud_rate(termios);
#endif
}

EXPORT_SYMBOL(tty_termios_input_baud_rate);

#ifdef BOTHER

/**
 *	tty_termios_encode_baud_rate
 *	@termios: termios structure
 *	@ispeed: input speed
 *	@ospeed: output speed
 *
 *	Encode the speeds set into the passed termios structure. This is
 *	used as a library helper for drivers os that they can report back
 *	the actual speed selected when it differs from the speed requested
 *
 *	For now input and output speed must agree.
 *
 *	Locking: Caller should hold termios lock. This is already held
 *	when calling this function from the driver termios handler.
 */

void tty_termios_encode_baud_rate(struct ktermios *termios, speed_t ibaud, speed_t obaud)
{
	int i = 0;
	int ifound = 0, ofound = 0;

	termios->c_ispeed = ibaud;
	termios->c_ospeed = obaud;

	termios->c_cflag &= ~CBAUD;
	/* Identical speed means no input encoding (ie B0 << IBSHIFT)*/
	if (termios->c_ispeed == termios->c_ospeed)
		ifound = 1;

	do {
		if (obaud == baud_table[i]) {
			termios->c_cflag |= baud_bits[i];
			ofound = 1;
			/* So that if ibaud == obaud we don't set it */
			continue;
		}
		if (ibaud == baud_table[i]) {
			termios->c_cflag |= (baud_bits[i] << IBSHIFT);
			ifound = 1;
		}
	}
	while(++i < n_baud_table);
	if (!ofound)
		termios->c_cflag |= BOTHER;
	if (!ifound)
		termios->c_cflag |= (BOTHER << IBSHIFT);
}

EXPORT_SYMBOL_GPL(tty_termios_encode_baud_rate);

#endif

/**
 *	tty_get_baud_rate	-	get tty bit rates
 *	@tty: tty to query
 *
 *	Returns the baud rate as an integer for this terminal. The
 *	termios lock must be held by the caller and the terminal bit
 *	flags may be updated.
 *
 *	Locking: none
 */

speed_t tty_get_baud_rate(struct tty_struct *tty)
{
	speed_t baud = tty_termios_baud_rate(tty->termios);

	if (baud == 38400 && tty->alt_speed) {
		if (!tty->warned) {
			printk(KERN_WARNING "Use of setserial/setrocket to "
					    "set SPD_* flags is deprecated\n");
			tty->warned = 1;
		}
		baud = tty->alt_speed;
	}

	return baud;
}

EXPORT_SYMBOL(tty_get_baud_rate);

/**
 *	change_termios		-	update termios values
 *	@tty: tty to update
 *	@new_termios: desired new value
 *
 *	Perform updates to the termios values set on this terminal. There
 *	is a bit of layering violation here with n_tty in terms of the
 *	internal knowledge of this function.
 *
 *	Locking: termios_sem
 */

static void change_termios(struct tty_struct * tty, struct ktermios * new_termios)
{
	int canon_change;
	struct ktermios old_termios = *tty->termios;
	struct tty_ldisc *ld;
	
	/*
	 *	Perform the actual termios internal changes under lock.
	 */
	 

	/* FIXME: we need to decide on some locking/ordering semantics
	   for the set_termios notification eventually */
	mutex_lock(&tty->termios_mutex);

	*tty->termios = *new_termios;
	unset_locked_termios(tty->termios, &old_termios, tty->termios_locked);
	canon_change = (old_termios.c_lflag ^ tty->termios->c_lflag) & ICANON;
	if (canon_change) {
		memset(&tty->read_flags, 0, sizeof tty->read_flags);
		tty->canon_head = tty->read_tail;
		tty->canon_data = 0;
		tty->erasing = 0;
	}
	
	
	if (canon_change && !L_ICANON(tty) && tty->read_cnt)
		/* Get characters left over from canonical mode. */
		wake_up_interruptible(&tty->read_wait);

	/* See if packet mode change of state. */

	if (tty->link && tty->link->packet) {
		int old_flow = ((old_termios.c_iflag & IXON) &&
				(old_termios.c_cc[VSTOP] == '\023') &&
				(old_termios.c_cc[VSTART] == '\021'));
		int new_flow = (I_IXON(tty) &&
				STOP_CHAR(tty) == '\023' &&
				START_CHAR(tty) == '\021');
		if (old_flow != new_flow) {
			tty->ctrl_status &= ~(TIOCPKT_DOSTOP | TIOCPKT_NOSTOP);
			if (new_flow)
				tty->ctrl_status |= TIOCPKT_DOSTOP;
			else
				tty->ctrl_status |= TIOCPKT_NOSTOP;
			wake_up_interruptible(&tty->link->read_wait);
		}
	}
	   
	if (tty->driver->set_termios)
		(*tty->driver->set_termios)(tty, &old_termios);

	ld = tty_ldisc_ref(tty);
	if (ld != NULL) {
		if (ld->set_termios)
			(ld->set_termios)(tty, &old_termios);
		tty_ldisc_deref(ld);
	}
	mutex_unlock(&tty->termios_mutex);
}

/**
 *	set_termios		-	set termios values for a tty
 *	@tty: terminal device
 *	@arg: user data
 *	@opt: option information
 *
 *	Helper function to prepare termios data and run neccessary other
 *	functions before using change_termios to do the actual changes.
 *
 *	Locking:
 *		Called functions take ldisc and termios_sem locks
 */

static int set_termios(struct tty_struct * tty, void __user *arg, int opt)
{
	struct ktermios tmp_termios;
	struct tty_ldisc *ld;
	int retval = tty_check_change(tty);

	if (retval)
		return retval;

	if (opt & TERMIOS_TERMIO) {
		memcpy(&tmp_termios, tty->termios, sizeof(struct ktermios));
		if (user_termio_to_kernel_termios(&tmp_termios,
						(struct termio __user *)arg))
			return -EFAULT;
#ifdef TCGETS2
	} else if (opt & TERMIOS_OLD) {
		memcpy(&tmp_termios, tty->termios, sizeof(struct termios));
		if (user_termios_to_kernel_termios_1(&tmp_termios,
						(struct termios_v1 __user *)arg))
			return -EFAULT;
#endif
	} else {
		if (user_termios_to_kernel_termios(&tmp_termios,
						(struct termios __user *)arg))
			return -EFAULT;
	}

	/* If old style Bfoo values are used then load c_ispeed/c_ospeed with the real speed
	   so its unconditionally usable */
	tmp_termios.c_ispeed = tty_termios_input_baud_rate(&tmp_termios);
	tmp_termios.c_ospeed = tty_termios_baud_rate(&tmp_termios);

	ld = tty_ldisc_ref(tty);
	
	if (ld != NULL) {
		if ((opt & TERMIOS_FLUSH) && ld->flush_buffer)
			ld->flush_buffer(tty);
		tty_ldisc_deref(ld);
	}
	
	if (opt & TERMIOS_WAIT) {
		tty_wait_until_sent(tty, 0);
		if (signal_pending(current))
			return -EINTR;
	}

	change_termios(tty, &tmp_termios);
	return 0;
}

static int get_termio(struct tty_struct * tty, struct termio __user * termio)
{
	if (kernel_termios_to_user_termio(termio, tty->termios))
		return -EFAULT;
	return 0;
}

static unsigned long inq_canon(struct tty_struct * tty)
{
	int nr, head, tail;

	if (!tty->canon_data || !tty->read_buf)
		return 0;
	head = tty->canon_head;
	tail = tty->read_tail;
	nr = (head - tail) & (N_TTY_BUF_SIZE-1);
	/* Skip EOF-chars.. */
	while (head != tail) {
		if (test_bit(tail, tty->read_flags) &&
		    tty->read_buf[tail] == __DISABLED_CHAR)
			nr--;
		tail = (tail+1) & (N_TTY_BUF_SIZE-1);
	}
	return nr;
}

#ifdef TIOCGETP
/*
 * These are deprecated, but there is limited support..
 *
 * The "sg_flags" translation is a joke..
 */
static int get_sgflags(struct tty_struct * tty)
{
	int flags = 0;

	if (!(tty->termios->c_lflag & ICANON)) {
		if (tty->termios->c_lflag & ISIG)
			flags |= 0x02;		/* cbreak */
		else
			flags |= 0x20;		/* raw */
	}
	if (tty->termios->c_lflag & ECHO)
		flags |= 0x08;			/* echo */
	if (tty->termios->c_oflag & OPOST)
		if (tty->termios->c_oflag & ONLCR)
			flags |= 0x10;		/* crmod */
	return flags;
}

static int get_sgttyb(struct tty_struct * tty, struct sgttyb __user * sgttyb)
{
	struct sgttyb tmp;

	mutex_lock(&tty->termios_mutex);
	tmp.sg_ispeed = tty->termios->c_ispeed;
	tmp.sg_ospeed = tty->termios->c_ospeed;
	tmp.sg_erase = tty->termios->c_cc[VERASE];
	tmp.sg_kill = tty->termios->c_cc[VKILL];
	tmp.sg_flags = get_sgflags(tty);
	mutex_unlock(&tty->termios_mutex);
	
	return copy_to_user(sgttyb, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

static void set_sgflags(struct ktermios * termios, int flags)
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
 *	Locking: termios_sem
 */

static int set_sgttyb(struct tty_struct * tty, struct sgttyb __user * sgttyb)
{
	int retval;
	struct sgttyb tmp;
	struct ktermios termios;

	retval = tty_check_change(tty);
	if (retval)
		return retval;
	
	if (copy_from_user(&tmp, sgttyb, sizeof(tmp)))
		return -EFAULT;

	mutex_lock(&tty->termios_mutex);
	termios =  *tty->termios;
	termios.c_cc[VERASE] = tmp.sg_erase;
	termios.c_cc[VKILL] = tmp.sg_kill;
	set_sgflags(&termios, tmp.sg_flags);
	/* Try and encode into Bfoo format */
#ifdef BOTHER
	tty_termios_encode_baud_rate(&termios, termios.c_ispeed, termios.c_ospeed);
#endif
	mutex_unlock(&tty->termios_mutex);
	change_termios(tty, &termios);
	return 0;
}
#endif

#ifdef TIOCGETC
static int get_tchars(struct tty_struct * tty, struct tchars __user * tchars)
{
	struct tchars tmp;

	tmp.t_intrc = tty->termios->c_cc[VINTR];
	tmp.t_quitc = tty->termios->c_cc[VQUIT];
	tmp.t_startc = tty->termios->c_cc[VSTART];
	tmp.t_stopc = tty->termios->c_cc[VSTOP];
	tmp.t_eofc = tty->termios->c_cc[VEOF];
	tmp.t_brkc = tty->termios->c_cc[VEOL2];	/* what is brkc anyway? */
	return copy_to_user(tchars, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

static int set_tchars(struct tty_struct * tty, struct tchars __user * tchars)
{
	struct tchars tmp;

	if (copy_from_user(&tmp, tchars, sizeof(tmp)))
		return -EFAULT;
	tty->termios->c_cc[VINTR] = tmp.t_intrc;
	tty->termios->c_cc[VQUIT] = tmp.t_quitc;
	tty->termios->c_cc[VSTART] = tmp.t_startc;
	tty->termios->c_cc[VSTOP] = tmp.t_stopc;
	tty->termios->c_cc[VEOF] = tmp.t_eofc;
	tty->termios->c_cc[VEOL2] = tmp.t_brkc;	/* what is brkc anyway? */
	return 0;
}
#endif

#ifdef TIOCGLTC
static int get_ltchars(struct tty_struct * tty, struct ltchars __user * ltchars)
{
	struct ltchars tmp;

	tmp.t_suspc = tty->termios->c_cc[VSUSP];
	tmp.t_dsuspc = tty->termios->c_cc[VSUSP];	/* what is dsuspc anyway? */
	tmp.t_rprntc = tty->termios->c_cc[VREPRINT];
	tmp.t_flushc = tty->termios->c_cc[VEOL2];	/* what is flushc anyway? */
	tmp.t_werasc = tty->termios->c_cc[VWERASE];
	tmp.t_lnextc = tty->termios->c_cc[VLNEXT];
	return copy_to_user(ltchars, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

static int set_ltchars(struct tty_struct * tty, struct ltchars __user * ltchars)
{
	struct ltchars tmp;

	if (copy_from_user(&tmp, ltchars, sizeof(tmp)))
		return -EFAULT;

	tty->termios->c_cc[VSUSP] = tmp.t_suspc;
	tty->termios->c_cc[VEOL2] = tmp.t_dsuspc;	/* what is dsuspc anyway? */
	tty->termios->c_cc[VREPRINT] = tmp.t_rprntc;
	tty->termios->c_cc[VEOL2] = tmp.t_flushc;	/* what is flushc anyway? */
	tty->termios->c_cc[VWERASE] = tmp.t_werasc;
	tty->termios->c_cc[VLNEXT] = tmp.t_lnextc;
	return 0;
}
#endif

/**
 *	send_prio_char		-	send priority character
 *
 *	Send a high priority character to the tty even if stopped
 *
 *	Locking: none for xchar method, write ordering for write method.
 */

static int send_prio_char(struct tty_struct *tty, char ch)
{
	int	was_stopped = tty->stopped;

	if (tty->driver->send_xchar) {
		tty->driver->send_xchar(tty, ch);
		return 0;
	}

	if (mutex_lock_interruptible(&tty->atomic_write_lock))
		return -ERESTARTSYS;

	if (was_stopped)
		start_tty(tty);
	tty->driver->write(tty, &ch, 1);
	if (was_stopped)
		stop_tty(tty);
	mutex_unlock(&tty->atomic_write_lock);
	return 0;
}

int n_tty_ioctl(struct tty_struct * tty, struct file * file,
		       unsigned int cmd, unsigned long arg)
{
	struct tty_struct * real_tty;
	void __user *p = (void __user *)arg;
	int retval;
	struct tty_ldisc *ld;

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
			if (kernel_termios_to_user_termios((struct termios __user *)arg, real_tty->termios))
				return -EFAULT;
			return 0;
#else
		case TCGETS:
			if (kernel_termios_to_user_termios_1((struct termios_v1 __user *)arg, real_tty->termios))
				return -EFAULT;
			return 0;
		case TCGETS2:
			if (kernel_termios_to_user_termios((struct termios __user *)arg, real_tty->termios))
				return -EFAULT;
			return 0;
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
		case TCXONC:
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			switch (arg) {
			case TCOOFF:
				if (!tty->flow_stopped) {
					tty->flow_stopped = 1;
					stop_tty(tty);
				}
				break;
			case TCOON:
				if (tty->flow_stopped) {
					tty->flow_stopped = 0;
					start_tty(tty);
				}
				break;
			case TCIOFF:
				if (STOP_CHAR(tty) != __DISABLED_CHAR)
					return send_prio_char(tty, STOP_CHAR(tty));
				break;
			case TCION:
				if (START_CHAR(tty) != __DISABLED_CHAR)
					return send_prio_char(tty, START_CHAR(tty));
				break;
			default:
				return -EINVAL;
			}
			return 0;
		case TCFLSH:
			retval = tty_check_change(tty);
			if (retval)
				return retval;
				
			ld = tty_ldisc_ref(tty);
			switch (arg) {
			case TCIFLUSH:
				if (ld && ld->flush_buffer)
					ld->flush_buffer(tty);
				break;
			case TCIOFLUSH:
				if (ld && ld->flush_buffer)
					ld->flush_buffer(tty);
				/* fall through */
			case TCOFLUSH:
				if (tty->driver->flush_buffer)
					tty->driver->flush_buffer(tty);
				break;
			default:
				tty_ldisc_deref(ld);
				return -EINVAL;
			}
			tty_ldisc_deref(ld);
			return 0;
		case TIOCOUTQ:
			return put_user(tty->driver->chars_in_buffer ?
					tty->driver->chars_in_buffer(tty) : 0,
					(int __user *) arg);
		case TIOCINQ:
			retval = tty->read_cnt;
			if (L_ICANON(tty))
				retval = inq_canon(tty);
			return put_user(retval, (unsigned int __user *) arg);
		case TIOCGLCKTRMIOS:
			if (kernel_termios_to_user_termios((struct termios __user *)arg, real_tty->termios_locked))
				return -EFAULT;
			return 0;

		case TIOCSLCKTRMIOS:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			if (user_termios_to_kernel_termios(real_tty->termios_locked, (struct termios __user *) arg))
				return -EFAULT;
			return 0;

		case TIOCPKT:
		{
			int pktmode;

			if (tty->driver->type != TTY_DRIVER_TYPE_PTY ||
			    tty->driver->subtype != PTY_TYPE_MASTER)
				return -ENOTTY;
			if (get_user(pktmode, (int __user *) arg))
				return -EFAULT;
			if (pktmode) {
				if (!tty->packet) {
					tty->packet = 1;
					tty->link->ctrl_status = 0;
				}
			} else
				tty->packet = 0;
			return 0;
		}
		case TIOCGSOFTCAR:
			return put_user(C_CLOCAL(tty) ? 1 : 0, (int __user *)arg);
		case TIOCSSOFTCAR:
			if (get_user(arg, (unsigned int __user *) arg))
				return -EFAULT;
			mutex_lock(&tty->termios_mutex);
			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				 (arg ? CLOCAL : 0));
			mutex_unlock(&tty->termios_mutex);
			return 0;
		default:
			return -ENOIOCTLCMD;
		}
}

EXPORT_SYMBOL(n_tty_ioctl);
