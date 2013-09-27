/*
 * n_tty.c --- implements the N_TTY line discipline.
 *
 * This code used to be in tty_io.c, but things are getting hairy
 * enough that it made sense to split things off.  (The N_TTY
 * processing has changed so much that it's hardly recognizable,
 * anyway...)
 *
 * Note that the open routine for N_TTY is guaranteed never to return
 * an error.  This is because Linux will fall back to setting a line
 * to N_TTY if it can not switch to any other line discipline.
 *
 * Written by Theodore Ts'o, Copyright 1994.
 *
 * This file also contains code originally written by Linus Torvalds,
 * Copyright 1991, 1992, 1993, and by Julian Cowley, Copyright 1994.
 *
 * This file may be redistributed under the terms of the GNU General Public
 * License.
 *
 * Reduced memory usage for older ARM systems  - Russell King.
 *
 * 2000/01/20   Fixed SMP locking on put_tty_queue using bits of
 *		the patch by Andrew J. Kroll <ag784@freenet.buffalo.edu>
 *		who actually finally proved there really was a race.
 *
 * 2002/03/18   Implemented n_tty_wakeup to send SIGIO POLL_OUTs to
 *		waiting writing processes-Sapan Bhatia <sapan@corewars.org>.
 *		Also fixed a bug in BLOCKING mode where n_tty_write returns
 *		EAGAIN
 */

#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/bitops.h>
#include <linux/audit.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>


/* number of characters left in xmit buffer before select has we have room */
#define WAKEUP_CHARS 256

/*
 * This defines the low- and high-watermarks for throttling and
 * unthrottling the TTY driver.  These watermarks are used for
 * controlling the space in the read buffer.
 */
#define TTY_THRESHOLD_THROTTLE		128 /* now based on remaining room */
#define TTY_THRESHOLD_UNTHROTTLE	128

/*
 * Special byte codes used in the echo buffer to represent operations
 * or special handling of characters.  Bytes in the echo buffer that
 * are not part of such special blocks are treated as normal character
 * codes.
 */
#define ECHO_OP_START 0xff
#define ECHO_OP_MOVE_BACK_COL 0x80
#define ECHO_OP_SET_CANON_COL 0x81
#define ECHO_OP_ERASE_TAB 0x82

#define ECHO_COMMIT_WATERMARK	256
#define ECHO_BLOCK		256
#define ECHO_DISCARD_WATERMARK	N_TTY_BUF_SIZE - (ECHO_BLOCK + 32)


#undef N_TTY_TRACE
#ifdef N_TTY_TRACE
# define n_tty_trace(f, args...)	trace_printk(f, ##args)
#else
# define n_tty_trace(f, args...)
#endif

struct n_tty_data {
	/* producer-published */
	size_t read_head;
	size_t canon_head;
	size_t echo_head;
	size_t echo_commit;
	DECLARE_BITMAP(char_map, 256);

	/* private to n_tty_receive_overrun (single-threaded) */
	unsigned long overrun_time;
	int num_overrun;

	/* non-atomic */
	bool no_room;

	/* must hold exclusive termios_rwsem to reset these */
	unsigned char lnext:1, erasing:1, raw:1, real_raw:1, icanon:1;

	/* shared by producer and consumer */
	char read_buf[N_TTY_BUF_SIZE];
	DECLARE_BITMAP(read_flags, N_TTY_BUF_SIZE);
	unsigned char echo_buf[N_TTY_BUF_SIZE];

	int minimum_to_wake;

	/* consumer-published */
	size_t read_tail;
	size_t line_start;

	/* protected by output lock */
	unsigned int column;
	unsigned int canon_column;
	size_t echo_tail;

	struct mutex atomic_read_lock;
	struct mutex output_lock;
};

static inline size_t read_cnt(struct n_tty_data *ldata)
{
	return ldata->read_head - ldata->read_tail;
}

static inline unsigned char read_buf(struct n_tty_data *ldata, size_t i)
{
	return ldata->read_buf[i & (N_TTY_BUF_SIZE - 1)];
}

static inline unsigned char *read_buf_addr(struct n_tty_data *ldata, size_t i)
{
	return &ldata->read_buf[i & (N_TTY_BUF_SIZE - 1)];
}

static inline unsigned char echo_buf(struct n_tty_data *ldata, size_t i)
{
	return ldata->echo_buf[i & (N_TTY_BUF_SIZE - 1)];
}

static inline unsigned char *echo_buf_addr(struct n_tty_data *ldata, size_t i)
{
	return &ldata->echo_buf[i & (N_TTY_BUF_SIZE - 1)];
}

static inline int tty_put_user(struct tty_struct *tty, unsigned char x,
			       unsigned char __user *ptr)
{
	struct n_tty_data *ldata = tty->disc_data;

	tty_audit_add_data(tty, &x, 1, ldata->icanon);
	return put_user(x, ptr);
}

static int receive_room(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	int left;

	if (I_PARMRK(tty)) {
		/* Multiply read_cnt by 3, since each byte might take up to
		 * three times as many spaces when PARMRK is set (depending on
		 * its flags, e.g. parity error). */
		left = N_TTY_BUF_SIZE - read_cnt(ldata) * 3 - 1;
	} else
		left = N_TTY_BUF_SIZE - read_cnt(ldata) - 1;

	/*
	 * If we are doing input canonicalization, and there are no
	 * pending newlines, let characters through without limit, so
	 * that erase characters will be handled.  Other excess
	 * characters will be beeped.
	 */
	if (left <= 0)
		left = ldata->icanon && ldata->canon_head == ldata->read_tail;

	return left;
}

/**
 *	n_tty_set_room	-	receive space
 *	@tty: terminal
 *
 *	Re-schedules the flip buffer work if space just became available.
 *
 *	Caller holds exclusive termios_rwsem
 *	   or
 *	n_tty_read()/consumer path:
 *		holds non-exclusive termios_rwsem
 */

static void n_tty_set_room(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	/* Did this open up the receive buffer? We may need to flip */
	if (unlikely(ldata->no_room) && receive_room(tty)) {
		ldata->no_room = 0;

		WARN_RATELIMIT(tty->port->itty == NULL,
				"scheduling with invalid itty\n");
		/* see if ldisc has been killed - if so, this means that
		 * even though the ldisc has been halted and ->buf.work
		 * cancelled, ->buf.work is about to be rescheduled
		 */
		WARN_RATELIMIT(test_bit(TTY_LDISC_HALTED, &tty->flags),
			       "scheduling buffer work for halted ldisc\n");
		queue_work(system_unbound_wq, &tty->port->buf.work);
	}
}

static ssize_t chars_in_buffer(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	ssize_t n = 0;

	if (!ldata->icanon)
		n = read_cnt(ldata);
	else
		n = ldata->canon_head - ldata->read_tail;
	return n;
}

/**
 *	n_tty_write_wakeup	-	asynchronous I/O notifier
 *	@tty: tty device
 *
 *	Required for the ptys, serial driver etc. since processes
 *	that attach themselves to the master and rely on ASYNC
 *	IO must be woken up
 */

static void n_tty_write_wakeup(struct tty_struct *tty)
{
	if (tty->fasync && test_and_clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags))
		kill_fasync(&tty->fasync, SIGIO, POLL_OUT);
}

static void n_tty_check_throttle(struct tty_struct *tty)
{
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY)
		return;
	/*
	 * Check the remaining room for the input canonicalization
	 * mode.  We don't want to throttle the driver if we're in
	 * canonical mode and don't have a newline yet!
	 */
	while (1) {
		int throttled;
		tty_set_flow_change(tty, TTY_THROTTLE_SAFE);
		if (receive_room(tty) >= TTY_THRESHOLD_THROTTLE)
			break;
		throttled = tty_throttle_safe(tty);
		if (!throttled)
			break;
	}
	__tty_set_flow_change(tty, 0);
}

static void n_tty_check_unthrottle(struct tty_struct *tty)
{
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->link->ldisc->ops->write_wakeup == n_tty_write_wakeup) {
		if (chars_in_buffer(tty) > TTY_THRESHOLD_UNTHROTTLE)
			return;
		if (!tty->count)
			return;
		n_tty_set_room(tty);
		n_tty_write_wakeup(tty->link);
		wake_up_interruptible_poll(&tty->link->write_wait, POLLOUT);
		return;
	}

	/* If there is enough space in the read buffer now, let the
	 * low-level driver know. We use chars_in_buffer() to
	 * check the buffer, as it now knows about canonical mode.
	 * Otherwise, if the driver is throttled and the line is
	 * longer than TTY_THRESHOLD_UNTHROTTLE in canonical mode,
	 * we won't get any more characters.
	 */

	while (1) {
		int unthrottled;
		tty_set_flow_change(tty, TTY_UNTHROTTLE_SAFE);
		if (chars_in_buffer(tty) > TTY_THRESHOLD_UNTHROTTLE)
			break;
		if (!tty->count)
			break;
		n_tty_set_room(tty);
		unthrottled = tty_unthrottle_safe(tty);
		if (!unthrottled)
			break;
	}
	__tty_set_flow_change(tty, 0);
}

