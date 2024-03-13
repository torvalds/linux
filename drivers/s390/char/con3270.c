// SPDX-License-Identifier: GPL-2.0
/*
 *    IBM/3270 Driver - tty functions.
 *
 *  Author(s):
 *    Original 3270 Code for 2.4 written by Richard Hitt (UTS Global)
 *    Rewritten for 2.5 by Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	-- Copyright IBM Corp. 2003
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/panic_notifier.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/compat.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/ebcdic.h>
#include <asm/cpcmd.h>
#include <linux/uaccess.h>

#include "raw3270.h"
#include "keyboard.h"

#define TTY3270_CHAR_BUF_SIZE 256
#define TTY3270_OUTPUT_BUFFER_SIZE 4096
#define TTY3270_SCREEN_PAGES 8 /* has to be power-of-two */
#define TTY3270_RECALL_SIZE 16 /* has to be power-of-two */
#define TTY3270_STATUS_AREA_SIZE 40

static struct tty_driver *tty3270_driver;
static int tty3270_max_index;
static struct raw3270_fn tty3270_fn;

#define TTY3270_HIGHLIGHT_BLINK		1
#define TTY3270_HIGHLIGHT_REVERSE	2
#define TTY3270_HIGHLIGHT_UNDERSCORE	4

struct tty3270_attribute {
	unsigned char alternate_charset:1;	/* Graphics charset */
	unsigned char highlight:3;		/* Blink/reverse/underscore */
	unsigned char f_color:4;		/* Foreground color */
	unsigned char b_color:4;		/* Background color */
};

struct tty3270_cell {
	u8 character;
	struct tty3270_attribute attributes;
};

struct tty3270_line {
	struct tty3270_cell *cells;
	int len;
	int dirty;
};

static const unsigned char sfq_read_partition[] = {
	0x00, 0x07, 0x01, 0xff, 0x03, 0x00, 0x81
};

#define ESCAPE_NPAR 8

/*
 * The main tty view data structure.
 * FIXME:
 * 1) describe line orientation & lines list concept against screen
 * 2) describe conversion of screen to lines
 * 3) describe line format.
 */
struct tty3270 {
	struct raw3270_view view;
	struct tty_port port;

	/* Output stuff. */
	unsigned char wcc;		/* Write control character. */
	int nr_up;			/* # lines up in history. */
	unsigned long update_flags;	/* Update indication bits. */
	struct raw3270_request *write;	/* Single write request. */
	struct timer_list timer;	/* Output delay timer. */
	char *converted_line;		/* RAW 3270 data stream */
	unsigned int line_view_start;	/* Start of visible area */
	unsigned int line_write_start;	/* current write position */
	unsigned int oops_line;		/* line counter used when print oops */

	/* Current tty screen. */
	unsigned int cx, cy;		/* Current output position. */
	struct tty3270_attribute attributes;
	struct tty3270_attribute saved_attributes;
	int allocated_lines;
	struct tty3270_line *screen;

	/* Input stuff. */
	char *prompt;			/* Output string for input area. */
	char *input;			/* Input string for read request. */
	struct raw3270_request *read;	/* Single read request. */
	struct raw3270_request *kreset;	/* Single keyboard reset request. */
	struct raw3270_request *readpartreq;
	unsigned char inattr;		/* Visible/invisible input. */
	int throttle, attn;		/* tty throttle/unthrottle. */
	struct tasklet_struct readlet;	/* Tasklet to issue read request. */
	struct tasklet_struct hanglet;	/* Tasklet to hang up the tty. */
	struct kbd_data *kbd;		/* key_maps stuff. */

	/* Escape sequence parsing. */
	int esc_state, esc_ques, esc_npar;
	int esc_par[ESCAPE_NPAR];
	unsigned int saved_cx, saved_cy;

	/* Command recalling. */
	char **rcl_lines;		/* Array of recallable lines */
	int rcl_write_index;		/* Write index of recallable items */
	int rcl_read_index;		/* Read index of recallable items */

	/* Character array for put_char/flush_chars. */
	unsigned int char_count;
	u8 char_buf[TTY3270_CHAR_BUF_SIZE];
};

/* tty3270->update_flags. See tty3270_update for details. */
#define TTY_UPDATE_INPUT	0x1	/* Update input line. */
#define TTY_UPDATE_STATUS	0x2	/* Update status line. */
#define TTY_UPDATE_LINES	0x4	/* Update visible screen lines */
#define TTY_UPDATE_ALL		0x7	/* Recreate screen. */

#define TTY3270_INPUT_AREA_ROWS 2

/*
 * Setup timeout for a device. On timeout trigger an update.
 */
static void tty3270_set_timer(struct tty3270 *tp, int expires)
{
	mod_timer(&tp->timer, jiffies + expires);
}

static int tty3270_tty_rows(struct tty3270 *tp)
{
	return tp->view.rows - TTY3270_INPUT_AREA_ROWS;
}

static char *tty3270_add_ba(struct tty3270 *tp, char *cp, char order, int x, int y)
{
	*cp++ = order;
	raw3270_buffer_address(tp->view.dev, cp, x, y);
	return cp + 2;
}

static char *tty3270_add_ra(struct tty3270 *tp, char *cp, int x, int y, char c)
{
	cp = tty3270_add_ba(tp, cp, TO_RA, x, y);
	*cp++ = c;
	return cp;
}

static char *tty3270_add_sa(struct tty3270 *tp, char *cp, char attr, char value)
{
	*cp++ = TO_SA;
	*cp++ = attr;
	*cp++ = value;
	return cp;
}

static char *tty3270_add_ge(struct tty3270 *tp, char *cp, char c)
{
	*cp++ = TO_GE;
	*cp++ = c;
	return cp;
}

static char *tty3270_add_sf(struct tty3270 *tp, char *cp, char type)
{
	*cp++ = TO_SF;
	*cp++ = type;
	return cp;
}

static int tty3270_line_increment(struct tty3270 *tp, unsigned int line, unsigned int incr)
{
	return (line + incr) & (tp->allocated_lines - 1);
}

static struct tty3270_line *tty3270_get_write_line(struct tty3270 *tp, unsigned int num)
{
	return tp->screen + tty3270_line_increment(tp, tp->line_write_start, num);
}

static struct tty3270_line *tty3270_get_view_line(struct tty3270 *tp, unsigned int num)
{
	return tp->screen + tty3270_line_increment(tp, tp->line_view_start, num - tp->nr_up);
}

static int tty3270_input_size(int cols)
{
	return cols * 2 - 11;
}

static void tty3270_update_prompt(struct tty3270 *tp, char *input)
{
	strcpy(tp->prompt, input);
	tp->update_flags |= TTY_UPDATE_INPUT;
	tty3270_set_timer(tp, 1);
}

/*
 * The input line are the two last lines of the screen.
 */
static int tty3270_add_prompt(struct tty3270 *tp)
{
	int count = 0;
	char *cp;

	cp = tp->converted_line;
	cp = tty3270_add_ba(tp, cp, TO_SBA, 0, -2);
	*cp++ = tp->view.ascebc['>'];

	if (*tp->prompt) {
		cp = tty3270_add_sf(tp, cp, TF_INMDT);
		count = min_t(int, strlen(tp->prompt),
			      tp->view.cols * 2 - TTY3270_STATUS_AREA_SIZE - 2);
		memcpy(cp, tp->prompt, count);
		cp += count;
	} else {
		cp = tty3270_add_sf(tp, cp, tp->inattr);
	}
	*cp++ = TO_IC;
	/* Clear to end of input line. */
	if (count < tp->view.cols * 2 - 11)
		cp = tty3270_add_ra(tp, cp, -TTY3270_STATUS_AREA_SIZE, -1, 0);
	return cp - tp->converted_line;
}

static char *tty3270_ebcdic_convert(struct tty3270 *tp, char *d, char *s)
{
	while (*s)
		*d++ = tp->view.ascebc[(int)*s++];
	return d;
}

/*
 * The status line is the last line of the screen. It shows the string
 * "Running"/"History X" in the lower right corner of the screen.
 */
