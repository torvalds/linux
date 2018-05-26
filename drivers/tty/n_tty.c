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
	size_t commit_head;
	size_t canon_head;
	size_t echo_head;
	size_t echo_commit;
	size_t echo_mark;
	DECLARE_BITMAP(char_map, 256);

	/* private to n_tty_receive_overrun (single-threaded) */
	unsigned long overrun_time;
	int num_overrun;

	/* non-atomic */
	bool no_room;

	/* must hold exclusive termios_rwsem to reset these */
	unsigned char lnext:1, erasing:1, raw:1, real_raw:1, icanon:1;
	unsigned char push:1;

	/* shared by producer and consumer */
	char read_buf[N_TTY_BUF_SIZE];
	DECLARE_BITMAP(read_flags, N_TTY_BUF_SIZE);
	unsigned char echo_buf[N_TTY_BUF_SIZE];

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

#define MASK(x) ((x) & (N_TTY_BUF_SIZE - 1))

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
	smp_rmb(); /* Matches smp_wmb() in add_echo_byte(). */
	return ldata->echo_buf[i & (N_TTY_BUF_SIZE - 1)];
}

static inline unsigned char *echo_buf_addr(struct n_tty_data *ldata, size_t i)
{
	return &ldata->echo_buf[i & (N_TTY_BUF_SIZE - 1)];
}

static int tty_copy_to_user(struct tty_struct *tty, void __user *to,
			    size_t tail, size_t n)
{
	struct n_tty_data *ldata = tty->disc_data;
	size_t size = N_TTY_BUF_SIZE - tail;
	const void *from = read_buf_addr(ldata, tail);
	int uncopied;

	if (n > size) {
		tty_audit_add_data(tty, from, size);
		uncopied = copy_to_user(to, from, size);
		if (uncopied)
			return uncopied;
		to += size;
		n -= size;
		from = ldata->read_buf;
	}

	tty_audit_add_data(tty, from, n);
	return copy_to_user(to, from, n);
}

/**
 *	n_tty_kick_worker - start input worker (if required)
 *	@tty: terminal
 *
 *	Re-schedules the flip buffer work if it may have stopped
 *
 *	Caller holds exclusive termios_rwsem
 *	   or
 *	n_tty_read()/consumer path:
 *		holds non-exclusive termios_rwsem
 */

static void n_tty_kick_worker(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	/* Did the input worker stop? Restart it */
	if (unlikely(ldata->no_room)) {
		ldata->no_room = 0;

		WARN_RATELIMIT(tty->port->itty == NULL,
				"scheduling with invalid itty\n");
		/* see if ldisc has been killed - if so, this means that
		 * even though the ldisc has been halted and ->buf.work
		 * cancelled, ->buf.work is about to be rescheduled
		 */
		WARN_RATELIMIT(test_bit(TTY_LDISC_HALTED, &tty->flags),
			       "scheduling buffer work for halted ldisc\n");
		tty_buffer_restart_work(tty->port);
	}
}

static ssize_t chars_in_buffer(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	ssize_t n = 0;

	if (!ldata->icanon)
		n = ldata->commit_head - ldata->read_tail;
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
	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	kill_fasync(&tty->fasync, SIGIO, POLL_OUT);
}

static void n_tty_check_throttle(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	/*
	 * Check the remaining room for the input canonicalization
	 * mode.  We don't want to throttle the driver if we're in
	 * canonical mode and don't have a newline yet!
	 */
	if (ldata->icanon && ldata->canon_head == ldata->read_tail)
		return;

	while (1) {
		int throttled;
		tty_set_flow_change(tty, TTY_THROTTLE_SAFE);
		if (N_TTY_BUF_SIZE - read_cnt(ldata) >= TTY_THRESHOLD_THROTTLE)
			break;
		throttled = tty_throttle_safe(tty);
		if (!throttled)
			break;
	}
	__tty_set_flow_change(tty, 0);
}

