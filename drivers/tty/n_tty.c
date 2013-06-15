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

struct n_tty_data {
	unsigned int column;
	unsigned long overrun_time;
	int num_overrun;

	unsigned char lnext:1, erasing:1, raw:1, real_raw:1, icanon:1;
	unsigned char echo_overrun:1;

	DECLARE_BITMAP(process_char_map, 256);
	DECLARE_BITMAP(read_flags, N_TTY_BUF_SIZE);

	char *read_buf;
	int read_head;
	int read_tail;
	int read_cnt;
	int minimum_to_wake;

	unsigned char *echo_buf;
	unsigned int echo_pos;
	unsigned int echo_cnt;

	int canon_data;
	unsigned long canon_head;
	unsigned int canon_column;

	struct mutex atomic_read_lock;
	struct mutex output_lock;
	struct mutex echo_lock;
	raw_spinlock_t read_lock;
};

static inline int tty_put_user(struct tty_struct *tty, unsigned char x,
			       unsigned char __user *ptr)
{
	struct n_tty_data *ldata = tty->disc_data;

	tty_audit_add_data(tty, &x, 1, ldata->icanon);
	return put_user(x, ptr);
}

/**
 *	n_tty_set_room	-	receive space
 *	@tty: terminal
 *
 *	Sets tty->receive_room to reflect the currently available space
 *	in the input buffer, and re-schedules the flip buffer work if space
 *	just became available.
 *
 *	Locks: Concurrent update is protected with read_lock
 */

static void n_tty_set_room(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	int left;
	int old_left;
	unsigned long flags;

	raw_spin_lock_irqsave(&ldata->read_lock, flags);

	if (I_PARMRK(tty)) {
		/* Multiply read_cnt by 3, since each byte might take up to
		 * three times as many spaces when PARMRK is set (depending on
		 * its flags, e.g. parity error). */
		left = N_TTY_BUF_SIZE - ldata->read_cnt * 3 - 1;
	} else
		left = N_TTY_BUF_SIZE - ldata->read_cnt - 1;

	/*
	 * If we are doing input canonicalization, and there are no
	 * pending newlines, let characters through without limit, so
	 * that erase characters will be handled.  Other excess
	 * characters will be beeped.
	 */
	if (left <= 0)
		left = ldata->icanon && !ldata->canon_data;
	old_left = tty->receive_room;
	tty->receive_room = left;

	raw_spin_unlock_irqrestore(&ldata->read_lock, flags);

	/* Did this open up the receive buffer? We may need to flip */
	if (left && !old_left) {
		WARN_RATELIMIT(tty->port->itty == NULL,
				"scheduling with invalid itty\n");
		/* see if ldisc has been killed - if so, this means that
		 * even though the ldisc has been halted and ->buf.work
		 * cancelled, ->buf.work is about to be rescheduled
		 */
		WARN_RATELIMIT(test_bit(TTY_LDISC_HALTED, &tty->flags),
			       "scheduling buffer work for halted ldisc\n");
		schedule_work(&tty->port->buf.work);
	}
}

static void put_tty_queue_nolock(unsigned char c, struct n_tty_data *ldata)
{
	if (ldata->read_cnt < N_TTY_BUF_SIZE) {
		ldata->read_buf[ldata->read_head] = c;
		ldata->read_head = (ldata->read_head + 1) & (N_TTY_BUF_SIZE-1);
		ldata->read_cnt++;
	}
}

/**
 *	put_tty_queue		-	add character to tty
 *	@c: character
 *	@ldata: n_tty data
 *
 *	Add a character to the tty read_buf queue. This is done under the
 *	read_lock to serialize character addition and also to protect us
 *	against parallel reads or flushes
 */

static void put_tty_queue(unsigned char c, struct n_tty_data *ldata)
{
	unsigned long flags;
	/*
	 *	The problem of stomping on the buffers ends here.
	 *	Why didn't anyone see this one coming? --AJK
	*/
	raw_spin_lock_irqsave(&ldata->read_lock, flags);
	put_tty_queue_nolock(c, ldata);
	raw_spin_unlock_irqrestore(&ldata->read_lock, flags);
}

/**
 *	reset_buffer_flags	-	reset buffer state
 *	@tty: terminal to reset
 *
 *	Reset the read buffer counters and clear the flags.
 *	Called from n_tty_open() and n_tty_flush_buffer().
 *
 *	Locking: tty_read_lock for read fields.
 */