static int tty3270_add_status(struct tty3270 *tp)
{
	char *cp = tp->converted_line;
	int len;

	cp = tty3270_add_ba(tp, cp, TO_SBA, -TTY3270_STATUS_AREA_SIZE, -1);
	cp = tty3270_add_sf(tp, cp, TF_LOG);
	cp = tty3270_add_sa(tp, cp, TAT_FGCOLOR, TAC_GREEN);
	cp = tty3270_ebcdic_convert(tp, cp, " 7");
	cp = tty3270_add_sa(tp, cp, TAT_EXTHI, TAX_REVER);
	cp = tty3270_ebcdic_convert(tp, cp, "PrevPg");
	cp = tty3270_add_sa(tp, cp, TAT_EXTHI, TAX_RESET);
	cp = tty3270_ebcdic_convert(tp, cp, " 8");
	cp = tty3270_add_sa(tp, cp, TAT_EXTHI, TAX_REVER);
	cp = tty3270_ebcdic_convert(tp, cp, "NextPg");
	cp = tty3270_add_sa(tp, cp, TAT_EXTHI, TAX_RESET);
	cp = tty3270_ebcdic_convert(tp, cp, " 12");
	cp = tty3270_add_sa(tp, cp, TAT_EXTHI, TAX_REVER);
	cp = tty3270_ebcdic_convert(tp, cp, "Recall");
	cp = tty3270_add_sa(tp, cp, TAT_EXTHI, TAX_RESET);
	cp = tty3270_ebcdic_convert(tp, cp, "  ");
	if (tp->nr_up) {
		len = sprintf(cp, "History %d", -tp->nr_up);
		codepage_convert(tp->view.ascebc, cp, len);
		cp += len;
	} else {
		cp = tty3270_ebcdic_convert(tp, cp, oops_in_progress ? "Crashed" : "Running");
	}
	cp = tty3270_add_sf(tp, cp, TF_LOG);
	cp = tty3270_add_sa(tp, cp, TAT_FGCOLOR, TAC_RESET);
	return cp - (char *)tp->converted_line;
}

static void tty3270_blank_screen(struct tty3270 *tp)
{
	struct tty3270_line *line;
	int i;

	for (i = 0; i < tty3270_tty_rows(tp); i++) {
		line = tty3270_get_write_line(tp, i);
		line->len = 0;
		line->dirty = 1;
	}
	tp->nr_up = 0;
}

/*
 * Write request completion callback.
 */
static void tty3270_write_callback(struct raw3270_request *rq, void *data)
{
	struct tty3270 *tp = container_of(rq->view, struct tty3270, view);

	if (rq->rc != 0) {
		/* Write wasn't successful. Refresh all. */
		tp->update_flags = TTY_UPDATE_ALL;
		tty3270_set_timer(tp, 1);
	}
	raw3270_request_reset(rq);
	xchg(&tp->write, rq);
}

static int tty3270_required_length(struct tty3270 *tp, struct tty3270_line *line)
{
	unsigned char f_color, b_color, highlight;
	struct tty3270_cell *cell;
	int i, flen = 3;		/* Prefix (TO_SBA). */

	flen += line->len;
	highlight = 0;
	f_color = TAC_RESET;
	b_color = TAC_RESET;

	for (i = 0, cell = line->cells; i < line->len; i++, cell++) {
		if (cell->attributes.highlight != highlight) {
			flen += 3;	/* TO_SA to switch highlight. */
			highlight = cell->attributes.highlight;
		}
		if (cell->attributes.f_color != f_color) {
			flen += 3;	/* TO_SA to switch color. */
			f_color = cell->attributes.f_color;
		}
		if (cell->attributes.b_color != b_color) {
			flen += 3;	/* TO_SA to switch color. */
			b_color = cell->attributes.b_color;
		}
		if (cell->attributes.alternate_charset)
			flen += 1;	/* TO_GE to switch to graphics extensions */
	}
	if (highlight)
		flen += 3;	/* TO_SA to reset hightlight. */
	if (f_color != TAC_RESET)
		flen += 3;	/* TO_SA to reset color. */
	if (b_color != TAC_RESET)
		flen += 3;	/* TO_SA to reset color. */
	if (line->len < tp->view.cols)
		flen += 4;	/* Postfix (TO_RA). */

	return flen;
}

static char *tty3270_add_reset_attributes(struct tty3270 *tp, struct tty3270_line *line,
					  char *cp, struct tty3270_attribute *attr, int lineno)
{
	if (attr->highlight)
		cp = tty3270_add_sa(tp, cp, TAT_EXTHI, TAX_RESET);
	if (attr->f_color != TAC_RESET)
		cp = tty3270_add_sa(tp, cp, TAT_FGCOLOR, TAX_RESET);
	if (attr->b_color != TAC_RESET)
		cp = tty3270_add_sa(tp, cp, TAT_BGCOLOR, TAX_RESET);
	if (line->len < tp->view.cols)
		cp = tty3270_add_ra(tp, cp, 0, lineno + 1, 0);
	return cp;
}

static char tty3270_graphics_translate(struct tty3270 *tp, char ch)
{
	switch (ch) {
	case 'q': /* - */
		return 0xa2;
	case 'x': /* '|' */
		return 0x85;
	case 'l': /* |- */
		return 0xc5;
	case 't': /* |_ */
		return 0xc6;
	case 'u': /* _| */
		return 0xd6;
	case 'k': /* -| */
		return 0xd5;
	case 'j':
		return 0xd4;
	case 'm':
		return 0xc4;
	case 'n': /* + */
		return 0xd3;
	case 'v':
		return 0xc7;
	case 'w':
		return 0xd7;
	default:
		return ch;
	}
}

static char *tty3270_add_attributes(struct tty3270 *tp, struct tty3270_line *line,
				    struct tty3270_attribute *attr, char *cp, int lineno)
{
	const unsigned char colors[16] = {
		[0] = TAC_DEFAULT,
		[1] = TAC_RED,
		[2] = TAC_GREEN,
		[3] = TAC_YELLOW,
		[4] = TAC_BLUE,
		[5] = TAC_PINK,
		[6] = TAC_TURQ,
		[7] = TAC_WHITE,
		[9] = TAC_DEFAULT
	};

	const unsigned char highlights[8] = {
		[TTY3270_HIGHLIGHT_BLINK] = TAX_BLINK,
		[TTY3270_HIGHLIGHT_REVERSE] = TAX_REVER,
		[TTY3270_HIGHLIGHT_UNDERSCORE] = TAX_UNDER,
	};

	struct tty3270_cell *cell;
	int c, i;

	cp = tty3270_add_ba(tp, cp, TO_SBA, 0, lineno);

	for (i = 0, cell = line->cells; i < line->len; i++, cell++) {
		if (cell->attributes.highlight != attr->highlight) {
			attr->highlight = cell->attributes.highlight;
			cp = tty3270_add_sa(tp, cp, TAT_EXTHI, highlights[attr->highlight]);
		}
		if (cell->attributes.f_color != attr->f_color) {
			attr->f_color = cell->attributes.f_color;
			cp = tty3270_add_sa(tp, cp, TAT_FGCOLOR, colors[attr->f_color]);
		}
		if (cell->attributes.b_color != attr->b_color) {
			attr->b_color = cell->attributes.b_color;
			cp = tty3270_add_sa(tp, cp, TAT_BGCOLOR, colors[attr->b_color]);
		}
		c = cell->character;
		if (cell->attributes.alternate_charset)
			cp = tty3270_add_ge(tp, cp, tty3270_graphics_translate(tp, c));
		else
			*cp++ = tp->view.ascebc[c];
	}
	return cp;
}

static void tty3270_reset_attributes(struct tty3270_attribute *attr)
{
	attr->highlight = TAX_RESET;
	attr->f_color = TAC_RESET;
	attr->b_color = TAC_RESET;
}

/*
 * Convert a tty3270_line to a 3270 data fragment usable for output.
 */
static unsigned int tty3270_convert_line(struct tty3270 *tp, struct tty3270_line *line, int lineno)
{
	struct tty3270_attribute attr;
	int flen;
	char *cp;

	/* Determine how long the fragment will be. */
	flen = tty3270_required_length(tp, line);
	if (flen > PAGE_SIZE)
		return 0;
	/* Write 3270 data fragment. */
	tty3270_reset_attributes(&attr);
	cp = tty3270_add_attributes(tp, line, &attr, tp->converted_line, lineno);
	cp = tty3270_add_reset_attributes(tp, line, cp, &attr, lineno);
	return cp - (char *)tp->converted_line;
}

static void tty3270_update_lines_visible(struct tty3270 *tp, struct raw3270_request *rq)
{
	struct tty3270_line *line;
	int len, i;

	for (i = 0; i < tty3270_tty_rows(tp); i++) {
		line = tty3270_get_view_line(tp, i);
		if (!line->dirty)
			continue;
		len = tty3270_convert_line(tp, line, i);
		if (raw3270_request_add_data(rq, tp->converted_line, len))
			break;
		line->dirty = 0;
	}
	if (i == tty3270_tty_rows(tp)) {
		for (i = 0; i < tp->allocated_lines; i++)
			tp->screen[i].dirty = 0;
		tp->update_flags &= ~TTY_UPDATE_LINES;
	}
}