/**
 *	put_tty_queue		-	add character to tty
 *	@c: character
 *	@ldata: n_tty data
 *
 *	Add a character to the tty read_buf queue.
 *
 *	n_tty_receive_buf()/producer path:
 *		caller holds non-exclusive termios_rwsem
 *		modifies read_head
 *
 *	read_head is only considered 'published' if canonical mode is
 *	not active.
 */

static inline void put_tty_queue(unsigned char c, struct n_tty_data *ldata)
{
	*read_buf_addr(ldata, ldata->read_head++) = c;
}

/**
 *	reset_buffer_flags	-	reset buffer state
 *	@tty: terminal to reset
 *
 *	Reset the read buffer counters and clear the flags.
 *	Called from n_tty_open() and n_tty_flush_buffer().
 *
 *	Locking: caller holds exclusive termios_rwsem
 *		 (or locking is not required)
 */

static void reset_buffer_flags(struct n_tty_data *ldata)
{
	ldata->read_head = ldata->canon_head = ldata->read_tail = 0;
	ldata->echo_head = ldata->echo_tail = ldata->echo_commit = 0;
	ldata->line_start = 0;

	ldata->erasing = 0;
	bitmap_zero(ldata->read_flags, N_TTY_BUF_SIZE);
}

static void n_tty_packet_mode_flush(struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&tty->ctrl_lock, flags);
	if (tty->link->packet) {
		tty->ctrl_status |= TIOCPKT_FLUSHREAD;
		wake_up_interruptible(&tty->link->read_wait);
	}
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);
}

/**
 *	n_tty_flush_buffer	-	clean input queue
 *	@tty:	terminal device
 *
 *	Flush the input buffer. Called when the tty layer wants the
 *	buffer flushed (eg at hangup) or when the N_TTY line discipline
 *	internally has to clean the pending queue (for example some signals).
 *
 *	Holds termios_rwsem to exclude producer/consumer while
 *	buffer indices are reset.
 *
 *	Locking: ctrl_lock, exclusive termios_rwsem
 */

static void n_tty_flush_buffer(struct tty_struct *tty)
{
	down_write(&tty->termios_rwsem);
	reset_buffer_flags(tty->disc_data);
	n_tty_set_room(tty);

	if (tty->link)
		n_tty_packet_mode_flush(tty);
	up_write(&tty->termios_rwsem);
}

/**
 *	n_tty_chars_in_buffer	-	report available bytes
 *	@tty: tty device
 *
 *	Report the number of characters buffered to be delivered to user
 *	at this instant in time.
 *
 *	Locking: exclusive termios_rwsem
 */

static ssize_t n_tty_chars_in_buffer(struct tty_struct *tty)
{
	ssize_t n;

	WARN_ONCE(1, "%s is deprecated and scheduled for removal.", __func__);

	down_write(&tty->termios_rwsem);
	n = chars_in_buffer(tty);
	up_write(&tty->termios_rwsem);
	return n;
}

/**
 *	is_utf8_continuation	-	utf8 multibyte check
 *	@c: byte to check
 *
 *	Returns true if the utf8 character 'c' is a multibyte continuation
 *	character. We use this to correctly compute the on screen size
 *	of the character when printing
 */

static inline int is_utf8_continuation(unsigned char c)
{
	return (c & 0xc0) == 0x80;
}

/**
 *	is_continuation		-	multibyte check
 *	@c: byte to check
 *
 *	Returns true if the utf8 character 'c' is a multibyte continuation
 *	character and the terminal is in unicode mode.
 */

static inline int is_continuation(unsigned char c, struct tty_struct *tty)
{
	return I_IUTF8(tty) && is_utf8_continuation(c);
}

/**
 *	do_output_char			-	output one character
 *	@c: character (or partial unicode symbol)
 *	@tty: terminal device
 *	@space: space available in tty driver write buffer
 *
 *	This is a helper function that handles one output character
 *	(including special characters like TAB, CR, LF, etc.),
 *	doing OPOST processing and putting the results in the
 *	tty driver's write buffer.
 *
 *	Note that Linux currently ignores TABDLY, CRDLY, VTDLY, FFDLY
 *	and NLDLY.  They simply aren't relevant in the world today.
 *	If you ever need them, add them here.
 *
 *	Returns the number of bytes of buffer space used or -1 if
 *	no space left.
 *
 *	Locking: should be called under the output_lock to protect
 *		 the column state and space left in the buffer
 */

static int do_output_char(unsigned char c, struct tty_struct *tty, int space)
{
	struct n_tty_data *ldata = tty->disc_data;
	int	spaces;

	if (!space)
		return -1;

	switch (c) {
	case '\n':
		if (O_ONLRET(tty))
			ldata->column = 0;
		if (O_ONLCR(tty)) {
			if (space < 2)
				return -1;
			ldata->canon_column = ldata->column = 0;
			tty->ops->write(tty, "\r\n", 2);
			return 2;
		}
		ldata->canon_column = ldata->column;
		break;
	case '\r':
		if (O_ONOCR(tty) && ldata->column == 0)
			return 0;
		if (O_OCRNL(tty)) {
			c = '\n';
			if (O_ONLRET(tty))
				ldata->canon_column = ldata->column = 0;
			break;
		}
		ldata->canon_column = ldata->column = 0;
		break;
	case '\t':
		spaces = 8 - (ldata->column & 7);
		if (O_TABDLY(tty) == XTABS) {
			if (space < spaces)
				return -1;
			ldata->column += spaces;
			tty->ops->write(tty, "        ", spaces);
			return spaces;
		}
		ldata->column += spaces;
		break;
	case '\b':
		if (ldata->column > 0)
			ldata->column--;
		break;
	default:
		if (!iscntrl(c)) {
			if (O_OLCUC(tty))
				c = toupper(c);
			if (!is_continuation(c, tty))
				ldata->column++;
		}
		break;
	}

	tty_put_char(tty, c);
	return 1;
}

/**
 *	process_output			-	output post processor
 *	@c: character (or partial unicode symbol)
 *	@tty: terminal device
 *
 *	Output one character with OPOST processing.
 *	Returns -1 when the output device is full and the character
 *	must be retried.
 *
 *	Locking: output_lock to protect column state and space left
 *		 (also, this is called from n_tty_write under the
 *		  tty layer write lock)
 */

static int process_output(unsigned char c, struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	int	space, retval;

	mutex_lock(&ldata->output_lock);

	space = tty_write_room(tty);
	retval = do_output_char(c, tty, space);

	mutex_unlock(&ldata->output_lock);
	if (retval < 0)
		return -1;
	else
		return 0;
}

/**
 *	process_output_block		-	block post processor
 *	@tty: terminal device
 *	@buf: character buffer
 *	@nr: number of bytes to output
 *
 *	Output a block of characters with OPOST processing.
 *	Returns the number of characters output.
 *
 *	This path is used to speed up block console writes, among other
 *	things when processing blocks of output data. It handles only
 *	the simple cases normally found and helps to generate blocks of
 *	symbols for the console driver and thus improve performance.
 *
 *	Locking: output_lock to protect column state and space left
 *		 (also, this is called from n_tty_write under the
 *		  tty layer write lock)
 */

static ssize_t process_output_block(struct tty_struct *tty,
				    const unsigned char *buf, unsigned int nr)
{
	struct n_tty_data *ldata = tty->disc_data;
	int	space;
	int	i;
	const unsigned char *cp;

	mutex_lock(&ldata->output_lock);

	space = tty_write_room(tty);
	if (!space) {
		mutex_unlock(&ldata->output_lock);
		return 0;
	}
	if (nr > space)
		nr = space;

	for (i = 0, cp = buf; i < nr; i++, cp++) {
		unsigned char c = *cp;

		switch (c) {
		case '\n':
			if (O_ONLRET(tty))
				ldata->column = 0;
			if (O_ONLCR(tty))
				goto break_out;
			ldata->canon_column = ldata->column;
			break;
		case '\r':
			if (O_ONOCR(tty) && ldata->column == 0)
				goto break_out;
			if (O_OCRNL(tty))
				goto break_out;
			ldata->canon_column = ldata->column = 0;
			break;
		case '\t':
			goto break_out;
		case '\b':
			if (ldata->column > 0)
				ldata->column--;
			break;
		default:
			if (!iscntrl(c)) {
				if (O_OLCUC(tty))
					goto break_out;
				if (!is_continuation(c, tty))
					ldata->column++;
			}
			break;
		}
	}
break_out:
	i = tty->ops->write(tty, buf, i);

	mutex_unlock(&ldata->output_lock);
	return i;
}

/**
 *	process_echoes	-	write pending echo characters
 *	@tty: terminal device
 *
 *	Write previously buffered echo (and other ldisc-generated)
 *	characters to the tty.
 *
 *	Characters generated by the ldisc (including echoes) need to
 *	be buffered because the driver's write buffer can fill during
 *	heavy program output.  Echoing straight to the driver will
 *	often fail under these conditions, causing lost characters and
 *	resulting mismatches of ldisc state information.
 *
 *	Since the ldisc state must represent the characters actually sent
 *	to the driver at the time of the write, operations like certain
 *	changes in column state are also saved in the buffer and executed
 *	here.
 *
 *	A circular fifo buffer is used so that the most recent characters
 *	are prioritized.  Also, when control characters are echoed with a
 *	prefixed "^", the pair is treated atomically and thus not separated.
 *
 *	Locking: callers must hold output_lock
 */