static void reset_buffer_flags(struct n_tty_data *ldata)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&ldata->read_lock, flags);
	ldata->read_head = ldata->read_tail = ldata->read_cnt = 0;
	raw_spin_unlock_irqrestore(&ldata->read_lock, flags);

	mutex_lock(&ldata->echo_lock);
	ldata->echo_pos = ldata->echo_cnt = ldata->echo_overrun = 0;
	mutex_unlock(&ldata->echo_lock);

	ldata->canon_head = ldata->canon_data = ldata->erasing = 0;
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
 *	Locking: ctrl_lock, read_lock.
 */

static void n_tty_flush_buffer(struct tty_struct *tty)
{
	reset_buffer_flags(tty->disc_data);
	n_tty_set_room(tty);

	if (tty->link)
		n_tty_packet_mode_flush(tty);
}

/**
 *	n_tty_chars_in_buffer	-	report available bytes
 *	@tty: tty device
 *
 *	Report the number of characters buffered to be delivered to user
 *	at this instant in time.
 *
 *	Locking: read_lock
 */

static ssize_t n_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	unsigned long flags;
	ssize_t n = 0;

	raw_spin_lock_irqsave(&ldata->read_lock, flags);
	if (!ldata->icanon) {
		n = ldata->read_cnt;
	} else if (ldata->canon_data) {
		n = (ldata->canon_head > ldata->read_tail) ?
			ldata->canon_head - ldata->read_tail :
			ldata->canon_head + (N_TTY_BUF_SIZE - ldata->read_tail);
	}
	raw_spin_unlock_irqrestore(&ldata->read_lock, flags);
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
 *	Locking: output_lock to protect column state and space left,
 *		 echo_lock to protect the echo buffer
 */