static void tty3270_update_lines_all(struct tty3270 *tp, struct raw3270_request *rq)
{
	struct tty3270_line *line;
	char buf[4];
	int len, i;

	for (i = 0; i < tp->allocated_lines; i++) {
		line = tty3270_get_write_line(tp, i + tp->cy + 1);
		if (!line->dirty)
			continue;
		len = tty3270_convert_line(tp, line, tp->oops_line);
		if (raw3270_request_add_data(rq, tp->converted_line, len))
			break;
		line->dirty = 0;
		if (++tp->oops_line >= tty3270_tty_rows(tp))
			tp->oops_line = 0;
	}

	if (i == tp->allocated_lines) {
		if (tp->oops_line < tty3270_tty_rows(tp)) {
			tty3270_add_ra(tp, buf, 0, tty3270_tty_rows(tp), 0);
			if (raw3270_request_add_data(rq, buf, sizeof(buf)))
				return;
		}
		tp->update_flags &= ~TTY_UPDATE_LINES;
	}
}

/*
 * Update 3270 display.
 */
static void tty3270_update(struct timer_list *t)
{
	struct tty3270 *tp = from_timer(tp, t, timer);
	struct raw3270_request *wrq;
	u8 cmd = TC_WRITE;
	int rc, len;

	wrq = xchg(&tp->write, 0);
	if (!wrq) {
		tty3270_set_timer(tp, 1);
		return;
	}

	spin_lock_irq(&tp->view.lock);
	if (tp->update_flags == TTY_UPDATE_ALL)
		cmd = TC_EWRITEA;

	raw3270_request_set_cmd(wrq, cmd);
	raw3270_request_add_data(wrq, &tp->wcc, 1);
	tp->wcc = TW_NONE;

	/*
	 * Update status line.
	 */
	if (tp->update_flags & TTY_UPDATE_STATUS) {
		len = tty3270_add_status(tp);
		if (raw3270_request_add_data(wrq, tp->converted_line, len) == 0)
			tp->update_flags &= ~TTY_UPDATE_STATUS;
	}

	/*
	 * Write input line.
	 */
	if (tp->update_flags & TTY_UPDATE_INPUT) {
		len = tty3270_add_prompt(tp);
		if (raw3270_request_add_data(wrq, tp->converted_line, len) == 0)
			tp->update_flags &= ~TTY_UPDATE_INPUT;
	}

	if (tp->update_flags & TTY_UPDATE_LINES) {
		if (oops_in_progress)
			tty3270_update_lines_all(tp, wrq);
		else
			tty3270_update_lines_visible(tp, wrq);
	}

	wrq->callback = tty3270_write_callback;
	rc = raw3270_start(&tp->view, wrq);
	if (rc == 0) {
		if (tp->update_flags)
			tty3270_set_timer(tp, 1);
	} else {
		raw3270_request_reset(wrq);
		xchg(&tp->write, wrq);
	}
	spin_unlock_irq(&tp->view.lock);
}

/*
 * Command recalling.
 */
static void tty3270_rcl_add(struct tty3270 *tp, char *input, int len)
{
	char *p;

	if (len <= 0)
		return;
	p = tp->rcl_lines[tp->rcl_write_index++];
	tp->rcl_write_index &= TTY3270_RECALL_SIZE - 1;
	memcpy(p, input, len);
	p[len] = '\0';
	tp->rcl_read_index = tp->rcl_write_index;
}

static void tty3270_rcl_backward(struct kbd_data *kbd)
{
	struct tty3270 *tp = container_of(kbd->port, struct tty3270, port);
	int i = 0;

	spin_lock_irq(&tp->view.lock);
	if (tp->inattr == TF_INPUT) {
		do {
			tp->rcl_read_index--;
			tp->rcl_read_index &= TTY3270_RECALL_SIZE - 1;
		} while (!*tp->rcl_lines[tp->rcl_read_index] &&
			 i++ < TTY3270_RECALL_SIZE - 1);
		tty3270_update_prompt(tp, tp->rcl_lines[tp->rcl_read_index]);
	}
	spin_unlock_irq(&tp->view.lock);
}

/*
 * Deactivate tty view.
 */
static void tty3270_exit_tty(struct kbd_data *kbd)
{
	struct tty3270 *tp = container_of(kbd->port, struct tty3270, port);

	raw3270_deactivate_view(&tp->view);
}

static void tty3270_redraw(struct tty3270 *tp)
{
	int i;

	for (i = 0; i < tty3270_tty_rows(tp); i++)
		tty3270_get_view_line(tp, i)->dirty = 1;
	tp->update_flags = TTY_UPDATE_ALL;
	tty3270_set_timer(tp, 1);
}

/*
 * Scroll forward in history.
 */
static void tty3270_scroll_forward(struct kbd_data *kbd)
{
	struct tty3270 *tp = container_of(kbd->port, struct tty3270, port);

	spin_lock_irq(&tp->view.lock);

	if (tp->nr_up >= tty3270_tty_rows(tp))
		tp->nr_up -= tty3270_tty_rows(tp) / 2;
	else
		tp->nr_up = 0;
	tty3270_redraw(tp);
	spin_unlock_irq(&tp->view.lock);
}

/*
 * Scroll backward in history.
 */
static void tty3270_scroll_backward(struct kbd_data *kbd)
{
	struct tty3270 *tp = container_of(kbd->port, struct tty3270, port);

	spin_lock_irq(&tp->view.lock);
	tp->nr_up += tty3270_tty_rows(tp) / 2;
	if (tp->nr_up > tp->allocated_lines - tty3270_tty_rows(tp))
		tp->nr_up = tp->allocated_lines - tty3270_tty_rows(tp);
	tty3270_redraw(tp);
	spin_unlock_irq(&tp->view.lock);
}

/*
 * Pass input line to tty.
 */
static void tty3270_read_tasklet(unsigned long data)
{
	struct raw3270_request *rrq = (struct raw3270_request *)data;
	static char kreset_data = TW_KR;
	struct tty3270 *tp = container_of(rrq->view, struct tty3270, view);
	char *input;
	int len;

	spin_lock_irq(&tp->view.lock);
	/*
	 * Two AID keys are special: For 0x7d (enter) the input line
	 * has to be emitted to the tty and for 0x6d the screen
	 * needs to be redrawn.
	 */
	input = NULL;
	len = 0;
	switch (tp->input[0]) {
	case AID_ENTER:
		/* Enter: write input to tty. */
		input = tp->input + 6;
		len = tty3270_input_size(tp->view.cols) - 6 - rrq->rescnt;
		if (tp->inattr != TF_INPUTN)
			tty3270_rcl_add(tp, input, len);
		if (tp->nr_up > 0)
			tp->nr_up = 0;
		/* Clear input area. */
		tty3270_update_prompt(tp, "");
		tty3270_set_timer(tp, 1);
		break;
	case AID_CLEAR:
		/* Display has been cleared. Redraw. */
		tp->update_flags = TTY_UPDATE_ALL;
		tty3270_set_timer(tp, 1);
		if (!list_empty(&tp->readpartreq->list))
			break;
		raw3270_start_request(&tp->view, tp->readpartreq, TC_WRITESF,
				      (char *)sfq_read_partition, sizeof(sfq_read_partition));
		break;
	case AID_READ_PARTITION:
		raw3270_read_modified_cb(tp->readpartreq, tp->input);
		break;
	default:
		break;
	}
	spin_unlock_irq(&tp->view.lock);

	/* Start keyboard reset command. */
	raw3270_start_request(&tp->view, tp->kreset, TC_WRITE, &kreset_data, 1);

	while (len-- > 0)
		kbd_keycode(tp->kbd, *input++);
	/* Emit keycode for AID byte. */
	kbd_keycode(tp->kbd, 256 + tp->input[0]);

	raw3270_request_reset(rrq);
	xchg(&tp->read, rrq);
	raw3270_put_view(&tp->view);
}

/*
 * Read request completion callback.
 */
static void tty3270_read_callback(struct raw3270_request *rq, void *data)
{
	struct tty3270 *tp = container_of(rq->view, struct tty3270, view);

	raw3270_get_view(rq->view);
	/* Schedule tasklet to pass input to tty. */
	tasklet_schedule(&tp->readlet);
}

/*
 * Issue a read request. Call with device lock.
 */
static void tty3270_issue_read(struct tty3270 *tp, int lock)
{
	struct raw3270_request *rrq;
	int rc;

	rrq = xchg(&tp->read, 0);
	if (!rrq)
		/* Read already scheduled. */
		return;
	rrq->callback = tty3270_read_callback;
	rrq->callback_data = tp;
	raw3270_request_set_cmd(rrq, TC_READMOD);
	raw3270_request_set_data(rrq, tp->input, tty3270_input_size(tp->view.cols));
	/* Issue the read modified request. */
	if (lock)
		rc = raw3270_start(&tp->view, rrq);
	else
		rc = raw3270_start_irq(&tp->view, rrq);
	if (rc) {
		raw3270_request_reset(rrq);
		xchg(&tp->read, rrq);
	}
}