static size_t __process_echoes(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	int	space, old_space;
	size_t tail;
	unsigned char c;

	old_space = space = tty_write_room(tty);

	tail = ldata->echo_tail;
	while (ldata->echo_commit != tail) {
		c = echo_buf(ldata, tail);
		if (c == ECHO_OP_START) {
			unsigned char op;
			int no_space_left = 0;

			/*
			 * If the buffer byte is the start of a multi-byte
			 * operation, get the next byte, which is either the
			 * op code or a control character value.
			 */
			op = echo_buf(ldata, tail + 1);

			switch (op) {
				unsigned int num_chars, num_bs;

			case ECHO_OP_ERASE_TAB:
				num_chars = echo_buf(ldata, tail + 2);

				/*
				 * Determine how many columns to go back
				 * in order to erase the tab.
				 * This depends on the number of columns
				 * used by other characters within the tab
				 * area.  If this (modulo 8) count is from
				 * the start of input rather than from a
				 * previous tab, we offset by canon column.
				 * Otherwise, tab spacing is normal.
				 */
				if (!(num_chars & 0x80))
					num_chars += ldata->canon_column;
				num_bs = 8 - (num_chars & 7);

				if (num_bs > space) {
					no_space_left = 1;
					break;
				}
				space -= num_bs;
				while (num_bs--) {
					tty_put_char(tty, '\b');
					if (ldata->column > 0)
						ldata->column--;
				}
				tail += 3;
				break;

			case ECHO_OP_SET_CANON_COL:
				ldata->canon_column = ldata->column;
				tail += 2;
				break;

			case ECHO_OP_MOVE_BACK_COL:
				if (ldata->column > 0)
					ldata->column--;
				tail += 2;
				break;

			case ECHO_OP_START:
				/* This is an escaped echo op start code */
				if (!space) {
					no_space_left = 1;
					break;
				}
				tty_put_char(tty, ECHO_OP_START);
				ldata->column++;
				space--;
				tail += 2;
				break;

			default:
				/*
				 * If the op is not a special byte code,
				 * it is a ctrl char tagged to be echoed
				 * as "^X" (where X is the letter
				 * representing the control char).
				 * Note that we must ensure there is
				 * enough space for the whole ctrl pair.
				 *
				 */
				if (space < 2) {
					no_space_left = 1;
					break;
				}
				tty_put_char(tty, '^');
				tty_put_char(tty, op ^ 0100);
				ldata->column += 2;
				space -= 2;
				tail += 2;
			}

			if (no_space_left)
				break;
		} else {
			if (O_OPOST(tty)) {
				int retval = do_output_char(c, tty, space);
				if (retval < 0)
					break;
				space -= retval;
			} else {
				if (!space)
					break;
				tty_put_char(tty, c);
				space -= 1;
			}
			tail += 1;
		}
	}

	/* If the echo buffer is nearly full (so that the possibility exists
	 * of echo overrun before the next commit), then discard enough
	 * data at the tail to prevent a subsequent overrun */
	while (ldata->echo_commit - tail >= ECHO_DISCARD_WATERMARK) {
		if (echo_buf(ldata, tail == ECHO_OP_START)) {
			if (echo_buf(ldata, tail) == ECHO_OP_ERASE_TAB)
				tail += 3;
			else
				tail += 2;
		} else
			tail++;
	}

	ldata->echo_tail = tail;
	return old_space - space;
}

static void commit_echoes(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	size_t nr, old, echoed;
	size_t head;

	head = ldata->echo_head;
	old = ldata->echo_commit - ldata->echo_tail;

	/* Process committed echoes if the accumulated # of bytes
	 * is over the threshold (and try again each time another
	 * block is accumulated) */
	nr = head - ldata->echo_tail;
	if (nr < ECHO_COMMIT_WATERMARK || (nr % ECHO_BLOCK > old % ECHO_BLOCK))
		return;

	mutex_lock(&ldata->output_lock);
	ldata->echo_commit = head;
	echoed = __process_echoes(tty);
	mutex_unlock(&ldata->output_lock);

	if (echoed && tty->ops->flush_chars)
		tty->ops->flush_chars(tty);
}

static void process_echoes(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	size_t echoed;

	if (!L_ECHO(tty) || ldata->echo_commit == ldata->echo_tail)
		return;

	mutex_lock(&ldata->output_lock);
	echoed = __process_echoes(tty);
	mutex_unlock(&ldata->output_lock);

	if (echoed && tty->ops->flush_chars)
		tty->ops->flush_chars(tty);
}

static void flush_echoes(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (!L_ECHO(tty) || ldata->echo_commit == ldata->echo_head)
		return;

	mutex_lock(&ldata->output_lock);
	ldata->echo_commit = ldata->echo_head;
	__process_echoes(tty);
	mutex_unlock(&ldata->output_lock);
}

/**
 *	add_echo_byte	-	add a byte to the echo buffer
 *	@c: unicode byte to echo
 *	@ldata: n_tty data
 *
 *	Add a character or operation byte to the echo buffer.
 */

static inline void add_echo_byte(unsigned char c, struct n_tty_data *ldata)
{
	*echo_buf_addr(ldata, ldata->echo_head++) = c;
}

/**
 *	echo_move_back_col	-	add operation to move back a column
 *	@ldata: n_tty data
 *
 *	Add an operation to the echo buffer to move back one column.
 */

static void echo_move_back_col(struct n_tty_data *ldata)
{
	add_echo_byte(ECHO_OP_START, ldata);
	add_echo_byte(ECHO_OP_MOVE_BACK_COL, ldata);
}

/**
 *	echo_set_canon_col	-	add operation to set the canon column
 *	@ldata: n_tty data
 *
 *	Add an operation to the echo buffer to set the canon column
 *	to the current column.
 */

static void echo_set_canon_col(struct n_tty_data *ldata)
{
	add_echo_byte(ECHO_OP_START, ldata);
	add_echo_byte(ECHO_OP_SET_CANON_COL, ldata);
}

/**
 *	echo_erase_tab	-	add operation to erase a tab
 *	@num_chars: number of character columns already used
 *	@after_tab: true if num_chars starts after a previous tab
 *	@ldata: n_tty data
 *
 *	Add an operation to the echo buffer to erase a tab.
 *
 *	Called by the eraser function, which knows how many character
 *	columns have been used since either a previous tab or the start
 *	of input.  This information will be used later, along with
 *	canon column (if applicable), to go back the correct number
 *	of columns.
 */

static void echo_erase_tab(unsigned int num_chars, int after_tab,
			   struct n_tty_data *ldata)
{
	add_echo_byte(ECHO_OP_START, ldata);
	add_echo_byte(ECHO_OP_ERASE_TAB, ldata);

	/* We only need to know this modulo 8 (tab spacing) */
	num_chars &= 7;

	/* Set the high bit as a flag if num_chars is after a previous tab */
	if (after_tab)
		num_chars |= 0x80;

	add_echo_byte(num_chars, ldata);
}

/**
 *	echo_char_raw	-	echo a character raw
 *	@c: unicode byte to echo
 *	@tty: terminal device
 *
 *	Echo user input back onto the screen. This must be called only when
 *	L_ECHO(tty) is true. Called from the driver receive_buf path.
 *
 *	This variant does not treat control characters specially.
 */

static void echo_char_raw(unsigned char c, struct n_tty_data *ldata)
{
	if (c == ECHO_OP_START) {
		add_echo_byte(ECHO_OP_START, ldata);
		add_echo_byte(ECHO_OP_START, ldata);
	} else {
		add_echo_byte(c, ldata);
	}
}

/**
 *	echo_char	-	echo a character
 *	@c: unicode byte to echo
 *	@tty: terminal device
 *
 *	Echo user input back onto the screen. This must be called only when
 *	L_ECHO(tty) is true. Called from the driver receive_buf path.
 *
 *	This variant tags control characters to be echoed as "^X"
 *	(where X is the letter representing the control char).
 */

static void echo_char(unsigned char c, struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (c == ECHO_OP_START) {
		add_echo_byte(ECHO_OP_START, ldata);
		add_echo_byte(ECHO_OP_START, ldata);
	} else {
		if (L_ECHOCTL(tty) && iscntrl(c) && c != '\t')
			add_echo_byte(ECHO_OP_START, ldata);
		add_echo_byte(c, ldata);
	}
}

/**
 *	finish_erasing		-	complete erase
 *	@ldata: n_tty data
 */

static inline void finish_erasing(struct n_tty_data *ldata)
{
	if (ldata->erasing) {
		echo_char_raw('/', ldata);
		ldata->erasing = 0;
	}
}

/**
 *	eraser		-	handle erase function
 *	@c: character input
 *	@tty: terminal device
 *
 *	Perform erase and necessary output when an erase character is
 *	present in the stream from the driver layer. Handles the complexities
 *	of UTF-8 multibyte symbols.
 *
 *	n_tty_receive_buf()/producer path:
 *		caller holds non-exclusive termios_rwsem
 *		modifies read_head
 *
 *	Modifying the read_head is not considered a publish in this context
 *	because canonical mode is active -- only canon_head publishes
 */