static void n_tty_check_unthrottle(struct tty_struct *tty)
{
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY) {
		if (chars_in_buffer(tty) > TTY_THRESHOLD_UNTHROTTLE)
			return;
		n_tty_kick_worker(tty);
		tty_wakeup(tty->link);
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
		n_tty_kick_worker(tty);
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
 */

static inline void put_tty_queue(unsigned char c, struct n_tty_data *ldata)
{
	*read_buf_addr(ldata, ldata->read_head) = c;
	ldata->read_head++;
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
	ldata->commit_head = 0;
	ldata->line_start = 0;

	ldata->erasing = 0;
	bitmap_zero(ldata->read_flags, N_TTY_BUF_SIZE);
	ldata->push = 0;
}

static void n_tty_packet_mode_flush(struct tty_struct *tty)
{
	unsigned long flags;

	if (tty->link->packet) {
		spin_lock_irqsave(&tty->ctrl_lock, flags);
		tty->ctrl_status |= TIOCPKT_FLUSHREAD;
		spin_unlock_irqrestore(&tty->ctrl_lock, flags);
		wake_up_interruptible(&tty->link->read_wait);
	}
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
	n_tty_kick_worker(tty);

	if (tty->link)
		n_tty_packet_mode_flush(tty);
	up_write(&tty->termios_rwsem);
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
	while (MASK(ldata->echo_commit) != MASK(tail)) {
		c = echo_buf(ldata, tail);
		if (c == ECHO_OP_START) {
			unsigned char op;
			int no_space_left = 0;

			/*
			 * Since add_echo_byte() is called without holding
			 * output_lock, we might see only portion of multi-byte
			 * operation.
			 */
			if (MASK(ldata->echo_commit) == MASK(tail + 1))
				goto not_yet_stored;
			/*
			 * If the buffer byte is the start of a multi-byte
			 * operation, get the next byte, which is either the
			 * op code or a control character value.
			 */
			op = echo_buf(ldata, tail + 1);

			switch (op) {
				unsigned int num_chars, num_bs;

			case ECHO_OP_ERASE_TAB:
				if (MASK(ldata->echo_commit) == MASK(tail + 2))
					goto not_yet_stored;
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
	while (ldata->echo_commit > tail &&
	       ldata->echo_commit - tail >= ECHO_DISCARD_WATERMARK) {
		if (echo_buf(ldata, tail) == ECHO_OP_START) {
			if (echo_buf(ldata, tail + 1) == ECHO_OP_ERASE_TAB)
				tail += 3;
			else
				tail += 2;
		} else
			tail++;
	}

 not_yet_stored:
	ldata->echo_tail = tail;
	return old_space - space;
}

static void commit_echoes(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	size_t nr, old, echoed;
	size_t head;

	mutex_lock(&ldata->output_lock);
	head = ldata->echo_head;
	ldata->echo_mark = head;
	old = ldata->echo_commit - ldata->echo_tail;

	/* Process committed echoes if the accumulated # of bytes
	 * is over the threshold (and try again each time another
	 * block is accumulated) */
	nr = head - ldata->echo_tail;
	if (nr < ECHO_COMMIT_WATERMARK ||
	    (nr % ECHO_BLOCK > old % ECHO_BLOCK)) {
		mutex_unlock(&ldata->output_lock);
		return;
	}

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

	if (ldata->echo_mark == ldata->echo_tail)
		return;

	mutex_lock(&ldata->output_lock);
	ldata->echo_commit = ldata->echo_mark;
	echoed = __process_echoes(tty);
	mutex_unlock(&ldata->output_lock);

	if (echoed && tty->ops->flush_chars)
		tty->ops->flush_chars(tty);
}

/* NB: echo_mark and echo_head should be equivalent here */
static void flush_echoes(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	if ((!L_ECHO(tty) && !L_ECHONL(tty)) ||
	    ldata->echo_commit == ldata->echo_head)
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
	*echo_buf_addr(ldata, ldata->echo_head) = c;
	smp_wmb(); /* Matches smp_rmb() in echo_buf(). */
	ldata->echo_head++;
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
	while (MASK(ldata->read_head) != MASK(ldata->canon_head)) {
		head = ldata->read_head;

		/* erase a single possibly multibyte character */
		do {
			head--;
			c = read_buf(ldata, head);
		} while (is_continuation(c, tty) &&
			 MASK(head) != MASK(ldata->canon_head));

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
				while (MASK(tail) != MASK(ldata->canon_head)) {
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
 *	Performs input and output flush if !NOFLSH. In this context, the echo
 *	buffer is 'output'. The signal is processed first to alert any current
 *	readers or writers to discontinue and exit their i/o loops.
 *
 *	Locking: ctrl_lock
 */

static void __isig(int sig, struct tty_struct *tty)
{
	struct pid *tty_pgrp = tty_get_pgrp(tty);
	if (tty_pgrp) {
		kill_pgrp(tty_pgrp, sig, 1);
		put_pid(tty_pgrp);
	}
}

static void isig(int sig, struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (L_NOFLSH(tty)) {
		/* signal only */
		__isig(sig, tty);

	} else { /* signal and flush */
		up_read(&tty->termios_rwsem);
		down_write(&tty->termios_rwsem);

		__isig(sig, tty);

		/* clear echo buffer */
		mutex_lock(&ldata->output_lock);
		ldata->echo_head = ldata->echo_tail = 0;
		ldata->echo_mark = ldata->echo_commit = 0;
		mutex_unlock(&ldata->output_lock);

		/* clear output buffer */
		tty_driver_flush_buffer(tty);

		/* clear input buffer */
		reset_buffer_flags(tty->disc_data);

		/* notify pty master of flush */
		if (tty->link)
			n_tty_packet_mode_flush(tty);

		up_write(&tty->termios_rwsem);
		down_read(&tty->termios_rwsem);
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
		return;
	}
	if (I_PARMRK(tty)) {
		put_tty_queue('\377', ldata);
		put_tty_queue('\0', ldata);
	}
	put_tty_queue('\0', ldata);
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

	ldata->num_overrun++;
	if (time_after(jiffies, ldata->overrun_time + HZ) ||
			time_after(ldata->overrun_time, jiffies)) {
		tty_warn(tty, "%d input overrun(s)\n", ldata->num_overrun);
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
 */
static void n_tty_receive_parity_error(struct tty_struct *tty, unsigned char c)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (I_INPCK(tty)) {
		if (I_IGNPAR(tty))
			return;
		if (I_PARMRK(tty)) {
			put_tty_queue('\377', ldata);
			put_tty_queue('\0', ldata);
			put_tty_queue(c, ldata);
		} else
			put_tty_queue('\0', ldata);
	} else
		put_tty_queue(c, ldata);
}

static void
n_tty_receive_signal_char(struct tty_struct *tty, int signal, unsigned char c)
{
	isig(signal, tty);
	if (I_IXON(tty))
		start_tty(tty);
	if (L_ECHO(tty)) {
		echo_char(c, tty);
		commit_echoes(tty);
	} else
		process_echoes(tty);
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
 *
 *	Returns 1 if LNEXT was received, else returns 0
 */

static int
n_tty_receive_char_special(struct tty_struct *tty, unsigned char c)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (I_IXON(tty)) {
		if (c == START_CHAR(tty)) {
			start_tty(tty);
			process_echoes(tty);
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
			while (MASK(tail) != MASK(ldata->read_head)) {
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
			if (c == (unsigned char) '\377' && I_PARMRK(tty))
				put_tty_queue(c, ldata);

handle_newline:
			set_bit(ldata->read_head & (N_TTY_BUF_SIZE - 1), ldata->read_flags);
			put_tty_queue(c, ldata);
			smp_store_release(&ldata->canon_head, ldata->read_head);
			kill_fasync(&tty->fasync, SIGIO, POLL_IN);
			wake_up_interruptible_poll(&tty->read_wait, POLLIN);
			return 0;
		}
	}

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

	/* PARMRK doubling check */
	if (c == (unsigned char) '\377' && I_PARMRK(tty))
		put_tty_queue(c, ldata);

	put_tty_queue(c, ldata);
	return 0;
}

static inline void
n_tty_receive_char_inline(struct tty_struct *tty, unsigned char c)
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
	/* PARMRK doubling check */
	if (c == (unsigned char) '\377' && I_PARMRK(tty))
		put_tty_queue(c, ldata);
	put_tty_queue(c, ldata);
}

static void n_tty_receive_char(struct tty_struct *tty, unsigned char c)
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

static void n_tty_receive_char_closing(struct tty_struct *tty, unsigned char c)
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
		tty_err(tty, "unknown flag %d\n", flag);
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

static void
n_tty_receive_buf_real_raw(struct tty_struct *tty, const unsigned char *cp,
			   char *fp, int count)
{
	struct n_tty_data *ldata = tty->disc_data;
	size_t n, head;

	head = ldata->read_head & (N_TTY_BUF_SIZE - 1);
	n = min_t(size_t, count, N_TTY_BUF_SIZE - head);
	memcpy(read_buf_addr(ldata, head), cp, n);
	ldata->read_head += n;
	cp += n;
	count -= n;

	head = ldata->read_head & (N_TTY_BUF_SIZE - 1);
	n = min_t(size_t, count, N_TTY_BUF_SIZE - head);
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

	if (ldata->icanon && !L_EXTPROC(tty))
		return;

	/* publish read_head to consumer */
	smp_store_release(&ldata->commit_head, ldata->read_head);

	if (read_cnt(ldata)) {
		kill_fasync(&tty->fasync, SIGIO, POLL_IN);
		wake_up_interruptible_poll(&tty->read_wait, POLLIN);
	}
}

/**
 *	n_tty_receive_buf_common	-	process input
 *	@tty: device to receive input
 *	@cp: input chars
 *	@fp: flags for each char (if NULL, all chars are TTY_NORMAL)
 *	@count: number of input chars in @cp
 *
 *	Called by the terminal driver when a block of characters has
 *	been received. This function must be called from soft contexts
 *	not from interrupt context. The driver is responsible for making
 *	calls one at a time and in order (or using flush_to_ldisc)
 *
 *	Returns the # of input chars from @cp which were processed.
 *
 *	In canonical mode, the maximum line length is 4096 chars (including
 *	the line termination char); lines longer than 4096 chars are
 *	truncated. After 4095 chars, input data is still processed but
 *	not stored. Overflow processing ensures the tty can always
 *	receive more input until at least one line can be read.
 *
 *	In non-canonical mode, the read buffer will only accept 4095 chars;
 *	this provides the necessary space for a newline char if the input
 *	mode is switched to canonical.
 *
 *	Note it is possible for the read buffer to _contain_ 4096 chars
 *	in non-canonical mode: the read buffer could already contain the
 *	maximum canon line of 4096 chars when the mode is switched to
 *	non-canonical.
 *
 *	n_tty_receive_buf()/producer path:
 *		claims non-exclusive termios_rwsem
 *		publishes commit_head or canon_head
 */
static int
n_tty_receive_buf_common(struct tty_struct *tty, const unsigned char *cp,
			 char *fp, int count, int flow)
{
	struct n_tty_data *ldata = tty->disc_data;
	int room, n, rcvd = 0, overflow;

	down_read(&tty->termios_rwsem);

	while (1) {
		/*
		 * When PARMRK is set, each input char may take up to 3 chars
		 * in the read buf; reduce the buffer space avail by 3x
		 *
		 * If we are doing input canonicalization, and there are no
		 * pending newlines, let characters through without limit, so
		 * that erase characters will be handled.  Other excess
		 * characters will be beeped.
		 *
		 * paired with store in *_copy_from_read_buf() -- guarantees
		 * the consumer has loaded the data in read_buf up to the new
		 * read_tail (so this producer will not overwrite unread data)
		 */
		size_t tail = smp_load_acquire(&ldata->read_tail);

		room = N_TTY_BUF_SIZE - (ldata->read_head - tail);
		if (I_PARMRK(tty))
			room = (room + 2) / 3;
		room--;
		if (room <= 0) {
			overflow = ldata->icanon && ldata->canon_head == tail;
			if (overflow && room < 0)
				ldata->read_head--;
			room = overflow;
			ldata->no_room = flow && !room;
		} else
			overflow = 0;

		n = min(count, room);
		if (!n)
			break;

		/* ignore parity errors if handling overflow */
		if (!overflow || !fp || *fp != TTY_PARITY)
			__receive_buf(tty, cp, fp, n);

		cp += n;
		if (fp)
			fp += n;
		count -= n;
		rcvd += n;
	}

	tty->receive_room = room;

	/* Unthrottle if handling overflow on pty */
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY) {
		if (overflow) {
			tty_set_flow_change(tty, TTY_UNTHROTTLE_SAFE);
			tty_unthrottle_safe(tty);
			__tty_set_flow_change(tty, 0);
		}
	} else
		n_tty_check_throttle(tty);

	up_read(&tty->termios_rwsem);

	return rcvd;
}

static void n_tty_receive_buf(struct tty_struct *tty, const unsigned char *cp,
			      char *fp, int count)
{
	n_tty_receive_buf_common(tty, cp, fp, count, 0);
}

static int n_tty_receive_buf2(struct tty_struct *tty, const unsigned char *cp,
			      char *fp, int count)
{
	return n_tty_receive_buf_common(tty, cp, fp, count, 1);
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

	if (!old || (old->c_lflag ^ tty->termios.c_lflag) & (ICANON | EXTPROC)) {
		bitmap_zero(ldata->read_flags, N_TTY_BUF_SIZE);
		ldata->line_start = ldata->read_tail;
		if (!L_ICANON(tty) || !read_cnt(ldata)) {
			ldata->canon_head = ldata->read_tail;
			ldata->push = 0;
		} else {
			set_bit((ldata->read_head - 1) & (N_TTY_BUF_SIZE - 1),
				ldata->read_flags);
			ldata->canon_head = ldata->read_head;
			ldata->push = 1;
		}
		ldata->commit_head = ldata->read_head;
		ldata->erasing = 0;
		ldata->lnext = 0;
	}

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
	/*
	 * Fix tty hang when I_IXON(tty) is cleared, but the tty
	 * been stopped by STOP_CHAR(tty) before it.
	 */
	if (!I_IXON(tty) && old && (old->c_iflag & IXON) && !tty->flow_stopped) {
		start_tty(tty);
		process_echoes(tty);
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
	ldata = vzalloc(sizeof(*ldata));
	if (!ldata)
		return -ENOMEM;

	ldata->overrun_time = jiffies;
	mutex_init(&ldata->atomic_read_lock);
	mutex_init(&ldata->output_lock);

	tty->disc_data = ldata;
	tty->closing = 0;
	/* indicate buffer work may resume */
	clear_bit(TTY_LDISC_HALTED, &tty->flags);
	n_tty_set_termios(tty, NULL);
	tty_unthrottle(tty);
	return 0;
}

static inline int input_available_p(struct tty_struct *tty, int poll)
{
	struct n_tty_data *ldata = tty->disc_data;
	int amt = poll && !TIME_CHAR(tty) && MIN_CHAR(tty) ? MIN_CHAR(tty) : 1;

	if (ldata->icanon && !L_EXTPROC(tty))
		return ldata->canon_head != ldata->read_tail;
	else
		return ldata->commit_head - ldata->read_tail >= amt;
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
	size_t head = smp_load_acquire(&ldata->commit_head);
	size_t tail = ldata->read_tail & (N_TTY_BUF_SIZE - 1);

	retval = 0;
	n = min(head - ldata->read_tail, N_TTY_BUF_SIZE - tail);
	n = min(*nr, n);
	if (n) {
		const unsigned char *from = read_buf_addr(ldata, tail);
		retval = copy_to_user(*b, from, n);
		n -= retval;
		is_eof = n == 1 && *from == EOF_CHAR(tty);
		tty_audit_add_data(tty, from, n);
		smp_store_release(&ldata->read_tail, ldata->read_tail + n);
		/* Turn single EOF into zero-length read */
		if (L_EXTPROC(tty) && ldata->icanon && is_eof &&
		    (head == ldata->read_tail))
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
 *	NB: When termios is changed from non-canonical to canonical mode and
 *	the read buffer contains data, n_tty_set_termios() simulates an EOF
 *	push (as if C-d were input) _without_ the DISABLED_CHAR in the buffer.
 *	This causes data already processed as input to be immediately available
 *	as input although a newline has not been received.
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

	/* N.B. avoid overrun if nr == 0 */
	if (!*nr)
		return 0;

	n = min(*nr + 1, smp_load_acquire(&ldata->canon_head) - ldata->read_tail);

	tail = ldata->read_tail & (N_TTY_BUF_SIZE - 1);
	size = min_t(size_t, tail + n, N_TTY_BUF_SIZE);

	n_tty_trace("%s: nr:%zu tail:%zu n:%zu size:%zu\n",
		    __func__, *nr, tail, n, size);

	eol = find_next_bit(ldata->read_flags, size, tail);
	more = n - (size - tail);
	if (eol == N_TTY_BUF_SIZE && more) {
		/* scan wrapped without finding set bit */
		eol = find_next_bit(ldata->read_flags, more, 0);
		found = eol != more;
	} else
		found = eol != size;

	n = eol - tail;
	if (n > N_TTY_BUF_SIZE)
		n += N_TTY_BUF_SIZE;
	c = n + found;

	if (!found || read_buf(ldata, eol) != __DISABLED_CHAR) {
		c = min(*nr, c);
		n = c;
	}

	n_tty_trace("%s: eol:%zu found:%d n:%zu c:%zu tail:%zu more:%zu\n",
		    __func__, eol, found, n, c, tail, more);

	ret = tty_copy_to_user(tty, *b, tail, n);
	if (ret)
		return -EFAULT;
	*b += n;
	*nr -= n;

	if (found)
		clear_bit(eol, ldata->read_flags);
	smp_store_release(&ldata->read_tail, ldata->read_tail + c);

	if (found) {
		if (!ldata->push)
			ldata->line_start = ldata->read_tail;
		else
			ldata->push = 0;
		tty_audit_push();
	}
	return 0;
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
	if (file->f_op->write == redirected_tty_write)
		return 0;

	return __tty_check_change(tty, SIGTTIN);
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
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int c;
	int minimum, time;
	ssize_t retval = 0;
	long timeout;
	int packet;
	size_t tail;

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
		} else {
			timeout = (HZ / 10) * TIME_CHAR(tty);
			minimum = 1;
		}
	}

	packet = tty->packet;
	tail = ldata->read_tail;

	add_wait_queue(&tty->read_wait, &wait);
	while (nr) {
		/* First test for status change. */
		if (packet && tty->link->ctrl_status) {
			unsigned char cs;
			if (b != buf)
				break;
			spin_lock_irq(&tty->link->ctrl_lock);
			cs = tty->link->ctrl_status;
			tty->link->ctrl_status = 0;
			spin_unlock_irq(&tty->link->ctrl_lock);
			if (put_user(cs, b)) {
				retval = -EFAULT;
				break;
			}
			b++;
			nr--;
			break;
		}

		if (!input_available_p(tty, 0)) {
			up_read(&tty->termios_rwsem);
			tty_buffer_flush_work(tty->port);
			down_read(&tty->termios_rwsem);
			if (!input_available_p(tty, 0)) {
				if (test_bit(TTY_OTHER_CLOSED, &tty->flags)) {
					retval = -EIO;
					break;
				}
				if (tty_hung_up_p(file))
					break;
				/*
				 * Abort readers for ttys which never actually
				 * get hung up.  See __tty_hangup().
				 */
				if (test_bit(TTY_HUPPING, &tty->flags))
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
				up_read(&tty->termios_rwsem);

				timeout = wait_woken(&wait, TASK_INTERRUPTIBLE,
						timeout);

				down_read(&tty->termios_rwsem);
				continue;
			}
		}

		if (ldata->icanon && !L_EXTPROC(tty)) {
			retval = canon_copy_from_read_buf(tty, &b, &nr);
			if (retval)
				break;
		} else {
			int uncopied;

			/* Deal with packet mode. */
			if (packet && b == buf) {
				if (put_user(TIOCPKT_DATA, b)) {
					retval = -EFAULT;
					break;
				}
				b++;
				nr--;
			}

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
	if (tail != ldata->read_tail)
		n_tty_kick_worker(tty);
	up_read(&tty->termios_rwsem);

	remove_wait_queue(&tty->read_wait, &wait);
	mutex_unlock(&ldata->atomic_read_lock);

	if (b - buf)
		retval = b - buf;

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
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
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
			struct n_tty_data *ldata = tty->disc_data;

			while (nr > 0) {
				mutex_lock(&ldata->output_lock);
				c = tty->ops->write(tty, b, nr);
				mutex_unlock(&ldata->output_lock);
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

		wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);

		down_read(&tty->termios_rwsem);
	}
break_out:
	remove_wait_queue(&tty->write_wait, &wait);
	if (nr && tty->fasync)
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
	unsigned int mask = 0;

	poll_wait(file, &tty->read_wait, wait);
	poll_wait(file, &tty->write_wait, wait);
	if (input_available_p(tty, 1))
		mask |= POLLIN | POLLRDNORM;
	else {
		tty_buffer_flush_work(tty->port);
		if (input_available_p(tty, 1))
			mask |= POLLIN | POLLRDNORM;
	}
	if (tty->packet && tty->link->ctrl_status)
		mask |= POLLPRI | POLLIN | POLLRDNORM;
	if (test_bit(TTY_OTHER_CLOSED, &tty->flags))
		mask |= POLLHUP;
	if (tty_hung_up_p(file))
		mask |= POLLHUP;
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
	while (MASK(head) != MASK(tail)) {
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
		if (L_ICANON(tty) && !L_EXTPROC(tty))
			retval = inq_canon(ldata);
		else
			retval = read_cnt(ldata);
		up_write(&tty->termios_rwsem);
		return put_user(retval, (unsigned int __user *) arg);
	default:
		return n_tty_ioctl_helper(tty, file, cmd, arg);
	}
}

static struct tty_ldisc_ops n_tty_ops = {
	.magic           = TTY_LDISC_MAGIC,
	.name            = "n_tty",
	.open            = n_tty_open,
	.close           = n_tty_close,
	.flush_buffer    = n_tty_flush_buffer,
	.read            = n_tty_read,
	.write           = n_tty_write,
	.ioctl           = n_tty_ioctl,
	.set_termios     = n_tty_set_termios,
	.poll            = n_tty_poll,
	.receive_buf     = n_tty_receive_buf,
	.write_wakeup    = n_tty_write_wakeup,
	.receive_buf2	 = n_tty_receive_buf2,
};

/**
 *	n_tty_inherit_ops	-	inherit N_TTY methods
 *	@ops: struct tty_ldisc_ops where to save N_TTY methods
 *
 *	Enables a 'subclass' line discipline to 'inherit' N_TTY methods.
 */

void n_tty_inherit_ops(struct tty_ldisc_ops *ops)
{
	*ops = n_tty_ops;
	ops->owner = NULL;
	ops->refcount = ops->flags = 0;
}
EXPORT_SYMBOL_GPL(n_tty_inherit_ops);

void __init n_tty_init(void)
{
	tty_register_ldisc(N_TTY, &n_tty_ops);
}