/*
 * Hang up the tty
 */
static void tty3270_hangup_tasklet(unsigned long data)
{
	struct tty3270 *tp = (struct tty3270 *)data;

	tty_port_tty_hangup(&tp->port, true);
	raw3270_put_view(&tp->view);
}

/*
 * Switch to the tty view.
 */
static int tty3270_activate(struct raw3270_view *view)
{
	struct tty3270 *tp = container_of(view, struct tty3270, view);

	tp->update_flags = TTY_UPDATE_ALL;
	tty3270_set_timer(tp, 1);
	return 0;
}

static void tty3270_deactivate(struct raw3270_view *view)
{
	struct tty3270 *tp = container_of(view, struct tty3270, view);

	del_timer(&tp->timer);
}

static void tty3270_irq(struct tty3270 *tp, struct raw3270_request *rq, struct irb *irb)
{
	/* Handle ATTN. Schedule tasklet to read aid. */
	if (irb->scsw.cmd.dstat & DEV_STAT_ATTENTION) {
		if (!tp->throttle)
			tty3270_issue_read(tp, 0);
		else
			tp->attn = 1;
	}

	if (rq) {
		if (irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK) {
			rq->rc = -EIO;
			raw3270_get_view(&tp->view);
			tasklet_schedule(&tp->hanglet);
		} else {
			/* Normal end. Copy residual count. */
			rq->rescnt = irb->scsw.cmd.count;
		}
	} else if (irb->scsw.cmd.dstat & DEV_STAT_DEV_END) {
		/* Interrupt without an outstanding request -> update all */
		tp->update_flags = TTY_UPDATE_ALL;
		tty3270_set_timer(tp, 1);
	}
}

/*
 * Allocate tty3270 structure.
 */
static struct tty3270 *tty3270_alloc_view(void)
{
	struct tty3270 *tp;

	tp = kzalloc(sizeof(*tp), GFP_KERNEL);
	if (!tp)
		goto out_err;

	tp->write = raw3270_request_alloc(TTY3270_OUTPUT_BUFFER_SIZE);
	if (IS_ERR(tp->write))
		goto out_tp;
	tp->read = raw3270_request_alloc(0);
	if (IS_ERR(tp->read))
		goto out_write;
	tp->kreset = raw3270_request_alloc(1);
	if (IS_ERR(tp->kreset))
		goto out_read;
	tp->readpartreq = raw3270_request_alloc(sizeof(sfq_read_partition));
	if (IS_ERR(tp->readpartreq))
		goto out_reset;
	tp->kbd = kbd_alloc();
	if (!tp->kbd)
		goto out_readpartreq;

	tty_port_init(&tp->port);
	timer_setup(&tp->timer, tty3270_update, 0);
	tasklet_init(&tp->readlet, tty3270_read_tasklet,
		     (unsigned long)tp->read);
	tasklet_init(&tp->hanglet, tty3270_hangup_tasklet,
		     (unsigned long)tp);
	return tp;

out_readpartreq:
	raw3270_request_free(tp->readpartreq);
out_reset:
	raw3270_request_free(tp->kreset);
out_read:
	raw3270_request_free(tp->read);
out_write:
	raw3270_request_free(tp->write);
out_tp:
	kfree(tp);
out_err:
	return ERR_PTR(-ENOMEM);
}

/*
 * Free tty3270 structure.
 */
static void tty3270_free_view(struct tty3270 *tp)
{
	kbd_free(tp->kbd);
	raw3270_request_free(tp->kreset);
	raw3270_request_free(tp->read);
	raw3270_request_free(tp->write);
	free_page((unsigned long)tp->converted_line);
	tty_port_destroy(&tp->port);
	kfree(tp);
}

/*
 * Allocate tty3270 screen.
 */
static struct tty3270_line *tty3270_alloc_screen(struct tty3270 *tp, unsigned int rows,
						 unsigned int cols, int *allocated_out)
{
	struct tty3270_line *screen;
	int allocated, lines;

	allocated = __roundup_pow_of_two(rows) * TTY3270_SCREEN_PAGES;
	screen = kcalloc(allocated, sizeof(struct tty3270_line), GFP_KERNEL);
	if (!screen)
		goto out_err;
	for (lines = 0; lines < allocated; lines++) {
		screen[lines].cells = kcalloc(cols, sizeof(struct tty3270_cell), GFP_KERNEL);
		if (!screen[lines].cells)
			goto out_screen;
	}
	*allocated_out = allocated;
	return screen;
out_screen:
	while (lines--)
		kfree(screen[lines].cells);
	kfree(screen);
out_err:
	return ERR_PTR(-ENOMEM);
}

static char **tty3270_alloc_recall(int cols)
{
	char **lines;
	int i;

	lines = kmalloc_array(TTY3270_RECALL_SIZE, sizeof(char *), GFP_KERNEL);
	if (!lines)
		return NULL;
	for (i = 0; i < TTY3270_RECALL_SIZE; i++) {
		lines[i] = kcalloc(1, tty3270_input_size(cols) + 1, GFP_KERNEL);
		if (!lines[i])
			break;
	}

	if (i == TTY3270_RECALL_SIZE)
		return lines;

	while (i--)
		kfree(lines[i]);
	kfree(lines);
	return NULL;
}

static void tty3270_free_recall(char **lines)
{
	int i;

	for (i = 0; i < TTY3270_RECALL_SIZE; i++)
		kfree(lines[i]);
	kfree(lines);
}

/*
 * Free tty3270 screen.
 */
static void tty3270_free_screen(struct tty3270_line *screen, int old_lines)
{
	int lines;

	for (lines = 0; lines < old_lines; lines++)
		kfree(screen[lines].cells);
	kfree(screen);
}

/*
 * Resize tty3270 screen
 */
static void tty3270_resize(struct raw3270_view *view,
			   int new_model, int new_rows, int new_cols,
			   int old_model, int old_rows, int old_cols)
{
	struct tty3270 *tp = container_of(view, struct tty3270, view);
	struct tty3270_line *screen, *oscreen;
	char **old_rcl_lines, **new_rcl_lines;
	char *old_prompt, *new_prompt;
	char *old_input, *new_input;
	struct tty_struct *tty;
	struct winsize ws;
	int new_allocated, old_allocated = tp->allocated_lines;

	if (old_model == new_model &&
	    old_cols == new_cols &&
	    old_rows == new_rows) {
		spin_lock_irq(&tp->view.lock);
		tty3270_redraw(tp);
		spin_unlock_irq(&tp->view.lock);
		return;
	}

	new_input = kzalloc(tty3270_input_size(new_cols), GFP_KERNEL | GFP_DMA);
	if (!new_input)
		return;
	new_prompt = kzalloc(tty3270_input_size(new_cols), GFP_KERNEL);
	if (!new_prompt)
		goto out_input;
	screen = tty3270_alloc_screen(tp, new_rows, new_cols, &new_allocated);
	if (IS_ERR(screen))
		goto out_prompt;
	new_rcl_lines = tty3270_alloc_recall(new_cols);
	if (!new_rcl_lines)
		goto out_screen;

	/* Switch to new output size */
	spin_lock_irq(&tp->view.lock);
	tty3270_blank_screen(tp);
	oscreen = tp->screen;
	tp->screen = screen;
	tp->allocated_lines = new_allocated;
	tp->view.rows = new_rows;
	tp->view.cols = new_cols;
	tp->view.model = new_model;
	tp->update_flags = TTY_UPDATE_ALL;
	old_input = tp->input;
	old_prompt = tp->prompt;
	old_rcl_lines = tp->rcl_lines;
	tp->input = new_input;
	tp->prompt = new_prompt;
	tp->rcl_lines = new_rcl_lines;
	tp->rcl_read_index = 0;
	tp->rcl_write_index = 0;
	spin_unlock_irq(&tp->view.lock);
	tty3270_free_screen(oscreen, old_allocated);
	kfree(old_input);
	kfree(old_prompt);
	tty3270_free_recall(old_rcl_lines);
	tty3270_set_timer(tp, 1);
	/* Informat tty layer about new size */
	tty = tty_port_tty_get(&tp->port);
	if (!tty)
		return;
	ws.ws_row = tty3270_tty_rows(tp);
	ws.ws_col = tp->view.cols;
	tty_do_resize(tty, &ws);
	tty_kref_put(tty);
	return;
out_screen:
	tty3270_free_screen(screen, new_rows);
out_prompt:
	kfree(new_prompt);
out_input:
	kfree(new_input);
}