static void eraser(unsigned char c, struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	enum { ERASE, WERASE, KILL } kill_type;
	size_t head;
	size_t cnt;
	int seen_alnums;

	if (ldata->read_head == ldata->canon_head) {
		/* process_output('\a', tty); */ /* what do you think? */
		return;
	}
	if (c == ERASE_CHAR(tty))
		kill_type = ERASE;
	else if (c == WERASE_CHAR(tty))
		kill_type = WERASE;
	else {
		if (!L_ECHO(tty)) {
			ldata->read_head = ldata->canon_head;
			return;
		}
		if (!L_ECHOK(tty) || !L_ECHOKE(tty) || !L_ECHOE(tty)) {
			ldata->read_head = ldata->canon_head;
			finish_erasing(ldata);
			echo_char(KILL_CHAR(tty), tty);
			/* Add a newline if ECHOK is on and ECHOKE is off. */
			if (L_ECHOK(tty))
				echo_char_raw('\n', ldata);
			return;
		}
		kill_type = KILL;
	}

	seen_alnums = 0;
	while (ldata->read_head != ldata->canon_head) {
		head = ldata->read_head;

		/* erase a single possibly multibyte character */
		do {
			head--;
			c = read_buf(ldata, head);
		} while (is_continuation(c, tty) && head != ldata->canon_head);

		/* do not partially erase */
		if (is_continuation(c, tty))
			break;

		if (kill_type == WERASE) {
			/* Equivalent to BSD's ALTWERASE. */
			if (isalnum(c) || c == '_')
				seen_alnums++;
			else if (seen_alnums)
				break;
		}
		cnt = ldata->read_head - head;
		ldata->read_head = head;
		if (L_ECHO(tty)) {
			if (L_ECHOPRT(tty)) {
				if (!ldata->erasing) {
					echo_char_raw('\\', ldata);
					ldata->erasing = 1;
				}
				/* if cnt > 1, output a multi-byte character */
				echo_char(c, tty);
				while (--cnt > 0) {
					head++;
					echo_char_raw(read_buf(ldata, head), ldata);
					echo_move_back_col(ldata);
				}
			} else if (kill_type == ERASE && !L_ECHOE(tty)) {
				echo_char(ERASE_CHAR(tty), tty);
			} else if (c == '\t') {
				unsigned int num_chars = 0;
				int after_tab = 0;
				size_t tail = ldata->read_head;

				/*
				 * Count the columns used for characters
				 * since the start of input or after a
				 * previous tab.
				 * This info is used to go back the correct
				 * number of columns.
				 */
				while (tail != ldata->canon_head) {
					tail--;
					c = read_buf(ldata, tail);
					if (c == '\t') {
						after_tab = 1;
						break;
					} else if (iscntrl(c)) {
						if (L_ECHOCTL(tty))
							num_chars += 2;
					} else if (!is_continuation(c, tty)) {
						num_chars++;
					}
				}
				echo_erase_tab(num_chars, after_tab, ldata);
			} else {
				if (iscntrl(c) && L_ECHOCTL(tty)) {
					echo_char_raw('\b', ldata);
					echo_char_raw(' ', ldata);
					echo_char_raw('\b', ldata);
				}
				if (!iscntrl(c) || L_ECHOCTL(tty)) {
					echo_char_raw('\b', ldata);
					echo_char_raw(' ', ldata);
					echo_char_raw('\b', ldata);
				}
			}
		}
		if (kill_type == ERASE)
			break;
	}
	if (ldata->read_head == ldata->canon_head && L_ECHO(tty))
		finish_erasing(ldata);
}

/**
 *	isig		-	handle the ISIG optio
 *	@sig: signal
 *	@tty: terminal
 *
 *	Called when a signal is being sent due to terminal input.
 *	Called from the driver receive_buf path so serialized.
 *
 *	Locking: ctrl_lock
 */

static void isig(int sig, struct tty_struct *tty)
{
	struct pid *tty_pgrp = tty_get_pgrp(tty);
	if (tty_pgrp) {
		kill_pgrp(tty_pgrp, sig, 1);
		put_pid(tty_pgrp);
	}
}

/**
 *	n_tty_receive_break	-	handle break
 *	@tty: terminal
 *
 *	An RS232 break event has been hit in the incoming bitstream. This
 *	can cause a variety of events depending upon the termios settings.
 *
 *	n_tty_receive_buf()/producer path:
 *		caller holds non-exclusive termios_rwsem
 *		publishes read_head via put_tty_queue()
 *
 *	Note: may get exclusive termios_rwsem if flushing input buffer
 */

static void n_tty_receive_break(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (I_IGNBRK(tty))
		return;
	if (I_BRKINT(tty)) {
		isig(SIGINT, tty);
		if (!L_NOFLSH(tty)) {
			/* flushing needs exclusive termios_rwsem */
			up_read(&tty->termios_rwsem);
			n_tty_flush_buffer(tty);
			tty_driver_flush_buffer(tty);
			down_read(&tty->termios_rwsem);
		}
		return;
	}
	if (I_PARMRK(tty)) {
		put_tty_queue('\377', ldata);
		put_tty_queue('\0', ldata);
	}
	put_tty_queue('\0', ldata);
	wake_up_interruptible(&tty->read_wait);
}

/**
 *	n_tty_receive_overrun	-	handle overrun reporting
 *	@tty: terminal
 *
 *	Data arrived faster than we could process it. While the tty
 *	driver has flagged this the bits that were missed are gone
 *	forever.
 *
 *	Called from the receive_buf path so single threaded. Does not
 *	need locking as num_overrun and overrun_time are function
 *	private.
 */

static void n_tty_receive_overrun(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	char buf[64];

	ldata->num_overrun++;
	if (time_after(jiffies, ldata->overrun_time + HZ) ||
			time_after(ldata->overrun_time, jiffies)) {
		printk(KERN_WARNING "%s: %d input overrun(s)\n",
			tty_name(tty, buf),
			ldata->num_overrun);
		ldata->overrun_time = jiffies;
		ldata->num_overrun = 0;
	}
}

/**
 *	n_tty_receive_parity_error	-	error notifier
 *	@tty: terminal device
 *	@c: character
 *
 *	Process a parity error and queue the right data to indicate
 *	the error case if necessary.
 *
 *	n_tty_receive_buf()/producer path:
 *		caller holds non-exclusive termios_rwsem
 *		publishes read_head via put_tty_queue()
 */
static void n_tty_receive_parity_error(struct tty_struct *tty, unsigned char c)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (I_IGNPAR(tty))
		return;
	if (I_PARMRK(tty)) {
		put_tty_queue('\377', ldata);
		put_tty_queue('\0', ldata);
		put_tty_queue(c, ldata);
	} else	if (I_INPCK(tty))
		put_tty_queue('\0', ldata);
	else
		put_tty_queue(c, ldata);
	wake_up_interruptible(&tty->read_wait);
}

static void
n_tty_receive_signal_char(struct tty_struct *tty, int signal, unsigned char c)
{
	if (!L_NOFLSH(tty)) {
		/* flushing needs exclusive termios_rwsem */
		up_read(&tty->termios_rwsem);
		n_tty_flush_buffer(tty);
		tty_driver_flush_buffer(tty);
		down_read(&tty->termios_rwsem);
	}
	if (I_IXON(tty))
		start_tty(tty);
	if (L_ECHO(tty)) {
		echo_char(c, tty);
		commit_echoes(tty);
	}
	isig(signal, tty);
	return;
}

/**
 *	n_tty_receive_char	-	perform processing
 *	@tty: terminal device
 *	@c: character
 *
 *	Process an individual character of input received from the driver.
 *	This is serialized with respect to itself by the rules for the
 *	driver above.
 *
 *	n_tty_receive_buf()/producer path:
 *		caller holds non-exclusive termios_rwsem
 *		publishes canon_head if canonical mode is active
 *		otherwise, publishes read_head via put_tty_queue()
 *
 *	Returns 1 if LNEXT was received, else returns 0
 */