static void process_echoes(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	int	space, nr;
	unsigned char c;
	unsigned char *cp, *buf_end;

	if (!ldata->echo_cnt)
		return;

	mutex_lock(&ldata->output_lock);
	mutex_lock(&ldata->echo_lock);

	space = tty_write_room(tty);

	buf_end = ldata->echo_buf + N_TTY_BUF_SIZE;
	cp = ldata->echo_buf + ldata->echo_pos;
	nr = ldata->echo_cnt;
	while (nr > 0) {
		c = *cp;
		if (c == ECHO_OP_START) {
			unsigned char op;
			unsigned char *opp;
			int no_space_left = 0;

			/*
			 * If the buffer byte is the start of a multi-byte
			 * operation, get the next byte, which is either the
			 * op code or a control character value.
			 */
			opp = cp + 1;
			if (opp == buf_end)
				opp -= N_TTY_BUF_SIZE;
			op = *opp;

			switch (op) {
				unsigned int num_chars, num_bs;

			case ECHO_OP_ERASE_TAB:
				if (++opp == buf_end)
					opp -= N_TTY_BUF_SIZE;
				num_chars = *opp;

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
				cp += 3;
				nr -= 3;
				break;

			case ECHO_OP_SET_CANON_COL:
				ldata->canon_column = ldata->column;
				cp += 2;
				nr -= 2;
				break;

			case ECHO_OP_MOVE_BACK_COL:
				if (ldata->column > 0)
					ldata->column--;
				cp += 2;
				nr -= 2;
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
				cp += 2;
				nr -= 2;
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
				cp += 2;
				nr -= 2;
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
			cp += 1;
			nr -= 1;
		}

		/* When end of circular buffer reached, wrap around */
		if (cp >= buf_end)
			cp -= N_TTY_BUF_SIZE;
	}

	if (nr == 0) {
		ldata->echo_pos = 0;
		ldata->echo_cnt = 0;
		ldata->echo_overrun = 0;
	} else {
		int num_processed = ldata->echo_cnt - nr;
		ldata->echo_pos += num_processed;
		ldata->echo_pos &= N_TTY_BUF_SIZE - 1;
		ldata->echo_cnt = nr;
		if (num_processed > 0)
			ldata->echo_overrun = 0;
	}

	mutex_unlock(&ldata->echo_lock);
	mutex_unlock(&ldata->output_lock);

	if (tty->ops->flush_chars)
		tty->ops->flush_chars(tty);
}

/**
 *	add_echo_byte	-	add a byte to the echo buffer
 *	@c: unicode byte to echo
 *	@ldata: n_tty data
 *
 *	Add a character or operation byte to the echo buffer.
 *
 *	Should be called under the echo lock to protect the echo buffer.
 */

static void add_echo_byte(unsigned char c, struct n_tty_data *ldata)
{
	int	new_byte_pos;

	if (ldata->echo_cnt == N_TTY_BUF_SIZE) {
		/* Circular buffer is already at capacity */
		new_byte_pos = ldata->echo_pos;

		/*
		 * Since the buffer start position needs to be advanced,
		 * be sure to step by a whole operation byte group.
		 */
		if (ldata->echo_buf[ldata->echo_pos] == ECHO_OP_START) {
			if (ldata->echo_buf[(ldata->echo_pos + 1) &
					  (N_TTY_BUF_SIZE - 1)] ==
						ECHO_OP_ERASE_TAB) {
				ldata->echo_pos += 3;
				ldata->echo_cnt -= 2;
			} else {
				ldata->echo_pos += 2;
				ldata->echo_cnt -= 1;
			}
		} else {
			ldata->echo_pos++;
		}
		ldata->echo_pos &= N_TTY_BUF_SIZE - 1;

		ldata->echo_overrun = 1;
	} else {
		new_byte_pos = ldata->echo_pos + ldata->echo_cnt;
		new_byte_pos &= N_TTY_BUF_SIZE - 1;
		ldata->echo_cnt++;
	}

	ldata->echo_buf[new_byte_pos] = c;
}

/**
 *	echo_move_back_col	-	add operation to move back a column
 *	@ldata: n_tty data
 *
 *	Add an operation to the echo buffer to move back one column.
 *
 *	Locking: echo_lock to protect the echo buffer
 */

static void echo_move_back_col(struct n_tty_data *ldata)
{
	mutex_lock(&ldata->echo_lock);
	add_echo_byte(ECHO_OP_START, ldata);
	add_echo_byte(ECHO_OP_MOVE_BACK_COL, ldata);
	mutex_unlock(&ldata->echo_lock);
}

/**
 *	echo_set_canon_col	-	add operation to set the canon column
 *	@ldata: n_tty data
 *
 *	Add an operation to the echo buffer to set the canon column
 *	to the current column.
 *
 *	Locking: echo_lock to protect the echo buffer
 */

static void echo_set_canon_col(struct n_tty_data *ldata)
{
	mutex_lock(&ldata->echo_lock);
	add_echo_byte(ECHO_OP_START, ldata);
	add_echo_byte(ECHO_OP_SET_CANON_COL, ldata);
	mutex_unlock(&ldata->echo_lock);
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
 *
 *	Locking: echo_lock to protect the echo buffer
 */

static void echo_erase_tab(unsigned int num_chars, int after_tab,
			   struct n_tty_data *ldata)
{
	mutex_lock(&ldata->echo_lock);

	add_echo_byte(ECHO_OP_START, ldata);
	add_echo_byte(ECHO_OP_ERASE_TAB, ldata);

	/* We only need to know this modulo 8 (tab spacing) */
	num_chars &= 7;

	/* Set the high bit as a flag if num_chars is after a previous tab */
	if (after_tab)
		num_chars |= 0x80;

	add_echo_byte(num_chars, ldata);

	mutex_unlock(&ldata->echo_lock);
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
 *
 *	Locking: echo_lock to protect the echo buffer
 */

static void echo_char_raw(unsigned char c, struct n_tty_data *ldata)
{
	mutex_lock(&ldata->echo_lock);
	if (c == ECHO_OP_START) {
		add_echo_byte(ECHO_OP_START, ldata);
		add_echo_byte(ECHO_OP_START, ldata);
	} else {
		add_echo_byte(c, ldata);
	}
	mutex_unlock(&ldata->echo_lock);
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
 *
 *	Locking: echo_lock to protect the echo buffer
 */

static void echo_char(unsigned char c, struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	mutex_lock(&ldata->echo_lock);

	if (c == ECHO_OP_START) {
		add_echo_byte(ECHO_OP_START, ldata);
		add_echo_byte(ECHO_OP_START, ldata);
	} else {
		if (L_ECHOCTL(tty) && iscntrl(c) && c != '\t')
			add_echo_byte(ECHO_OP_START, ldata);
		add_echo_byte(c, ldata);
	}

	mutex_unlock(&ldata->echo_lock);
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
 *	Locking: read_lock for tty buffers
 */

static void eraser(unsigned char c, struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;
	enum { ERASE, WERASE, KILL } kill_type;
	int head, seen_alnums, cnt;
	unsigned long flags;

	/* FIXME: locking needed ? */
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
			raw_spin_lock_irqsave(&ldata->read_lock, flags);
			ldata->read_cnt -= ((ldata->read_head - ldata->canon_head) &
					  (N_TTY_BUF_SIZE - 1));
			ldata->read_head = ldata->canon_head;
			raw_spin_unlock_irqrestore(&ldata->read_lock, flags);
			return;
		}
		if (!L_ECHOK(tty) || !L_ECHOKE(tty) || !L_ECHOE(tty)) {
			raw_spin_lock_irqsave(&ldata->read_lock, flags);
			ldata->read_cnt -= ((ldata->read_head - ldata->canon_head) &
					  (N_TTY_BUF_SIZE - 1));
			ldata->read_head = ldata->canon_head;
			raw_spin_unlock_irqrestore(&ldata->read_lock, flags);
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
	/* FIXME: Locking ?? */
	while (ldata->read_head != ldata->canon_head) {
		head = ldata->read_head;

		/* erase a single possibly multibyte character */
		do {
			head = (head - 1) & (N_TTY_BUF_SIZE-1);
			c = ldata->read_buf[head];
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
		cnt = (ldata->read_head - head) & (N_TTY_BUF_SIZE-1);
		raw_spin_lock_irqsave(&ldata->read_lock, flags);
		ldata->read_head = head;
		ldata->read_cnt -= cnt;
		raw_spin_unlock_irqrestore(&ldata->read_lock, flags);
		if (L_ECHO(tty)) {
			if (L_ECHOPRT(tty)) {
				if (!ldata->erasing) {
					echo_char_raw('\\', ldata);
					ldata->erasing = 1;
				}
				/* if cnt > 1, output a multi-byte character */
				echo_char(c, tty);
				while (--cnt > 0) {
					head = (head+1) & (N_TTY_BUF_SIZE-1);
					echo_char_raw(ldata->read_buf[head],
							ldata);
					echo_move_back_col(ldata);
				}
			} else if (kill_type == ERASE && !L_ECHOE(tty)) {
				echo_char(ERASE_CHAR(tty), tty);
			} else if (c == '\t') {
				unsigned int num_chars = 0;
				int after_tab = 0;
				unsigned long tail = ldata->read_head;

				/*
				 * Count the columns used for characters
				 * since the start of input or after a
				 * previous tab.
				 * This info is used to go back the correct
				 * number of columns.
				 */
				while (tail != ldata->canon_head) {
					tail = (tail-1) & (N_TTY_BUF_SIZE-1);
					c = ldata->read_buf[tail];
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

static inline void isig(int sig, struct tty_struct *tty)
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
 *	Called from the receive_buf path so single threaded.
 */

static inline void n_tty_receive_break(struct tty_struct *tty)
{
	struct n_tty_data *ldata = tty->disc_data;

	if (I_IGNBRK(tty))
		return;
	if (I_BRKINT(tty)) {
		isig(SIGINT, tty);
		if (!L_NOFLSH(tty)) {
			n_tty_flush_buffer(tty);
			tty_driver_flush_buffer(tty);
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

static inline void n_tty_receive_overrun(struct tty_struct *tty)
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
 *	the error case if necessary. Locking as per n_tty_receive_buf.
 */
static inline void n_tty_receive_parity_error(struct tty_struct *tty,
					      unsigned char c)
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

/**
 *	n_tty_receive_char	-	perform processing
 *	@tty: terminal device
 *	@c: character
 *
 *	Process an individual character of input received from the driver.
 *	This is serialized with respect to itself by the rules for the
 *	driver above.
 */

static inline void n_tty_receive_char(struct tty_struct *tty, unsigned char c)
{
	struct n_tty_data *ldata = tty->disc_data;
	unsigned long flags;
	int parmrk;

	if (ldata->raw) {
		put_tty_queue(c, ldata);
		return;
	}

	if (I_ISTRIP(tty))
		c &= 0x7f;
	if (I_IUCLC(tty) && L_IEXTEN(tty))
		c = tolower(c);

	if (L_EXTPROC(tty)) {
		put_tty_queue(c, ldata);
		return;
	}

	if (tty->stopped && !tty->flow_stopped && I_IXON(tty) &&
	    I_IXANY(tty) && c != START_CHAR(tty) && c != STOP_CHAR(tty) &&
	    c != INTR_CHAR(tty) && c != QUIT_CHAR(tty) && c != SUSP_CHAR(tty)) {
		start_tty(tty);
		process_echoes(tty);
	}

	if (tty->closing) {
		if (I_IXON(tty)) {
			if (c == START_CHAR(tty)) {
				start_tty(tty);
				process_echoes(tty);
			} else if (c == STOP_CHAR(tty))
				stop_tty(tty);
		}
		return;
	}

	/*
	 * If the previous character was LNEXT, or we know that this
	 * character is not one of the characters that we'll have to
	 * handle specially, do shortcut processing to speed things
	 * up.
	 */
	if (!test_bit(c, ldata->process_char_map) || ldata->lnext) {
		ldata->lnext = 0;
		parmrk = (c == (unsigned char) '\377' && I_PARMRK(tty)) ? 1 : 0;
		if (ldata->read_cnt >= (N_TTY_BUF_SIZE - parmrk - 1)) {
			/* beep if no space */
			if (L_ECHO(tty))
				process_output('\a', tty);
			return;
		}
		if (L_ECHO(tty)) {
			finish_erasing(ldata);
			/* Record the column of first canon char. */
			if (ldata->canon_head == ldata->read_head)
				echo_set_canon_col(ldata);
			echo_char(c, tty);
			process_echoes(tty);
		}
		if (parmrk)
			put_tty_queue(c, ldata);
		put_tty_queue(c, ldata);
		return;
	}

	if (I_IXON(tty)) {
		if (c == START_CHAR(tty)) {
			start_tty(tty);
			process_echoes(tty);
			return;
		}
		if (c == STOP_CHAR(tty)) {
			stop_tty(tty);
			return;
		}
	}

	if (L_ISIG(tty)) {
		int signal;
		signal = SIGINT;
		if (c == INTR_CHAR(tty))
			goto send_signal;
		signal = SIGQUIT;
		if (c == QUIT_CHAR(tty))
			goto send_signal;
		signal = SIGTSTP;
		if (c == SUSP_CHAR(tty)) {
send_signal:
			if (!L_NOFLSH(tty)) {
				n_tty_flush_buffer(tty);
				tty_driver_flush_buffer(tty);
			}
			if (I_IXON(tty))
				start_tty(tty);
			if (L_ECHO(tty)) {
				echo_char(c, tty);
				process_echoes(tty);
			}
			isig(signal, tty);
			return;
		}
	}

	if (c == '\r') {
		if (I_IGNCR(tty))
			return;
		if (I_ICRNL(tty))
			c = '\n';
	} else if (c == '\n' && I_INLCR(tty))
		c = '\r';

	if (ldata->icanon) {
		if (c == ERASE_CHAR(tty) || c == KILL_CHAR(tty) ||
		    (c == WERASE_CHAR(tty) && L_IEXTEN(tty))) {
			eraser(c, tty);
			process_echoes(tty);
			return;
		}
		if (c == LNEXT_CHAR(tty) && L_IEXTEN(tty)) {
			ldata->lnext = 1;
			if (L_ECHO(tty)) {
				finish_erasing(ldata);
				if (L_ECHOCTL(tty)) {
					echo_char_raw('^', ldata);
					echo_char_raw('\b', ldata);
					process_echoes(tty);
				}
			}
			return;
		}
		if (c == REPRINT_CHAR(tty) && L_ECHO(tty) &&
		    L_IEXTEN(tty)) {
			unsigned long tail = ldata->canon_head;

			finish_erasing(ldata);
			echo_char(c, tty);
			echo_char_raw('\n', ldata);
			while (tail != ldata->read_head) {
				echo_char(ldata->read_buf[tail], tty);
				tail = (tail+1) & (N_TTY_BUF_SIZE-1);
			}
			process_echoes(tty);
			return;
		}
		if (c == '\n') {
			if (ldata->read_cnt >= N_TTY_BUF_SIZE) {
				if (L_ECHO(tty))
					process_output('\a', tty);
				return;
			}
			if (L_ECHO(tty) || L_ECHONL(tty)) {
				echo_char_raw('\n', ldata);
				process_echoes(tty);
			}
			goto handle_newline;
		}
		if (c == EOF_CHAR(tty)) {
			if (ldata->read_cnt >= N_TTY_BUF_SIZE)
				return;
			if (ldata->canon_head != ldata->read_head)
				set_bit(TTY_PUSH, &tty->flags);
			c = __DISABLED_CHAR;
			goto handle_newline;
		}
		if ((c == EOL_CHAR(tty)) ||
		    (c == EOL2_CHAR(tty) && L_IEXTEN(tty))) {
			parmrk = (c == (unsigned char) '\377' && I_PARMRK(tty))
				 ? 1 : 0;
			if (ldata->read_cnt >= (N_TTY_BUF_SIZE - parmrk)) {
				if (L_ECHO(tty))
					process_output('\a', tty);
				return;
			}
			/*
			 * XXX are EOL_CHAR and EOL2_CHAR echoed?!?
			 */
			if (L_ECHO(tty)) {
				/* Record the column of first canon char. */
				if (ldata->canon_head == ldata->read_head)
					echo_set_canon_col(ldata);
				echo_char(c, tty);
				process_echoes(tty);
			}
			/*
			 * XXX does PARMRK doubling happen for
			 * EOL_CHAR and EOL2_CHAR?
			 */
			if (parmrk)
				put_tty_queue(c, ldata);

handle_newline:
			raw_spin_lock_irqsave(&ldata->read_lock, flags);
			set_bit(ldata->read_head, ldata->read_flags);
			put_tty_queue_nolock(c, ldata);
			ldata->canon_head = ldata->read_head;
			ldata->canon_data++;
			raw_spin_unlock_irqrestore(&ldata->read_lock, flags);
			kill_fasync(&tty->fasync, SIGIO, POLL_IN);
			if (waitqueue_active(&tty->read_wait))
				wake_up_interruptible(&tty->read_wait);
			return;
		}
	}

	parmrk = (c == (unsigned char) '\377' && I_PARMRK(tty)) ? 1 : 0;
	if (ldata->read_cnt >= (N_TTY_BUF_SIZE - parmrk - 1)) {
		/* beep if no space */
		if (L_ECHO(tty))
			process_output('\a', tty);
		return;
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
		process_echoes(tty);
	}

	if (parmrk)
		put_tty_queue(c, ldata);

	put_tty_queue(c, ldata);
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
 */

static void n_tty_receive_buf(struct tty_struct *tty, const unsigned char *cp,
			      char *fp, int count)
{
	struct n_tty_data *ldata = tty->disc_data;
	const unsigned char *p;
	char *f, flags = TTY_NORMAL;
	int	i;
	char	buf[64];
	unsigned long cpuflags;

	if (ldata->real_raw) {
		raw_spin_lock_irqsave(&ldata->read_lock, cpuflags);
		i = min(N_TTY_BUF_SIZE - ldata->read_cnt,
			N_TTY_BUF_SIZE - ldata->read_head);
		i = min(count, i);
		memcpy(ldata->read_buf + ldata->read_head, cp, i);
		ldata->read_head = (ldata->read_head + i) & (N_TTY_BUF_SIZE-1);
		ldata->read_cnt += i;
		cp += i;
		count -= i;

		i = min(N_TTY_BUF_SIZE - ldata->read_cnt,
			N_TTY_BUF_SIZE - ldata->read_head);
		i = min(count, i);
		memcpy(ldata->read_buf + ldata->read_head, cp, i);
		ldata->read_head = (ldata->read_head + i) & (N_TTY_BUF_SIZE-1);
		ldata->read_cnt += i;
		raw_spin_unlock_irqrestore(&ldata->read_lock, cpuflags);
	} else {
		for (i = count, p = cp, f = fp; i; i--, p++) {
			if (f)
				flags = *f++;
			switch (flags) {
			case TTY_NORMAL:
				n_tty_receive_char(tty, *p);
				break;
			case TTY_BREAK:
				n_tty_receive_break(tty);
				break;
			case TTY_PARITY:
			case TTY_FRAME:
				n_tty_receive_parity_error(tty, *p);
				break;
			case TTY_OVERRUN:
				n_tty_receive_overrun(tty);
				break;
			default:
				printk(KERN_ERR "%s: unknown flag %d\n",
				       tty_name(tty, buf), flags);
				break;
			}
		}
		if (tty->ops->flush_chars)
			tty->ops->flush_chars(tty);
	}

	n_tty_set_room(tty);

	if ((!ldata->icanon && (ldata->read_cnt >= ldata->minimum_to_wake)) ||
		L_EXTPROC(tty)) {
		kill_fasync(&tty->fasync, SIGIO, POLL_IN);
		if (waitqueue_active(&tty->read_wait))
			wake_up_interruptible(&tty->read_wait);
	}

	/*
	 * Check the remaining room for the input canonicalization
	 * mode.  We don't want to throttle the driver if we're in
	 * canonical mode and don't have a newline yet!
	 */
	while (1) {
		tty_set_flow_change(tty, TTY_THROTTLE_SAFE);
		if (tty->receive_room >= TTY_THRESHOLD_THROTTLE)
			break;
		if (!tty_throttle_safe(tty))
			break;
	}
	__tty_set_flow_change(tty, 0);
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
 *	Locking: Caller holds tty->termios_mutex
 */

static void n_tty_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	struct n_tty_data *ldata = tty->disc_data;
	int canon_change = 1;

	if (old)
		canon_change = (old->c_lflag ^ tty->termios.c_lflag) & ICANON;
	if (canon_change) {
		bitmap_zero(ldata->read_flags, N_TTY_BUF_SIZE);
		ldata->canon_head = ldata->read_tail;
		ldata->canon_data = 0;
		ldata->erasing = 0;
	}

	if (canon_change && !L_ICANON(tty) && ldata->read_cnt)
		wake_up_interruptible(&tty->read_wait);

	ldata->icanon = (L_ICANON(tty) != 0);

	if (I_ISTRIP(tty) || I_IUCLC(tty) || I_IGNCR(tty) ||
	    I_ICRNL(tty) || I_INLCR(tty) || L_ICANON(tty) ||
	    I_IXON(tty) || L_ISIG(tty) || L_ECHO(tty) ||
	    I_PARMRK(tty)) {
		bitmap_zero(ldata->process_char_map, 256);

		if (I_IGNCR(tty) || I_ICRNL(tty))
			set_bit('\r', ldata->process_char_map);
		if (I_INLCR(tty))
			set_bit('\n', ldata->process_char_map);

		if (L_ICANON(tty)) {
			set_bit(ERASE_CHAR(tty), ldata->process_char_map);
			set_bit(KILL_CHAR(tty), ldata->process_char_map);
			set_bit(EOF_CHAR(tty), ldata->process_char_map);
			set_bit('\n', ldata->process_char_map);
			set_bit(EOL_CHAR(tty), ldata->process_char_map);
			if (L_IEXTEN(tty)) {
				set_bit(WERASE_CHAR(tty),
					ldata->process_char_map);
				set_bit(LNEXT_CHAR(tty),
					ldata->process_char_map);
				set_bit(EOL2_CHAR(tty),
					ldata->process_char_map);
				if (L_ECHO(tty))
					set_bit(REPRINT_CHAR(tty),
						ldata->process_char_map);
			}
		}
		if (I_IXON(tty)) {
			set_bit(START_CHAR(tty), ldata->process_char_map);
			set_bit(STOP_CHAR(tty), ldata->process_char_map);
		}
		if (L_ISIG(tty)) {
			set_bit(INTR_CHAR(tty), ldata->process_char_map);
			set_bit(QUIT_CHAR(tty), ldata->process_char_map);
			set_bit(SUSP_CHAR(tty), ldata->process_char_map);
		}
		clear_bit(__DISABLED_CHAR, ldata->process_char_map);
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

	kfree(ldata->read_buf);
	kfree(ldata->echo_buf);
	kfree(ldata);
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

	ldata = kzalloc(sizeof(*ldata), GFP_KERNEL);
	if (!ldata)
		goto err;

	ldata->overrun_time = jiffies;
	mutex_init(&ldata->atomic_read_lock);
	mutex_init(&ldata->output_lock);
	mutex_init(&ldata->echo_lock);
	raw_spin_lock_init(&ldata->read_lock);

	/* These are ugly. Currently a malloc failure here can panic */
	ldata->read_buf = kzalloc(N_TTY_BUF_SIZE, GFP_KERNEL);
	ldata->echo_buf = kzalloc(N_TTY_BUF_SIZE, GFP_KERNEL);
	if (!ldata->read_buf || !ldata->echo_buf)
		goto err_free_bufs;

	tty->disc_data = ldata;
	reset_buffer_flags(tty->disc_data);
	ldata->column = 0;
	ldata->minimum_to_wake = 1;
	tty->closing = 0;
	/* indicate buffer work may resume */
	clear_bit(TTY_LDISC_HALTED, &tty->flags);
	n_tty_set_termios(tty, NULL);
	tty_unthrottle(tty);

	return 0;
err_free_bufs:
	kfree(ldata->read_buf);
	kfree(ldata->echo_buf);
	kfree(ldata);
err:
	return -ENOMEM;
}

static inline int input_available_p(struct tty_struct *tty, int amt)
{
	struct n_tty_data *ldata = tty->disc_data;

	tty_flush_to_ldisc(tty);
	if (ldata->icanon && !L_EXTPROC(tty)) {
		if (ldata->canon_data)
			return 1;
	} else if (ldata->read_cnt >= (amt ? amt : 1))
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
 */

static int copy_from_read_buf(struct tty_struct *tty,
				      unsigned char __user **b,
				      size_t *nr)

{
	struct n_tty_data *ldata = tty->disc_data;
	int retval;
	size_t n;
	unsigned long flags;
	bool is_eof;

	retval = 0;
	raw_spin_lock_irqsave(&ldata->read_lock, flags);
	n = min(ldata->read_cnt, N_TTY_BUF_SIZE - ldata->read_tail);
	n = min(*nr, n);
	raw_spin_unlock_irqrestore(&ldata->read_lock, flags);
	if (n) {
		retval = copy_to_user(*b, &ldata->read_buf[ldata->read_tail], n);
		n -= retval;
		is_eof = n == 1 &&
			ldata->read_buf[ldata->read_tail] == EOF_CHAR(tty);
		tty_audit_add_data(tty, &ldata->read_buf[ldata->read_tail], n,
				ldata->icanon);
		raw_spin_lock_irqsave(&ldata->read_lock, flags);
		ldata->read_tail = (ldata->read_tail + n) & (N_TTY_BUF_SIZE-1);
		ldata->read_cnt -= n;
		/* Turn single EOF into zero-length read */
		if (L_EXTPROC(tty) && ldata->icanon && is_eof && !ldata->read_cnt)
			n = 0;
		raw_spin_unlock_irqrestore(&ldata->read_lock, flags);
		*b += n;
		*nr -= n;
	}
	return retval;
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
	ssize_t size;
	long timeout;
	unsigned long flags;
	int packet;

do_it_again:
	c = job_control(tty, file);
	if (c < 0)
		return c;

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
			timeout = schedule_timeout(timeout);
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
			/* N.B. avoid overrun if nr == 0 */
			raw_spin_lock_irqsave(&ldata->read_lock, flags);
			while (nr && ldata->read_cnt) {
				int eol;

				eol = test_and_clear_bit(ldata->read_tail,
						ldata->read_flags);
				c = ldata->read_buf[ldata->read_tail];
				ldata->read_tail = ((ldata->read_tail+1) &
						  (N_TTY_BUF_SIZE-1));
				ldata->read_cnt--;
				if (eol) {
					/* this test should be redundant:
					 * we shouldn't be reading data if
					 * canon_data is 0
					 */
					if (--ldata->canon_data < 0)
						ldata->canon_data = 0;
				}
				raw_spin_unlock_irqrestore(&ldata->read_lock, flags);

				if (!eol || (c != __DISABLED_CHAR)) {
					if (tty_put_user(tty, c, b++)) {
						retval = -EFAULT;
						b--;
						raw_spin_lock_irqsave(&ldata->read_lock, flags);
						break;
					}
					nr--;
				}
				if (eol) {
					tty_audit_push(tty);
					raw_spin_lock_irqsave(&ldata->read_lock, flags);
					break;
				}
				raw_spin_lock_irqsave(&ldata->read_lock, flags);
			}
			raw_spin_unlock_irqrestore(&ldata->read_lock, flags);
			if (retval)
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

		/* If there is enough space in the read buffer now, let the
		 * low-level driver know. We use n_tty_chars_in_buffer() to
		 * check the buffer, as it now knows about canonical mode.
		 * Otherwise, if the driver is throttled and the line is
		 * longer than TTY_THRESHOLD_UNTHROTTLE in canonical mode,
		 * we won't get any more characters.
		 */
		while (1) {
			tty_set_flow_change(tty, TTY_UNTHROTTLE_SAFE);
			if (n_tty_chars_in_buffer(tty) > TTY_THRESHOLD_UNTHROTTLE)
				break;
			if (!tty->count)
				break;
			n_tty_set_room(tty);
			if (!tty_unthrottle_safe(tty))
				break;
		}
		__tty_set_flow_change(tty, 0);

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
	size = b - buf;
	if (size) {
		retval = size;
		if (nr)
			clear_bit(TTY_PUSH, &tty->flags);
	} else if (test_and_clear_bit(TTY_PUSH, &tty->flags))
		goto do_it_again;

	n_tty_set_room(tty);
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
		schedule();
	}
break_out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&tty->write_wait, &wait);
	if (b - buf != nr && tty->fasync)
		set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
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
	int nr, head, tail;

	if (!ldata->canon_data)
		return 0;
	head = ldata->canon_head;
	tail = ldata->read_tail;
	nr = (head - tail) & (N_TTY_BUF_SIZE-1);
	/* Skip EOF-chars.. */
	while (head != tail) {
		if (test_bit(tail, ldata->read_flags) &&
		    ldata->read_buf[tail] == __DISABLED_CHAR)
			nr--;
		tail = (tail+1) & (N_TTY_BUF_SIZE-1);
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
		/* FIXME: Locking */
		retval = ldata->read_cnt;
		if (L_ICANON(tty))
			retval = inq_canon(ldata);
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