/*
 * Unlink tty3270 data structure from tty.
 */
static void tty3270_release(struct raw3270_view *view)
{
	struct tty3270 *tp = container_of(view, struct tty3270, view);
	struct tty_struct *tty = tty_port_tty_get(&tp->port);

	if (tty) {
		tty->driver_data = NULL;
		tty_port_tty_set(&tp->port, NULL);
		tty_hangup(tty);
		raw3270_put_view(&tp->view);
		tty_kref_put(tty);
	}
}

/*
 * Free tty3270 data structure
 */
static void tty3270_free(struct raw3270_view *view)
{
	struct tty3270 *tp = container_of(view, struct tty3270, view);

	del_timer_sync(&tp->timer);
	tty3270_free_screen(tp->screen, tp->allocated_lines);
	free_page((unsigned long)tp->converted_line);
	kfree(tp->input);
	kfree(tp->prompt);
	tty3270_free_view(tp);
}

/*
 * Delayed freeing of tty3270 views.
 */
static void tty3270_del_views(void)
{
	int i;

	for (i = RAW3270_FIRSTMINOR; i <= tty3270_max_index; i++) {
		struct raw3270_view *view = raw3270_find_view(&tty3270_fn, i);

		if (!IS_ERR(view))
			raw3270_del_view(view);
	}
}

static struct raw3270_fn tty3270_fn = {
	.activate = tty3270_activate,
	.deactivate = tty3270_deactivate,
	.intv = (void *)tty3270_irq,
	.release = tty3270_release,
	.free = tty3270_free,
	.resize = tty3270_resize
};

static int
tty3270_create_view(int index, struct tty3270 **newtp)
{
	struct tty3270 *tp;
	int rc;

	if (tty3270_max_index < index + 1)
		tty3270_max_index = index + 1;

	/* Allocate tty3270 structure on first open. */
	tp = tty3270_alloc_view();
	if (IS_ERR(tp))
		return PTR_ERR(tp);

	rc = raw3270_add_view(&tp->view, &tty3270_fn,
			      index + RAW3270_FIRSTMINOR,
			      RAW3270_VIEW_LOCK_IRQ);
	if (rc)
		goto out_free_view;

	tp->screen = tty3270_alloc_screen(tp, tp->view.rows, tp->view.cols,
					  &tp->allocated_lines);
	if (IS_ERR(tp->screen)) {
		rc = PTR_ERR(tp->screen);
		goto out_put_view;
	}

	tp->converted_line = (void *)__get_free_page(GFP_KERNEL);
	if (!tp->converted_line) {
		rc = -ENOMEM;
		goto out_free_screen;
	}

	tp->input = kzalloc(tty3270_input_size(tp->view.cols), GFP_KERNEL | GFP_DMA);
	if (!tp->input) {
		rc = -ENOMEM;
		goto out_free_converted_line;
	}

	tp->prompt = kzalloc(tty3270_input_size(tp->view.cols), GFP_KERNEL);
	if (!tp->prompt) {
		rc = -ENOMEM;
		goto out_free_input;
	}

	tp->rcl_lines = tty3270_alloc_recall(tp->view.cols);
	if (!tp->rcl_lines) {
		rc = -ENOMEM;
		goto out_free_prompt;
	}

	/* Create blank line for every line in the tty output area. */
	tty3270_blank_screen(tp);

	tp->kbd->port = &tp->port;
	tp->kbd->fn_handler[KVAL(K_INCRCONSOLE)] = tty3270_exit_tty;
	tp->kbd->fn_handler[KVAL(K_SCROLLBACK)] = tty3270_scroll_backward;
	tp->kbd->fn_handler[KVAL(K_SCROLLFORW)] = tty3270_scroll_forward;
	tp->kbd->fn_handler[KVAL(K_CONS)] = tty3270_rcl_backward;
	kbd_ascebc(tp->kbd, tp->view.ascebc);

	raw3270_activate_view(&tp->view);
	raw3270_put_view(&tp->view);
	*newtp = tp;
	return 0;

out_free_prompt:
	kfree(tp->prompt);
out_free_input:
	kfree(tp->input);
out_free_converted_line:
	free_page((unsigned long)tp->converted_line);
out_free_screen:
	tty3270_free_screen(tp->screen, tp->view.rows);
out_put_view:
	raw3270_put_view(&tp->view);
	raw3270_del_view(&tp->view);
out_free_view:
	tty3270_free_view(tp);
	return rc;
}

/*
 * This routine is called whenever a 3270 tty is opened first time.
 */
static int
tty3270_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct raw3270_view *view;
	struct tty3270 *tp;
	int rc;

	/* Check if the tty3270 is already there. */
	view = raw3270_find_view(&tty3270_fn, tty->index + RAW3270_FIRSTMINOR);
	if (IS_ERR(view)) {
		rc = tty3270_create_view(tty->index, &tp);
		if (rc)
			return rc;
	} else {
		tp = container_of(view, struct tty3270, view);
		tty->driver_data = tp;
		tp->inattr = TF_INPUT;
	}

	tty->winsize.ws_row = tty3270_tty_rows(tp);
	tty->winsize.ws_col = tp->view.cols;
	rc = tty_port_install(&tp->port, driver, tty);
	if (rc) {
		raw3270_put_view(&tp->view);
		return rc;
	}
	tty->driver_data = tp;
	return 0;
}

/*
 * This routine is called whenever a 3270 tty is opened.
 */
static int tty3270_open(struct tty_struct *tty, struct file *filp)
{
	struct tty3270 *tp = tty->driver_data;
	struct tty_port *port = &tp->port;

	port->count++;
	tty_port_tty_set(port, tty);
	return 0;
}

/*
 * This routine is called when the 3270 tty is closed. We wait
 * for the remaining request to be completed. Then we clean up.
 */
static void tty3270_close(struct tty_struct *tty, struct file *filp)
{
	struct tty3270 *tp = tty->driver_data;

	if (tty->count > 1)
		return;
	if (tp)
		tty_port_tty_set(&tp->port, NULL);
}

static void tty3270_cleanup(struct tty_struct *tty)
{
	struct tty3270 *tp = tty->driver_data;

	if (tp) {
		tty->driver_data = NULL;
		raw3270_put_view(&tp->view);
	}
}

/*
 * We always have room.
 */
static unsigned int tty3270_write_room(struct tty_struct *tty)
{
	return INT_MAX;
}

/*
 * Insert character into the screen at the current position with the
 * current color and highlight. This function does NOT do cursor movement.
 */
static void tty3270_put_character(struct tty3270 *tp, u8 ch)
{
	struct tty3270_line *line;
	struct tty3270_cell *cell;

	line = tty3270_get_write_line(tp, tp->cy);
	if (line->len <= tp->cx) {
		while (line->len < tp->cx) {
			cell = line->cells + line->len;
			cell->character = ' ';
			cell->attributes = tp->attributes;
			line->len++;
		}
		line->len++;
	}
	cell = line->cells + tp->cx;
	cell->character = ch;
	cell->attributes = tp->attributes;
	line->dirty = 1;
}

/*
 * Do carriage return.
 */
static void tty3270_cr(struct tty3270 *tp)
{
	tp->cx = 0;
}

/*
 * Do line feed.
 */
static void tty3270_lf(struct tty3270 *tp)
{
	struct tty3270_line *line;
	int i;

	if (tp->cy < tty3270_tty_rows(tp) - 1) {
		tp->cy++;
	} else {
		tp->line_view_start = tty3270_line_increment(tp, tp->line_view_start, 1);
		tp->line_write_start = tty3270_line_increment(tp, tp->line_write_start, 1);
		for (i = 0; i < tty3270_tty_rows(tp); i++)
			tty3270_get_view_line(tp, i)->dirty = 1;
	}

	line = tty3270_get_write_line(tp, tp->cy);
	line->len = 0;
	line->dirty = 1;
}

static void tty3270_ri(struct tty3270 *tp)
{
	if (tp->cy > 0)
		tp->cy--;
}

static void tty3270_reset_cell(struct tty3270 *tp, struct tty3270_cell *cell)
{
	cell->character = ' ';
	tty3270_reset_attributes(&cell->attributes);
}

/*
 * Insert characters at current position.
 */
static void tty3270_insert_characters(struct tty3270 *tp, int n)
{
	struct tty3270_line *line;
	int k;

	line = tty3270_get_write_line(tp, tp->cy);
	while (line->len < tp->cx)
		tty3270_reset_cell(tp, &line->cells[line->len++]);
	if (n > tp->view.cols - tp->cx)
		n = tp->view.cols - tp->cx;
	k = min_t(int, line->len - tp->cx, tp->view.cols - tp->cx - n);
	while (k--)
		line->cells[tp->cx + n + k] = line->cells[tp->cx + k];
	line->len += n;
	if (line->len > tp->view.cols)
		line->len = tp->view.cols;
	while (n-- > 0) {
		line->cells[tp->cx + n].character = ' ';
		line->cells[tp->cx + n].attributes = tp->attributes;
	}
}