static int
n_tty_receive_char_special(struct tty_struct *tty, unsigned char c)
{
	struct n_tty_data *ldata = tty->disc_data;
	int parmrk;

	if (I_IXON(tty)) {
		if (c == START_CHAR(tty)) {
			start_tty(tty);
			commit_echoes(tty);
			return 0;
		}
		if (c == STOP_CHAR(tty)) {
			stop_tty(tty);
			return 0;
		}
	}

	if (L_ISIG(tty)) {
		if (c == INTR_CHAR(tty)) {
			n_tty_receive_signal_char(tty, SIGINT, c);
			return 0;
		} else if (c == QUIT_CHAR(tty)) {
			n_tty_receive_signal_char(tty, SIGQUIT, c);
			return 0;
		} else if (c == SUSP_CHAR(tty)) {
			n_tty_receive_signal_char(tty, SIGTSTP, c);
			return 0;
		}
	}

	if (tty->stopped && !tty->flow_stopped && I_IXON(tty) && I_IXANY(tty)) {
		start_tty(tty);
		process_echoes(tty);
	}

	if (c == '\r') {
		if (I_IGNCR(tty))
			return 0;
		if (I_ICRNL(tty))
			c = '\n';
	} else if (c == '\n' && I_INLCR(tty))
		c = '\r';

	if (ldata->icanon) {
		if (c == ERASE_CHAR(tty) || c == KILL_CHAR(tty) ||
		    (c == WERASE_CHAR(tty) && L_IEXTEN(tty))) {
			eraser(c, tty);
			commit_echoes(tty);
			return 0;
		}
		if (c == LNEXT_CHAR(tty) && L_IEXTEN(tty)) {
			ldata->lnext = 1;
			if (L_ECHO(tty)) {
				finish_erasing(ldata);
				if (L_ECHOCTL(tty)) {
					echo_char_raw('^', ldata);
					echo_char_raw('\b', ldata);
					commit_echoes(tty);
				}
			}
			return 1;
		}
		if (c == REPRINT_CHAR(tty) && L_ECHO(tty) && L_IEXTEN(tty)) {
			size_t tail = ldata->canon_head;

			finish_erasing(ldata);
			echo_char(c, tty);
			echo_char_raw('\n', ldata);
			while (tail != ldata->read_head) {
				echo_char(read_buf(ldata, tail), tty);
				tail++;
			}
			commit_echoes(tty);
			return 0;
		}
		if (c == '\n') {
			if (L_ECHO(tty) || L_ECHONL(tty)) {
				echo_char_raw('\n', ldata);
				commit_echoes(tty);
			}
			goto handle_newline;
		}
		if (c == EOF_CHAR(tty)) {
			c = __DISABLED_CHAR;
			goto handle_newline;
		}
		if ((c == EOL_CHAR(tty)) ||
		    (c == EOL2_CHAR(tty) && L_IEXTEN(tty))) {
			parmrk = (c == (unsigned char) '\377' && I_PARMRK(tty))
				 ? 1 : 0;
			/*
			 * XXX are EOL_CHAR and EOL2_CHAR echoed?!?
			 */
			if (L_ECHO(tty)) {
				/* Record the column of first canon char. */
				if (ldata->canon_head == ldata->read_head)
					echo_set_canon_col(ldata);
				echo_char(c, tty);
				commit_echoes(tty);
			}
			/*
			 * XXX does PARMRK doubling happen for
			 * EOL_CHAR and EOL2_CHAR?
			 */
			if (parmrk)
				put_tty_queue(c, ldata);

handle_newline:
			set_bit(ldata->read_head & (N_TTY_BUF_SIZE - 1), ldata->read_flags);
			put_tty_queue(c, ldata);
			ldata->canon_head = ldata->read_head;
			kill_fasync(&tty->fasync, SIGIO, POLL_IN);
			if (waitqueue_active(&tty->read_wait))
				wake_up_interruptible(&tty->read_wait);
			return 0;
		}
	}

	parmrk = (c == (unsigned char) '\377' && I_PARMRK(tty)) ? 1 : 0;
	if (L_ECHO(tty)) {
		finish_erasing(ldata);
		if (c == '\n')
			echo_char_raw('\n', ldata);
		else {
			/* Record the column of first canon char. */
			if (ldata->canon_head == ldata->read_head)
				echo_set_canon_col(ldata);
			echo_char(c, tty);
		}
		commit_echoes(tty);
	}

	if (parmrk)
		put_tty_queue(c, ldata);

	put_tty_queue(c, ldata);
	return 0;
}

static inline void
n_tty_receive_char_inline(struct tty_struct *tty, unsigned char c)
{
	struct n_tty_data *ldata = tty->disc_data;
	int parmrk;

	if (tty->stopped && !tty->flow_stopped && I_IXON(tty) && I_IXANY(tty)) {
		start_tty(tty);
		process_echoes(tty);
	}
	if (L_ECHO(tty)) {
		finish_erasing(ldata);
		/* Record the column of first canon char. */
		if (ldata->canon_head == ldata->read_head)
			echo_set_canon_col(ldata);
		echo_char(c, tty);
		commit_echoes(tty);
	}
	parmrk = (c == (unsigned char) '\377' && I_PARMRK(tty)) ? 1 : 0;
	if (parmrk)
		put_tty_queue(c, ldata);
	put_tty_queue(c, ldata);
}

static inline void n_tty_receive_char(struct tty_struct *tty, unsigned char c)
{
	n_tty_receive_char_inline(tty, c);
}

static inline void
n_tty_receive_char_fast(struct tty_struct *tty, unsigned char c)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (tty->stopped && !tty->flow_stopped && I_IXON(tty) && I_IXANY(tty)) {
		start_tty(tty);
		process_echoes(tty);
	}
	if (L_ECHO(tty)) {
		finish_erasing(ldata);
		/* Record the column of first canon char. */
		if (ldata->canon_head == ldata->read_head)
			echo_set_canon_col(ldata);
		echo_char(c, tty);
		commit_echoes(tty);
	}
	put_tty_queue(c, ldata);
}

static inline void
n_tty_receive_char_closing(struct tty_struct *tty, unsigned char c)
{
	if (I_ISTRIP(tty))
		c &= 0x7f;
	if (I_IUCLC(tty) && L_IEXTEN(tty))
		c = tolower(c);

	if (I_IXON(tty)) {
		if (c == STOP_CHAR(tty))
			stop_tty(tty);
		else if (c == START_CHAR(tty) ||
			 (tty->stopped && !tty->flow_stopped && I_IXANY(tty) &&
			  c != INTR_CHAR(tty) && c != QUIT_CHAR(tty) &&
			  c != SUSP_CHAR(tty))) {
			start_tty(tty);
			process_echoes(tty);
		}
	}
}

static void
n_tty_receive_char_flagged(struct tty_struct *tty, unsigned char c, char flag)
{
	char buf[64];

	switch (flag) {
	case TTY_BREAK:
		n_tty_receive_break(tty);
		break;
	case TTY_PARITY:
	case TTY_FRAME:
		n_tty_receive_parity_error(tty, c);
		break;
	case TTY_OVERRUN:
		n_tty_receive_overrun(tty);
		break;
	default:
		printk(KERN_ERR "%s: unknown flag %d\n",
		       tty_name(tty, buf), flag);
		break;
	}
}

static void
n_tty_receive_char_lnext(struct tty_struct *tty, unsigned char c, char flag)
{
	struct n_tty_data *ldata = tty->disc_data;

	ldata->lnext = 0;
	if (likely(flag == TTY_NORMAL)) {
		if (I_ISTRIP(tty))
			c &= 0x7f;
		if (I_IUCLC(tty) && L_IEXTEN(tty))
			c = tolower(c);
		n_tty_receive_char(tty, c);
	} else
		n_tty_receive_char_flagged(tty, c, flag);
}

/**
 *	n_tty_receive_buf	-	data receive
 *	@tty: terminal device
 *	@cp: buffer
 *	@fp: flag buffer
 *	@count: characters
 *
 *	Called by the terminal driver when a block of characters has
 *	been received. This function must be called from soft contexts
 *	not from interrupt context. The driver is responsible for making
 *	calls one at a time and in order (or using flush_to_ldisc)
 *
 *	n_tty_receive_buf()/producer path:
 *		claims non-exclusive termios_rwsem
 *		publishes read_head and canon_head
 */

static void
n_tty_receive_buf_real_raw(struct tty_struct *tty, const unsigned char *cp,
			   char *fp, int count)
{
	struct n_tty_data *ldata = tty->disc_data;
	size_t n, head;

	head = ldata->read_head & (N_TTY_BUF_SIZE - 1);
	n = N_TTY_BUF_SIZE - max(read_cnt(ldata), head);
	n = min_t(size_t, count, n);
	memcpy(read_buf_addr(ldata, head), cp, n);
	ldata->read_head += n;
	cp += n;
	count -= n;

	head = ldata->read_head & (N_TTY_BUF_SIZE - 1);
	n = N_TTY_BUF_SIZE - max(read_cnt(ldata), head);
	n = min_t(size_t, count, n);
	memcpy(read_buf_addr(ldata, head), cp, n);
	ldata->read_head += n;
}

static void
n_tty_receive_buf_raw(struct tty_struct *tty, const unsigned char *cp,
		      char *fp, int count)
{
	struct n_tty_data *ldata = tty->disc_data;
	char flag = TTY_NORMAL;

	while (count--) {
		if (fp)
			flag = *fp++;
		if (likely(flag == TTY_NORMAL))
			put_tty_queue(*cp++, ldata);
		else
			n_tty_receive_char_flagged(tty, *cp++, flag);
	}
}

static void
n_tty_receive_buf_closing(struct tty_struct *tty, const unsigned char *cp,
			  char *fp, int count)
{
	char flag = TTY_NORMAL;

	while (count--) {
		if (fp)
			flag = *fp++;
		if (likely(flag == TTY_NORMAL))
			n_tty_receive_char_closing(tty, *cp++);
		else
			n_tty_receive_char_flagged(tty, *cp++, flag);
	}
}

static void
n_tty_receive_buf_standard(struct tty_struct *tty, const unsigned char *cp,
			  char *fp, int count)
{
	struct n_tty_data *ldata = tty->disc_data;
	char flag = TTY_NORMAL;

	while (count--) {
		if (fp)
			flag = *fp++;
		if (likely(flag == TTY_NORMAL)) {
			unsigned char c = *cp++;

			if (I_ISTRIP(tty))
				c &= 0x7f;
			if (I_IUCLC(tty) && L_IEXTEN(tty))
				c = tolower(c);
			if (L_EXTPROC(tty)) {
				put_tty_queue(c, ldata);
				continue;
			}
			if (!test_bit(c, ldata->char_map))
				n_tty_receive_char_inline(tty, c);
			else if (n_tty_receive_char_special(tty, c) && count) {
				if (fp)
					flag = *fp++;
				n_tty_receive_char_lnext(tty, *cp++, flag);
				count--;
			}
		} else
			n_tty_receive_char_flagged(tty, *cp++, flag);
	}
}