/*
 * Delete characters at current position.
 */
static void tty3270_delete_characters(struct tty3270 *tp, int n)
{
	struct tty3270_line *line;
	int i;

	line = tty3270_get_write_line(tp, tp->cy);
	if (line->len <= tp->cx)
		return;
	if (line->len - tp->cx <= n) {
		line->len = tp->cx;
		return;
	}
	for (i = tp->cx; i + n < line->len; i++)
		line->cells[i] = line->cells[i + n];
	line->len -= n;
}

/*
 * Erase characters at current position.
 */
static void tty3270_erase_characters(struct tty3270 *tp, int n)
{
	struct tty3270_line *line;
	struct tty3270_cell *cell;

	line = tty3270_get_write_line(tp, tp->cy);
	while (line->len > tp->cx && n-- > 0) {
		cell = line->cells + tp->cx++;
		tty3270_reset_cell(tp, cell);
	}
	tp->cx += n;
	tp->cx = min_t(int, tp->cx, tp->view.cols - 1);
}

/*
 * Erase line, 3 different cases:
 *  Esc [ 0 K	Erase from current position to end of line inclusive
 *  Esc [ 1 K	Erase from beginning of line to current position inclusive
 *  Esc [ 2 K	Erase entire line (without moving cursor)
 */
static void tty3270_erase_line(struct tty3270 *tp, int mode)
{
	struct tty3270_line *line;
	struct tty3270_cell *cell;
	int i, start, end;

	line = tty3270_get_write_line(tp, tp->cy);

	switch (mode) {
	case 0:
		start = tp->cx;
		end = tp->view.cols;
		break;
	case 1:
		start = 0;
		end = tp->cx;
		break;
	case 2:
		start = 0;
		end = tp->view.cols;
		break;
	default:
		return;
	}

	for (i = start; i < end; i++) {
		cell = line->cells + i;
		tty3270_reset_cell(tp, cell);
		cell->attributes.b_color = tp->attributes.b_color;
	}

	if (line->len <= end)
		line->len = end;
}

/*
 * Erase display, 3 different cases:
 *  Esc [ 0 J	Erase from current position to bottom of screen inclusive
 *  Esc [ 1 J	Erase from top of screen to current position inclusive
 *  Esc [ 2 J	Erase entire screen (without moving the cursor)
 */
static void tty3270_erase_display(struct tty3270 *tp, int mode)
{
	struct tty3270_line *line;
	int i, start, end;

	switch (mode) {
	case 0:
		tty3270_erase_line(tp, 0);
		start = tp->cy + 1;
		end = tty3270_tty_rows(tp);
		break;
	case 1:
		start = 0;
		end = tp->cy;
		tty3270_erase_line(tp, 1);
		break;
	case 2:
		start = 0;
		end = tty3270_tty_rows(tp);
		break;
	default:
		return;
	}
	for (i = start; i < end; i++) {
		line = tty3270_get_write_line(tp, i);
		line->len = 0;
		line->dirty = 1;
	}
}

/*
 * Set attributes found in an escape sequence.
 *  Esc [ <attr> ; <attr> ; ... m
 */
static void tty3270_set_attributes(struct tty3270 *tp)
{
	int i, attr;

	for (i = 0; i <= tp->esc_npar; i++) {
		attr = tp->esc_par[i];
		switch (attr) {
		case 0:		/* Reset */
			tty3270_reset_attributes(&tp->attributes);
			break;
		/* Highlight. */
		case 4:		/* Start underlining. */
			tp->attributes.highlight = TTY3270_HIGHLIGHT_UNDERSCORE;
			break;
		case 5:		/* Start blink. */
			tp->attributes.highlight = TTY3270_HIGHLIGHT_BLINK;
			break;
		case 7:		/* Start reverse. */
			tp->attributes.highlight = TTY3270_HIGHLIGHT_REVERSE;
			break;
		case 24:	/* End underlining */
			tp->attributes.highlight &= ~TTY3270_HIGHLIGHT_UNDERSCORE;
			break;
		case 25:	/* End blink. */
			tp->attributes.highlight &= ~TTY3270_HIGHLIGHT_BLINK;
			break;
		case 27:	/* End reverse. */
			tp->attributes.highlight &= ~TTY3270_HIGHLIGHT_REVERSE;
			break;
		/* Foreground color. */
		case 30:	/* Black */
		case 31:	/* Red */
		case 32:	/* Green */
		case 33:	/* Yellow */
		case 34:	/* Blue */
		case 35:	/* Magenta */
		case 36:	/* Cyan */
		case 37:	/* White */
		case 39:	/* Black */
			tp->attributes.f_color = attr - 30;
			break;
		/* Background color. */
		case 40:	/* Black */
		case 41:	/* Red */
		case 42:	/* Green */
		case 43:	/* Yellow */
		case 44:	/* Blue */
		case 45:	/* Magenta */
		case 46:	/* Cyan */
		case 47:	/* White */
		case 49:	/* Black */
			tp->attributes.b_color = attr - 40;
			break;
		}
	}
}

static inline int tty3270_getpar(struct tty3270 *tp, int ix)
{
	return (tp->esc_par[ix] > 0) ? tp->esc_par[ix] : 1;
}

static void tty3270_goto_xy(struct tty3270 *tp, int cx, int cy)
{
	struct tty3270_line *line;
	struct tty3270_cell *cell;
	int max_cx = max(0, cx);
	int max_cy = max(0, cy);

	tp->cx = min_t(int, tp->view.cols - 1, max_cx);
	line = tty3270_get_write_line(tp, tp->cy);
	while (line->len < tp->cx) {
		cell = line->cells + line->len;
		cell->character = ' ';
		cell->attributes = tp->attributes;
		line->len++;
	}
	tp->cy = min_t(int, tty3270_tty_rows(tp) - 1, max_cy);
}

/*
 * Process escape sequences. Known sequences:
 *  Esc 7			Save Cursor Position
 *  Esc 8			Restore Cursor Position
 *  Esc [ Pn ; Pn ; .. m	Set attributes
 *  Esc [ Pn ; Pn H		Cursor Position
 *  Esc [ Pn ; Pn f		Cursor Position
 *  Esc [ Pn A			Cursor Up
 *  Esc [ Pn B			Cursor Down
 *  Esc [ Pn C			Cursor Forward
 *  Esc [ Pn D			Cursor Backward
 *  Esc [ Pn G			Cursor Horizontal Absolute
 *  Esc [ Pn X			Erase Characters
 *  Esc [ Ps J			Erase in Display
 *  Esc [ Ps K			Erase in Line
 * // FIXME: add all the new ones.
 *
 *  Pn is a numeric parameter, a string of zero or more decimal digits.
 *  Ps is a selective parameter.
 */
static void tty3270_escape_sequence(struct tty3270 *tp, u8 ch)
{
	enum { ES_NORMAL, ES_ESC, ES_SQUARE, ES_PAREN, ES_GETPARS };

	if (tp->esc_state == ES_NORMAL) {
		if (ch == 0x1b)
			/* Starting new escape sequence. */
			tp->esc_state = ES_ESC;
		return;
	}
	if (tp->esc_state == ES_ESC) {
		tp->esc_state = ES_NORMAL;
		switch (ch) {
		case '[':
			tp->esc_state = ES_SQUARE;
			break;
		case '(':
			tp->esc_state = ES_PAREN;
			break;
		case 'E':
			tty3270_cr(tp);
			tty3270_lf(tp);
			break;
		case 'M':
			tty3270_ri(tp);
			break;
		case 'D':
			tty3270_lf(tp);
			break;
		case 'Z':		/* Respond ID. */
			kbd_puts_queue(&tp->port, "\033[?6c");
			break;
		case '7':		/* Save cursor position. */
			tp->saved_cx = tp->cx;
			tp->saved_cy = tp->cy;
			tp->saved_attributes = tp->attributes;
			break;
		case '8':		/* Restore cursor position. */
			tty3270_goto_xy(tp, tp->saved_cx, tp->saved_cy);
			tp->attributes = tp->saved_attributes;
			break;
		case 'c':		/* Reset terminal. */
			tp->cx = 0;
			tp->cy = 0;
			tp->saved_cx = 0;
			tp->saved_cy = 0;
			tty3270_reset_attributes(&tp->attributes);
			tty3270_reset_attributes(&tp->saved_attributes);
			tty3270_erase_display(tp, 2);
			break;
		}
		return;
	}

	switch (tp->esc_state) {
	case ES_PAREN:
		tp->esc_state = ES_NORMAL;
		switch (ch) {
		case 'B':
			tp->attributes.alternate_charset = 0;
			break;
		case '0':
			tp->attributes.alternate_charset = 1;
			break;
		}
		return;
	case ES_SQUARE:
		tp->esc_state = ES_GETPARS;
		memset(tp->esc_par, 0, sizeof(tp->esc_par));
		tp->esc_npar = 0;
		tp->esc_ques = (ch == '?');
		if (tp->esc_ques)
			return;
		fallthrough;
	case ES_GETPARS:
		if (ch == ';' && tp->esc_npar < ESCAPE_NPAR - 1) {
			tp->esc_npar++;
			return;
		}
		if (ch >= '0' && ch <= '9') {
			tp->esc_par[tp->esc_npar] *= 10;
			tp->esc_par[tp->esc_npar] += ch - '0';
			return;
		}
		break;
	default:
		break;
	}
	tp->esc_state = ES_NORMAL;
	if (ch == 'n' && !tp->esc_ques) {
		if (tp->esc_par[0] == 5)		/* Status report. */
			kbd_puts_queue(&tp->port, "\033[0n");
		else if (tp->esc_par[0] == 6) {	/* Cursor report. */
			char buf[40];

			sprintf(buf, "\033[%d;%dR", tp->cy + 1, tp->cx + 1);
			kbd_puts_queue(&tp->port, buf);
		}
		return;
	}
	if (tp->esc_ques)
		return;
	switch (ch) {
	case 'm':
		tty3270_set_attributes(tp);
		break;
	case 'H':	/* Set cursor position. */
	case 'f':
		tty3270_goto_xy(tp, tty3270_getpar(tp, 1) - 1,
				tty3270_getpar(tp, 0) - 1);
		break;
	case 'd':	/* Set y position. */
		tty3270_goto_xy(tp, tp->cx, tty3270_getpar(tp, 0) - 1);
		break;
	case 'A':	/* Cursor up. */
	case 'F':
		tty3270_goto_xy(tp, tp->cx, tp->cy - tty3270_getpar(tp, 0));
		break;
	case 'B':	/* Cursor down. */
	case 'e':
	case 'E':
		tty3270_goto_xy(tp, tp->cx, tp->cy + tty3270_getpar(tp, 0));
		break;
	case 'C':	/* Cursor forward. */
	case 'a':
		tty3270_goto_xy(tp, tp->cx + tty3270_getpar(tp, 0), tp->cy);
		break;
	case 'D':	/* Cursor backward. */
		tty3270_goto_xy(tp, tp->cx - tty3270_getpar(tp, 0), tp->cy);
		break;
	case 'G':	/* Set x position. */
	case '`':
		tty3270_goto_xy(tp, tty3270_getpar(tp, 0), tp->cy);
		break;
	case 'X':	/* Erase Characters. */
		tty3270_erase_characters(tp, tty3270_getpar(tp, 0));
		break;
	case 'J':	/* Erase display. */
		tty3270_erase_display(tp, tp->esc_par[0]);
		break;
	case 'K':	/* Erase line. */
		tty3270_erase_line(tp, tp->esc_par[0]);
		break;
	case 'P':	/* Delete characters. */
		tty3270_delete_characters(tp, tty3270_getpar(tp, 0));
		break;
	case '@':	/* Insert characters. */
		tty3270_insert_characters(tp, tty3270_getpar(tp, 0));
		break;
	case 's':	/* Save cursor position. */
		tp->saved_cx = tp->cx;
		tp->saved_cy = tp->cy;
		tp->saved_attributes = tp->attributes;
		break;
	case 'u':	/* Restore cursor position. */
		tty3270_goto_xy(tp, tp->saved_cx, tp->saved_cy);
		tp->attributes = tp->saved_attributes;
		break;
	}
}

/*
 * String write routine for 3270 ttys
 */
static void tty3270_do_write(struct tty3270 *tp, struct tty_struct *tty,
			     const u8 *buf, size_t count)
{
	int i_msg, i;

	spin_lock_irq(&tp->view.lock);
	for (i_msg = 0; !tty->flow.stopped && i_msg < count; i_msg++) {
		if (tp->esc_state != 0) {
			/* Continue escape sequence. */
			tty3270_escape_sequence(tp, buf[i_msg]);
			continue;
		}

		switch (buf[i_msg]) {
		case 0x00:
			break;
		case 0x07:		/* '\a' -- Alarm */
			tp->wcc |= TW_PLUSALARM;
			break;
		case 0x08:		/* Backspace. */
			if (tp->cx > 0) {
				tp->cx--;
				tty3270_put_character(tp, ' ');
			}
			break;
		case 0x09:		/* '\t' -- Tabulate */
			for (i = tp->cx % 8; i < 8; i++) {
				if (tp->cx >= tp->view.cols) {
					tty3270_cr(tp);
					tty3270_lf(tp);
					break;
				}
				tty3270_put_character(tp, ' ');
				tp->cx++;
			}
			break;
		case 0x0a:		/* '\n' -- New Line */
			tty3270_cr(tp);
			tty3270_lf(tp);
			break;
		case 0x0c:		/* '\f' -- Form Feed */
			tty3270_erase_display(tp, 2);
			tp->cx = 0;
			tp->cy = 0;
			break;
		case 0x0d:		/* '\r' -- Carriage Return */
			tp->cx = 0;
			break;
		case 0x0e:
			tp->attributes.alternate_charset = 1;
			break;
		case 0x0f:		/* SuSE "exit alternate mode" */
			tp->attributes.alternate_charset = 0;
			break;
		case 0x1b:		/* Start escape sequence. */
			tty3270_escape_sequence(tp, buf[i_msg]);
			break;
		default:		/* Insert normal character. */
			if (tp->cx >= tp->view.cols) {
				tty3270_cr(tp);
				tty3270_lf(tp);
			}
			tty3270_put_character(tp, buf[i_msg]);
			tp->cx++;
			break;
		}
	}
	/* Setup timer to update display after 1/10 second */
	tp->update_flags |= TTY_UPDATE_LINES;
	if (!timer_pending(&tp->timer))
		tty3270_set_timer(tp, msecs_to_jiffies(100));

	spin_unlock_irq(&tp->view.lock);
}

/*
 * String write routine for 3270 ttys
 */
static ssize_t tty3270_write(struct tty_struct *tty, const u8 *buf,
			     size_t count)
{
	struct tty3270 *tp;

	tp = tty->driver_data;
	if (!tp)
		return 0;
	if (tp->char_count > 0) {
		tty3270_do_write(tp, tty, tp->char_buf, tp->char_count);
		tp->char_count = 0;
	}
	tty3270_do_write(tp, tty, buf, count);
	return count;
}

/*
 * Put single characters to the ttys character buffer
 */
static int tty3270_put_char(struct tty_struct *tty, u8 ch)
{
	struct tty3270 *tp;

	tp = tty->driver_data;
	if (!tp || tp->char_count >= TTY3270_CHAR_BUF_SIZE)
		return 0;
	tp->char_buf[tp->char_count++] = ch;
	return 1;
}

/*
 * Flush all characters from the ttys characeter buffer put there
 * by tty3270_put_char.
 */
static void tty3270_flush_chars(struct tty_struct *tty)
{
	struct tty3270 *tp;

	tp = tty->driver_data;
	if (!tp)
		return;
	if (tp->char_count > 0) {
		tty3270_do_write(tp, tty, tp->char_buf, tp->char_count);
		tp->char_count = 0;
	}
}

/*
 * Check for visible/invisible input switches
 */
static void tty3270_set_termios(struct tty_struct *tty, const struct ktermios *old)
{
	struct tty3270 *tp;
	int new;

	tp = tty->driver_data;
	if (!tp)
		return;
	spin_lock_irq(&tp->view.lock);
	if (L_ICANON(tty)) {
		new = L_ECHO(tty) ? TF_INPUT : TF_INPUTN;
		if (new != tp->inattr) {
			tp->inattr = new;
			tty3270_update_prompt(tp, "");
			tty3270_set_timer(tp, 1);
		}
	}
	spin_unlock_irq(&tp->view.lock);
}

/*
 * Disable reading from a 3270 tty
 */
static void tty3270_throttle(struct tty_struct *tty)
{
	struct tty3270 *tp;

	tp = tty->driver_data;
	if (!tp)
		return;
	tp->throttle = 1;
}

/*
 * Enable reading from a 3270 tty
 */
static void tty3270_unthrottle(struct tty_struct *tty)
{
	struct tty3270 *tp;

	tp = tty->driver_data;
	if (!tp)
		return;
	tp->throttle = 0;
	if (tp->attn)
		tty3270_issue_read(tp, 1);
}