static void
n_tty_receive_buf_fast(struct tty_struct *tty, const unsigned char *cp,
		       char *fp, int count)
{
	struct n_tty_data *ldata = tty->disc_data;
	char flag = TTY_NORMAL;

	while (count--) {
		if (fp)
			flag = *fp++;
		if (likely(flag == TTY_NORMAL)) {
			unsigned char c = *cp++;

			if (!test_bit(c, ldata->char_map))
				n_tty_receive_char_fast(tty, c);
			else if (n_tty_receive_char_special(tty, c) && count) {
				if (fp)
					flag = *fp++;
				n_tty_receive_char_lnext(tty, *cp++, flag);
				count--;
			}
		} else
			n_tty_receive_char_flagged(tty, *cp++, flag);
	}
}

static void __receive_buf(struct tty_struct *tty, const unsigned char *cp,
			  char *fp, int count)
{
	struct n_tty_data *ldata = tty->disc_data;
	bool preops = I_ISTRIP(tty) || (I_IUCLC(tty) && L_IEXTEN(tty));

	if (ldata->real_raw)
		n_tty_receive_buf_real_raw(tty, cp, fp, count);
	else if (ldata->raw || (L_EXTPROC(tty) && !preops))
		n_tty_receive_buf_raw(tty, cp, fp, count);
	else if (tty->closing && !L_EXTPROC(tty))
		n_tty_receive_buf_closing(tty, cp, fp, count);
	else {
		if (ldata->lnext) {
			char flag = TTY_NORMAL;

			if (fp)
				flag = *fp++;
			n_tty_receive_char_lnext(tty, *cp++, flag);
			count--;
		}

		if (!preops && !I_PARMRK(tty))
			n_tty_receive_buf_fast(tty, cp, fp, count);
		else
			n_tty_receive_buf_standard(tty, cp, fp, count);

		flush_echoes(tty);
		if (tty->ops->flush_chars)
			tty->ops->flush_chars(tty);
	}

	if ((!ldata->icanon && (read_cnt(ldata) >= ldata->minimum_to_wake)) ||
		L_EXTPROC(tty)) {
		kill_fasync(&tty->fasync, SIGIO, POLL_IN);
		if (waitqueue_active(&tty->read_wait))
			wake_up_interruptible(&tty->read_wait);
	}
}

static void n_tty_receive_buf(struct tty_struct *tty, const unsigned char *cp,
			      char *fp, int count)
{
	int room, n;

	down_read(&tty->termios_rwsem);

	while (1) {
		room = receive_room(tty);
		n = min(count, room);
		if (!n)
			break;
		__receive_buf(tty, cp, fp, n);
		cp += n;
		if (fp)
			fp += n;
		count -= n;
	}

	tty->receive_room = room;
	n_tty_check_throttle(tty);
	up_read(&tty->termios_rwsem);
}

static int n_tty_receive_buf2(struct tty_struct *tty, const unsigned char *cp,
			      char *fp, int count)
{
	struct n_tty_data *ldata = tty->disc_data;
	int room, n, rcvd = 0;

	down_read(&tty->termios_rwsem);

	while (1) {
		room = receive_room(tty);
		n = min(count, room);
		if (!n) {
			if (!room)
				ldata->no_room = 1;
			break;
		}
		__receive_buf(tty, cp, fp, n);
		cp += n;
		if (fp)
			fp += n;
		count -= n;
		rcvd += n;
	}

	tty->receive_room = room;
	n_tty_check_throttle(tty);
	up_read(&tty->termios_rwsem);

	return rcvd;
}

int is_ignored(int sig)
{
	return (sigismember(&current->blocked, sig) ||
		current->sighand->action[sig-1].sa.sa_handler == SIG_IGN);
}

/**
 *	n_tty_set_termios	-	termios data changed
 *	@tty: terminal
 *	@old: previous data
 *
 *	Called by the tty layer when the user changes termios flags so
 *	that the line discipline can plan ahead. This function cannot sleep
 *	and is protected from re-entry by the tty layer. The user is
 *	guaranteed that this function will not be re-entered or in progress
 *	when the ldisc is closed.
 *
 *	Locking: Caller holds tty->termios_rwsem
 */

static void n_tty_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	struct n_tty_data *ldata = tty->disc_data;
	int canon_change = 1;

	if (old)
		canon_change = (old->c_lflag ^ tty->termios.c_lflag) & ICANON;
	if (canon_change) {
		bitmap_zero(ldata->read_flags, N_TTY_BUF_SIZE);
		ldata->line_start = 0;
		ldata->canon_head = ldata->read_tail;
		ldata->erasing = 0;
		ldata->lnext = 0;
	}

	if (canon_change && !L_ICANON(tty) && read_cnt(ldata))
		wake_up_interruptible(&tty->read_wait);

	ldata->icanon = (L_ICANON(tty) != 0);

	if (I_ISTRIP(tty) || I_IUCLC(tty) || I_IGNCR(tty) ||
	    I_ICRNL(tty) || I_INLCR(tty) || L_ICANON(tty) ||
	    I_IXON(tty) || L_ISIG(tty) || L_ECHO(tty) ||
	    I_PARMRK(tty)) {
		bitmap_zero(ldata->char_map, 256);

		if (I_IGNCR(tty) || I_ICRNL(tty))
			set_bit('\r', ldata->char_map);
		if (I_INLCR(tty))
			set_bit('\n', ldata->char_map);

		if (L_ICANON(tty)) {
			set_bit(ERASE_CHAR(tty), ldata->char_map);
			set_bit(KILL_CHAR(tty), ldata->char_map);
			set_bit(EOF_CHAR(tty), ldata->char_map);
			set_bit('\n', ldata->char_map);
			set_bit(EOL_CHAR(tty), ldata->char_map);
			if (L_IEXTEN(tty)) {
				set_bit(WERASE_CHAR(tty), ldata->char_map);
				set_bit(LNEXT_CHAR(tty), ldata->char_map);
				set_bit(EOL2_CHAR(tty), ldata->char_map);
				if (L_ECHO(tty))
					set_bit(REPRINT_CHAR(tty),
						ldata->char_map);
			}
		}
		if (I_IXON(tty)) {
			set_bit(START_CHAR(tty), ldata->char_map);
			set_bit(STOP_CHAR(tty), ldata->char_map);
		}
		if (L_ISIG(tty)) {
			set_bit(INTR_CHAR(tty), ldata->char_map);
			set_bit(QUIT_CHAR(tty), ldata->char_map);
			set_bit(SUSP_CHAR(tty), ldata->char_map);
		}
		clear_bit(__DISABLED_CHAR, ldata->char_map);
		ldata->raw = 0;
		ldata->real_raw = 0;
	} else {
		ldata->raw = 1;
		if ((I_IGNBRK(tty) || (!I_BRKINT(tty) && !I_PARMRK(tty))) &&
		    (I_IGNPAR(tty) || !I_INPCK(tty)) &&
		    (tty->driver->flags & TTY_DRIVER_REAL_RAW))
			ldata->real_raw = 1;
		else
			ldata->real_raw = 0;
	}
	n_tty_set_room(tty);
	/*
	 * Fix tty hang when I_IXON(tty) is cleared, but the tty
	 * been stopped by STOP_CHAR(tty) before it.
	 */
	if (!I_IXON(tty) && old && (old->c_iflag & IXON) && !tty->flow_stopped) {
		start_tty(tty);
	}

	/* The termios change make the tty ready for I/O */
	wake_up_interruptible(&tty->write_wait);
	wake_up_interruptible(&tty->read_wait);
}

/**
 *	n_tty_close		-	close the ldisc for this tty
 *	@tty: device
 *
 *	Called from the terminal layer when this line discipline is
 *	being shut down, either because of a close or becsuse of a
 *	discipline change. The function will not be called while other
 *	ldisc methods are in progress.
 */

static void n_tty_close(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (tty->link)
		n_tty_packet_mode_flush(tty);

	vfree(ldata);
	tty->disc_data = NULL;
}

/**
 *	n_tty_open		-	open an ldisc
 *	@tty: terminal to open
 *
 *	Called when this line discipline is being attached to the
 *	terminal device. Can sleep. Called serialized so that no
 *	other events will occur in parallel. No further open will occur
 *	until a close.
 */

static int n_tty_open(struct tty_struct *tty)
{
	struct n_tty_data *ldata;

	/* Currently a malloc failure here can panic */
	ldata = vmalloc(sizeof(*ldata));
	if (!ldata)
		goto err;

	ldata->overrun_time = jiffies;
	mutex_init(&ldata->atomic_read_lock);
	mutex_init(&ldata->output_lock);

	tty->disc_data = ldata;
	reset_buffer_flags(tty->disc_data);
	ldata->column = 0;
	ldata->canon_column = 0;
	ldata->minimum_to_wake = 1;
	ldata->num_overrun = 0;
	ldata->no_room = 0;
	ldata->lnext = 0;
	tty->closing = 0;
	/* indicate buffer work may resume */
	clear_bit(TTY_LDISC_HALTED, &tty->flags);
	n_tty_set_termios(tty, NULL);
	tty_unthrottle(tty);

	return 0;
err:
	return -ENOMEM;
}

static inline int input_available_p(struct tty_struct *tty, int amt)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (ldata->icanon && !L_EXTPROC(tty)) {
		if (ldata->canon_head != ldata->read_tail)
			return 1;
	} else if (read_cnt(ldata) >= (amt ? amt : 1))
		return 1;

	return 0;
}

/**
 *	copy_from_read_buf	-	copy read data directly
 *	@tty: terminal device
 *	@b: user data
 *	@nr: size of data
 *
 *	Helper function to speed up n_tty_read.  It is only called when
 *	ICANON is off; it copies characters straight from the tty queue to
 *	user space directly.  It can be profitably called twice; once to
 *	drain the space from the tail pointer to the (physical) end of the
 *	buffer, and once to drain the space from the (physical) beginning of
 *	the buffer to head pointer.
 *
 *	Called under the ldata->atomic_read_lock sem
 *
 *	n_tty_read()/consumer path:
 *		caller holds non-exclusive termios_rwsem
 *		read_tail published
 */

static int copy_from_read_buf(struct tty_struct *tty,
				      unsigned char __user **b,
				      size_t *nr)

{
	struct n_tty_data *ldata = tty->disc_data;
	int retval;
	size_t n;
	bool is_eof;
	size_t tail = ldata->read_tail & (N_TTY_BUF_SIZE - 1);

	retval = 0;
	n = min(read_cnt(ldata), N_TTY_BUF_SIZE - tail);
	n = min(*nr, n);
	if (n) {
		retval = copy_to_user(*b, read_buf_addr(ldata, tail), n);
		n -= retval;
		is_eof = n == 1 && read_buf(ldata, tail) == EOF_CHAR(tty);
		tty_audit_add_data(tty, read_buf_addr(ldata, tail), n,
				ldata->icanon);
		ldata->read_tail += n;
		/* Turn single EOF into zero-length read */
		if (L_EXTPROC(tty) && ldata->icanon && is_eof && !read_cnt(ldata))
			n = 0;
		*b += n;
		*nr -= n;
	}
	return retval;
}

/**
 *	canon_copy_from_read_buf	-	copy read data in canonical mode
 *	@tty: terminal device
 *	@b: user data
 *	@nr: size of data
 *
 *	Helper function for n_tty_read.  It is only called when ICANON is on;
 *	it copies one line of input up to and including the line-delimiting
 *	character into the user-space buffer.
 *
 *	Called under the atomic_read_lock mutex
 *
 *	n_tty_read()/consumer path:
 *		caller holds non-exclusive termios_rwsem
 *		read_tail published
 */

static int canon_copy_from_read_buf(struct tty_struct *tty,
				    unsigned char __user **b,
				    size_t *nr)
{
	struct n_tty_data *ldata = tty->disc_data;
	size_t n, size, more, c;
	size_t eol;
	size_t tail;
	int ret, found = 0;
	bool eof_push = 0;

	/* N.B. avoid overrun if nr == 0 */
	n = min(*nr, read_cnt(ldata));
	if (!n)
		return 0;

	tail = ldata->read_tail & (N_TTY_BUF_SIZE - 1);
	size = min_t(size_t, tail + n, N_TTY_BUF_SIZE);

	n_tty_trace("%s: nr:%zu tail:%zu n:%zu size:%zu\n",
		    __func__, *nr, tail, n, size);

	eol = find_next_bit(ldata->read_flags, size, tail);
	more = n - (size - tail);
	if (eol == N_TTY_BUF_SIZE && more) {
		/* scan wrapped without finding set bit */
		eol = find_next_bit(ldata->read_flags, more, 0);
		if (eol != more)
			found = 1;
	} else if (eol != size)
		found = 1;

	size = N_TTY_BUF_SIZE - tail;
	n = (found + eol + size) & (N_TTY_BUF_SIZE - 1);
	c = n;

	if (found && read_buf(ldata, eol) == __DISABLED_CHAR) {
		n--;
		eof_push = !n && ldata->read_tail != ldata->line_start;
	}

	n_tty_trace("%s: eol:%zu found:%d n:%zu c:%zu size:%zu more:%zu\n",
		    __func__, eol, found, n, c, size, more);

	if (n > size) {
		ret = copy_to_user(*b, read_buf_addr(ldata, tail), size);
		if (ret)
			return -EFAULT;
		ret = copy_to_user(*b + size, ldata->read_buf, n - size);
	} else
		ret = copy_to_user(*b, read_buf_addr(ldata, tail), n);

	if (ret)
		return -EFAULT;
	*b += n;
	*nr -= n;

	if (found)
		clear_bit(eol, ldata->read_flags);
	smp_mb__after_clear_bit();
	ldata->read_tail += c;

	if (found) {
		ldata->line_start = ldata->read_tail;
		tty_audit_push(tty);
	}
	return eof_push ? -EAGAIN : 0;
}

extern ssize_t redirected_tty_write(struct file *, const char __user *,
							size_t, loff_t *);

/**
 *	job_control		-	check job control
 *	@tty: tty
 *	@file: file handle
 *
 *	Perform job control management checks on this file/tty descriptor
 *	and if appropriate send any needed signals and return a negative
 *	error code if action should be taken.
 *
 *	Locking: redirected write test is safe
 *		 current->signal->tty check is safe
 *		 ctrl_lock to safely reference tty->pgrp
 */

static int job_control(struct tty_struct *tty, struct file *file)
{
	/* Job control check -- must be done at start and after
	   every sleep (POSIX.1 7.1.1.4). */
	/* NOTE: not yet done after every sleep pending a thorough
	   check of the logic of this change. -- jlc */
	/* don't stop on /dev/console */
	if (file->f_op->write == redirected_tty_write ||
	    current->signal->tty != tty)
		return 0;

	spin_lock_irq(&tty->ctrl_lock);
	if (!tty->pgrp)
		printk(KERN_ERR "n_tty_read: no tty->pgrp!\n");
	else if (task_pgrp(current) != tty->pgrp) {
		spin_unlock_irq(&tty->ctrl_lock);
		if (is_ignored(SIGTTIN) || is_current_pgrp_orphaned())
			return -EIO;
		kill_pgrp(task_pgrp(current), SIGTTIN, 1);
		set_thread_flag(TIF_SIGPENDING);
		return -ERESTARTSYS;
	}
	spin_unlock_irq(&tty->ctrl_lock);
	return 0;
}


/**
 *	n_tty_read		-	read function for tty
 *	@tty: tty device
 *	@file: file object
 *	@buf: userspace buffer pointer
 *	@nr: size of I/O
 *
 *	Perform reads for the line discipline. We are guaranteed that the
 *	line discipline will not be closed under us but we may get multiple
 *	parallel readers and must handle this ourselves. We may also get
 *	a hangup. Always called in user context, may sleep.
 *
 *	This code must be sure never to sleep through a hangup.
 *
 *	n_tty_read()/consumer path:
 *		claims non-exclusive termios_rwsem
 *		publishes read_tail
 */