/*
 * Hang up the tty device.
 */
static void tty3270_hangup(struct tty_struct *tty)
{
	struct tty3270 *tp;

	tp = tty->driver_data;
	if (!tp)
		return;
	spin_lock_irq(&tp->view.lock);
	tp->cx = 0;
	tp->cy = 0;
	tp->saved_cx = 0;
	tp->saved_cy = 0;
	tty3270_reset_attributes(&tp->attributes);
	tty3270_reset_attributes(&tp->saved_attributes);
	tty3270_blank_screen(tp);
	tp->update_flags = TTY_UPDATE_ALL;
	spin_unlock_irq(&tp->view.lock);
	tty3270_set_timer(tp, 1);
}

static void tty3270_wait_until_sent(struct tty_struct *tty, int timeout)
{
}

static int tty3270_ioctl(struct tty_struct *tty, unsigned int cmd,
			 unsigned long arg)
{
	struct tty3270 *tp;

	tp = tty->driver_data;
	if (!tp)
		return -ENODEV;
	if (tty_io_error(tty))
		return -EIO;
	return kbd_ioctl(tp->kbd, cmd, arg);
}

#ifdef CONFIG_COMPAT
static long tty3270_compat_ioctl(struct tty_struct *tty,
				 unsigned int cmd, unsigned long arg)
{
	struct tty3270 *tp;

	tp = tty->driver_data;
	if (!tp)
		return -ENODEV;
	if (tty_io_error(tty))
		return -EIO;
	return kbd_ioctl(tp->kbd, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct tty_operations tty3270_ops = {
	.install = tty3270_install,
	.cleanup = tty3270_cleanup,
	.open = tty3270_open,
	.close = tty3270_close,
	.write = tty3270_write,
	.put_char = tty3270_put_char,
	.flush_chars = tty3270_flush_chars,
	.write_room = tty3270_write_room,
	.throttle = tty3270_throttle,
	.unthrottle = tty3270_unthrottle,
	.hangup = tty3270_hangup,
	.wait_until_sent = tty3270_wait_until_sent,
	.ioctl = tty3270_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tty3270_compat_ioctl,
#endif
	.set_termios = tty3270_set_termios
};

static void tty3270_create_cb(int minor)
{
	tty_register_device(tty3270_driver, minor - RAW3270_FIRSTMINOR, NULL);
}

static void tty3270_destroy_cb(int minor)
{
	tty_unregister_device(tty3270_driver, minor - RAW3270_FIRSTMINOR);
}

static struct raw3270_notifier tty3270_notifier = {
	.create = tty3270_create_cb,
	.destroy = tty3270_destroy_cb,
};

/*
 * 3270 tty registration code called from tty_init().
 * Most kernel services (incl. kmalloc) are available at this poimt.
 */
static int __init tty3270_init(void)
{
	struct tty_driver *driver;
	int ret;

	driver = tty_alloc_driver(RAW3270_MAXDEVS,
				  TTY_DRIVER_REAL_RAW |
				  TTY_DRIVER_DYNAMIC_DEV |
				  TTY_DRIVER_RESET_TERMIOS);
	if (IS_ERR(driver))
		return PTR_ERR(driver);

	/*
	 * Initialize the tty_driver structure
	 * Entries in tty3270_driver that are NOT initialized:
	 * proc_entry, set_termios, flush_buffer, set_ldisc, write_proc
	 */
	driver->driver_name = "tty3270";
	driver->name = "3270/tty";
	driver->major = IBM_TTY3270_MAJOR;
	driver->minor_start = RAW3270_FIRSTMINOR;
	driver->name_base = RAW3270_FIRSTMINOR;
	driver->type = TTY_DRIVER_TYPE_SYSTEM;
	driver->subtype = SYSTEM_TYPE_TTY;
	driver->init_termios = tty_std_termios;
	tty_set_operations(driver, &tty3270_ops);
	ret = tty_register_driver(driver);
	if (ret) {
		tty_driver_kref_put(driver);
		return ret;
	}
	tty3270_driver = driver;
	raw3270_register_notifier(&tty3270_notifier);
	return 0;
}

static void __exit tty3270_exit(void)
{
	struct tty_driver *driver;

	raw3270_unregister_notifier(&tty3270_notifier);
	driver = tty3270_driver;
	tty3270_driver = NULL;
	tty_unregister_driver(driver);
	tty_driver_kref_put(driver);
	tty3270_del_views();
}

#if IS_ENABLED(CONFIG_TN3270_CONSOLE)

static struct tty3270 *condev;

static void
con3270_write(struct console *co, const char *str, unsigned int count)
{
	struct tty3270 *tp = co->data;
	unsigned long flags;
	u8 c;

	spin_lock_irqsave(&tp->view.lock, flags);
	while (count--) {
		c = *str++;
		if (c == 0x0a) {
			tty3270_cr(tp);
			tty3270_lf(tp);
		} else {
			if (tp->cx >= tp->view.cols) {
				tty3270_cr(tp);
				tty3270_lf(tp);
			}
			tty3270_put_character(tp, c);
			tp->cx++;
		}
	}
	spin_unlock_irqrestore(&tp->view.lock, flags);
}

static struct tty_driver *
con3270_device(struct console *c, int *index)
{
	*index = c->index;
	return tty3270_driver;
}

static void
con3270_wait_write(struct tty3270 *tp)
{
	while (!tp->write) {
		raw3270_wait_cons_dev(tp->view.dev);
		barrier();
	}
}

/*
 * The below function is called as a panic/reboot notifier before the
 * system enters a disabled, endless loop.
 *
 * Notice we must use the spin_trylock() alternative, to prevent lockups
 * in atomic context (panic routine runs with secondary CPUs, local IRQs
 * and preemption disabled).
 */
static int con3270_notify(struct notifier_block *self,
			  unsigned long event, void *data)
{
	struct tty3270 *tp;
	unsigned long flags;
	int rc;

	tp = condev;
	if (!tp->view.dev)
		return NOTIFY_DONE;
	if (!raw3270_view_lock_unavailable(&tp->view)) {
		rc = raw3270_activate_view(&tp->view);
		if (rc)
			return NOTIFY_DONE;
	}
	if (!spin_trylock_irqsave(&tp->view.lock, flags))
		return NOTIFY_DONE;
	con3270_wait_write(tp);
	tp->nr_up = 0;
	tp->update_flags = TTY_UPDATE_ALL;
	while (tp->update_flags != 0) {
		spin_unlock_irqrestore(&tp->view.lock, flags);
		tty3270_update(&tp->timer);
		spin_lock_irqsave(&tp->view.lock, flags);
		con3270_wait_write(tp);
	}
	spin_unlock_irqrestore(&tp->view.lock, flags);
	return NOTIFY_DONE;
}

static struct notifier_block on_panic_nb = {
	.notifier_call = con3270_notify,
	.priority = INT_MIN + 1, /* run the callback late */
};

static struct notifier_block on_reboot_nb = {
	.notifier_call = con3270_notify,
	.priority = INT_MIN + 1, /* run the callback late */
};

static struct console con3270 = {
	.name	 = "tty3270",
	.write	 = con3270_write,
	.device	 = con3270_device,
	.flags	 = CON_PRINTBUFFER,
};

static int __init
con3270_init(void)
{
	struct raw3270_view *view;
	struct raw3270 *rp;
	struct tty3270 *tp;
	int rc;

	/* Check if 3270 is to be the console */
	if (!CONSOLE_IS_3270)
		return -ENODEV;

	/* Set the console mode for VM */
	if (MACHINE_IS_VM) {
		cpcmd("TERM CONMODE 3270", NULL, 0, NULL);
		cpcmd("TERM AUTOCR OFF", NULL, 0, NULL);
	}

	rp = raw3270_setup_console();
	if (IS_ERR(rp))
		return PTR_ERR(rp);

	/* Check if the tty3270 is already there. */
	view = raw3270_find_view(&tty3270_fn, RAW3270_FIRSTMINOR);
	if (IS_ERR(view)) {
		rc = tty3270_create_view(0, &tp);
		if (rc)
			return rc;
	} else {
		tp = container_of(view, struct tty3270, view);
		tp->inattr = TF_INPUT;
	}
	con3270.data = tp;
	condev = tp;
	atomic_notifier_chain_register(&panic_notifier_list, &on_panic_nb);
	register_reboot_notifier(&on_reboot_nb);
	register_console(&con3270);
	return 0;
}
console_initcall(con3270_init);
#endif

MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(IBM_TTY3270_MAJOR);

module_init(tty3270_init);
module_exit(tty3270_exit);