static ssize_t n_tty_read(struct tty_struct *tty, struct file *file,
			 unsigned char __user *buf, size_t nr)
{
	struct n_tty_data *ldata = tty->disc_data;
	unsigned char __user *b = buf;
	DECLARE_WAITQUEUE(wait, current);
	int c;
	int minimum, time;
	ssize_t retval = 0;
	long timeout;
	unsigned long flags;
	int packet;

	c = job_control(tty, file);
	if (c < 0)
		return c;

	/*
	 *	Internal serialization of reads.
	 */
	if (file->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&ldata->atomic_read_lock))
			return -EAGAIN;
	} else {
		if (mutex_lock_interruptible(&ldata->atomic_read_lock))
			return -ERESTARTSYS;
	}

	down_read(&tty->termios_rwsem);

	minimum = time = 0;
	timeout = MAX_SCHEDULE_TIMEOUT;
	if (!ldata->icanon) {
		minimum = MIN_CHAR(tty);
		if (minimum) {
			time = (HZ / 10) * TIME_CHAR(tty);
			if (time)
				ldata->minimum_to_wake = 1;
			else if (!waitqueue_active(&tty->read_wait) ||
				 (ldata->minimum_to_wake > minimum))
				ldata->minimum_to_wake = minimum;
		} else {
			timeout = (HZ / 10) * TIME_CHAR(tty);
			ldata->minimum_to_wake = minimum = 1;
		}
	}

	packet = tty->packet;

	add_wait_queue(&tty->read_wait, &wait);
	while (nr) {
		/* First test for status change. */
		if (packet && tty->link->ctrl_status) {
			unsigned char cs;
			if (b != buf)
				break;
			spin_lock_irqsave(&tty->link->ctrl_lock, flags);
			cs = tty->link->ctrl_status;
			tty->link->ctrl_status = 0;
			spin_unlock_irqrestore(&tty->link->ctrl_lock, flags);
			if (tty_put_user(tty, cs, b++)) {
				retval = -EFAULT;
				b--;
				break;
			}
			nr--;
			break;
		}
		/* This statement must be first before checking for input
		   so that any interrupt will set the state back to
		   TASK_RUNNING. */
		set_current_state(TASK_INTERRUPTIBLE);

		if (((minimum - (b - buf)) < ldata->minimum_to_wake) &&
		    ((minimum - (b - buf)) >= 1))
			ldata->minimum_to_wake = (minimum - (b - buf));

		if (!input_available_p(tty, 0)) {
			if (test_bit(TTY_OTHER_CLOSED, &tty->flags)) {
				retval = -EIO;
				break;
			}
			if (tty_hung_up_p(file))
				break;
			if (!timeout)
				break;
			if (file->f_flags & O_NONBLOCK) {
				retval = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				retval = -ERESTARTSYS;
				break;
			}
			n_tty_set_room(tty);
			up_read(&tty->termios_rwsem);

			timeout = schedule_timeout(timeout);

			down_read(&tty->termios_rwsem);
			continue;
		}
		__set_current_state(TASK_RUNNING);

		/* Deal with packet mode. */
		if (packet && b == buf) {
			if (tty_put_user(tty, TIOCPKT_DATA, b++)) {
				retval = -EFAULT;
				b--;
				break;
			}
			nr--;
		}

		if (ldata->icanon && !L_EXTPROC(tty)) {
			retval = canon_copy_from_read_buf(tty, &b, &nr);
			if (retval == -EAGAIN) {
				retval = 0;
				continue;
			} else if (retval)
				break;
		} else {
			int uncopied;
			/* The copy function takes the read lock and handles
			   locking internally for this case */
			uncopied = copy_from_read_buf(tty, &b, &nr);
			uncopied += copy_from_read_buf(tty, &b, &nr);
			if (uncopied) {
				retval = -EFAULT;
				break;
			}
		}

		n_tty_check_unthrottle(tty);

		if (b - buf >= minimum)
			break;
		if (time)
			timeout = time;
	}
	mutex_unlock(&ldata->atomic_read_lock);
	remove_wait_queue(&tty->read_wait, &wait);

	if (!waitqueue_active(&tty->read_wait))
		ldata->minimum_to_wake = minimum;

	__set_current_state(TASK_RUNNING);
	if (b - buf)
		retval = b - buf;

	n_tty_set_room(tty);
	up_read(&tty->termios_rwsem);
	return retval;
}

/**
 *	n_tty_write		-	write function for tty
 *	@tty: tty device
 *	@file: file object
 *	@buf: userspace buffer pointer
 *	@nr: size of I/O
 *
 *	Write function of the terminal device.  This is serialized with
 *	respect to other write callers but not to termios changes, reads
 *	and other such events.  Since the receive code will echo characters,
 *	thus calling driver write methods, the output_lock is used in
 *	the output processing functions called here as well as in the
 *	echo processing function to protect the column state and space
 *	left in the buffer.
 *
 *	This code must be sure never to sleep through a hangup.
 *
 *	Locking: output_lock to protect column state and space left
 *		 (note that the process_output*() functions take this
 *		  lock themselves)
 */

static ssize_t n_tty_write(struct tty_struct *tty, struct file *file,
			   const unsigned char *buf, size_t nr)
{
	const unsigned char *b = buf;
	DECLARE_WAITQUEUE(wait, current);
	int c;
	ssize_t retval = 0;

	/* Job control check -- must be done at start (POSIX.1 7.1.1.4). */
	if (L_TOSTOP(tty) && file->f_op->write != redirected_tty_write) {
		retval = tty_check_change(tty);
		if (retval)
			return retval;
	}

	down_read(&tty->termios_rwsem);

	/* Write out any echoed characters that are still pending */
	process_echoes(tty);

	add_wait_queue(&tty->write_wait, &wait);
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		if (tty_hung_up_p(file) || (tty->link && !tty->link->count)) {
			retval = -EIO;
			break;
		}
		if (O_OPOST(tty)) {
			while (nr > 0) {
				ssize_t num = process_output_block(tty, b, nr);
				if (num < 0) {
					if (num == -EAGAIN)
						break;
					retval = num;
					goto break_out;
				}
				b += num;
				nr -= num;
				if (nr == 0)
					break;
				c = *b;
				if (process_output(c, tty) < 0)
					break;
				b++; nr--;
			}
			if (tty->ops->flush_chars)
				tty->ops->flush_chars(tty);
		} else {
			while (nr > 0) {
				c = tty->ops->write(tty, b, nr);
				if (c < 0) {
					retval = c;
					goto break_out;
				}
				if (!c)
					break;
				b += c;
				nr -= c;
			}
		}
		if (!nr)
			break;
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			break;
		}
		up_read(&tty->termios_rwsem);

		schedule();

		down_read(&tty->termios_rwsem);
	}
break_out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&tty->write_wait, &wait);
	if (b - buf != nr && tty->fasync)
		set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	up_read(&tty->termios_rwsem);
	return (b - buf) ? b - buf : retval;
}

/**
 *	n_tty_poll		-	poll method for N_TTY
 *	@tty: terminal device
 *	@file: file accessing it
 *	@wait: poll table
 *
 *	Called when the line discipline is asked to poll() for data or
 *	for special events. This code is not serialized with respect to
 *	other events save open/close.
 *
 *	This code must be sure never to sleep through a hangup.
 *	Called without the kernel lock held - fine
 */

static unsigned int n_tty_poll(struct tty_struct *tty, struct file *file,
							poll_table *wait)
{
	struct n_tty_data *ldata = tty->disc_data;
	unsigned int mask = 0;

	poll_wait(file, &tty->read_wait, wait);
	poll_wait(file, &tty->write_wait, wait);
	if (input_available_p(tty, TIME_CHAR(tty) ? 0 : MIN_CHAR(tty)))
		mask |= POLLIN | POLLRDNORM;
	if (tty->packet && tty->link->ctrl_status)
		mask |= POLLPRI | POLLIN | POLLRDNORM;
	if (test_bit(TTY_OTHER_CLOSED, &tty->flags))
		mask |= POLLHUP;
	if (tty_hung_up_p(file))
		mask |= POLLHUP;
	if (!(mask & (POLLHUP | POLLIN | POLLRDNORM))) {
		if (MIN_CHAR(tty) && !TIME_CHAR(tty))
			ldata->minimum_to_wake = MIN_CHAR(tty);
		else
			ldata->minimum_to_wake = 1;
	}
	if (tty->ops->write && !tty_is_writelocked(tty) &&
			tty_chars_in_buffer(tty) < WAKEUP_CHARS &&
			tty_write_room(tty) > 0)
		mask |= POLLOUT | POLLWRNORM;
	return mask;
}

static unsigned long inq_canon(struct n_tty_data *ldata)
{
	size_t nr, head, tail;

	if (ldata->canon_head == ldata->read_tail)
		return 0;
	head = ldata->canon_head;
	tail = ldata->read_tail;
	nr = head - tail;
	/* Skip EOF-chars.. */
	while (head != tail) {
		if (test_bit(tail & (N_TTY_BUF_SIZE - 1), ldata->read_flags) &&
		    read_buf(ldata, tail) == __DISABLED_CHAR)
			nr--;
		tail++;
	}
	return nr;
}

static int n_tty_ioctl(struct tty_struct *tty, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	struct n_tty_data *ldata = tty->disc_data;
	int retval;

	switch (cmd) {
	case TIOCOUTQ:
		return put_user(tty_chars_in_buffer(tty), (int __user *) arg);
	case TIOCINQ:
		down_write(&tty->termios_rwsem);
		if (L_ICANON(tty))
			retval = inq_canon(ldata);
		else
			retval = read_cnt(ldata);
		up_write(&tty->termios_rwsem);
		return put_user(retval, (unsigned int __user *) arg);
	default:
		return n_tty_ioctl_helper(tty, file, cmd, arg);
	}
}

static void n_tty_fasync(struct tty_struct *tty, int on)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (!waitqueue_active(&tty->read_wait)) {
		if (on)
			ldata->minimum_to_wake = 1;
		else if (!tty->fasync)
			ldata->minimum_to_wake = N_TTY_BUF_SIZE;
	}
}

struct tty_ldisc_ops tty_ldisc_N_TTY = {
	.magic           = TTY_LDISC_MAGIC,
	.name            = "n_tty",
	.open            = n_tty_open,
	.close           = n_tty_close,
	.flush_buffer    = n_tty_flush_buffer,
	.chars_in_buffer = n_tty_chars_in_buffer,
	.read            = n_tty_read,
	.write           = n_tty_write,
	.ioctl           = n_tty_ioctl,
	.set_termios     = n_tty_set_termios,
	.poll            = n_tty_poll,
	.receive_buf     = n_tty_receive_buf,
	.write_wakeup    = n_tty_write_wakeup,
	.fasync		 = n_tty_fasync,
	.receive_buf2	 = n_tty_receive_buf2,
};

/**
 *	n_tty_inherit_ops	-	inherit N_TTY methods
 *	@ops: struct tty_ldisc_ops where to save N_TTY methods
 *
 *	Enables a 'subclass' line discipline to 'inherit' N_TTY
 *	methods.
 */

void n_tty_inherit_ops(struct tty_ldisc_ops *ops)
{
	*ops = tty_ldisc_N_TTY;
	ops->owner = NULL;
	ops->refcount = ops->flags = 0;
}
EXPORT_SYMBOL_GPL(n_tty_inherit_ops);
