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

#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/compat.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/ebcdic.h>
#include <linux/uaccess.h>

#include "raw3270.h"
#include "tty3270.h"
#include "keyboard.h"

#define TTY3270_CHAR_BUF_SIZE 256
#define TTY3270_OUTPUT_BUFFER_SIZE 1024
#define TTY3270_STRING_PAGES 5

struct tty_driver *tty3270_driver;
static int tty3270_max_index;

static struct raw3270_fn tty3270_fn;

struct tty3270_cell {
	unsigned char character;
	unsigned char highlight;
	unsigned char f_color;
};

struct tty3270_line {
	struct tty3270_cell *cells;
	int len;
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
	void **freemem_pages;		/* Array of pages used for freemem. */
	struct list_head freemem;	/* List of free memory for strings. */

	/* Output stuff. */
	struct list_head lines;		/* List of lines. */
	struct list_head update;	/* List of lines to update. */
	unsigned char wcc;		/* Write control character. */
	int nr_lines;			/* # lines in list. */
	int nr_up;			/* # lines up in history. */
	unsigned long update_flags;	/* Update indication bits. */
	struct string *status;		/* Lower right of display. */
	struct raw3270_request *write;	/* Single write request. */
	struct timer_list timer;	/* Output delay timer. */

	/* Current tty screen. */
	unsigned int cx, cy;		/* Current output position. */
	unsigned int highlight;		/* Blink/reverse/underscore */
	unsigned int f_color;		/* Foreground color */
	struct tty3270_line *screen;
	unsigned int n_model, n_cols, n_rows;	/* New model & size */
	struct work_struct resize_work;

	/* Input stuff. */
	struct string *prompt;		/* Output string for input area. */
	struct string *input;		/* Input string for read request. */
	struct raw3270_request *read;	/* Single read request. */
	struct raw3270_request *kreset;	/* Single keyboard reset request. */
	unsigned char inattr;		/* Visible/invisible input. */
	int throttle, attn;		/* tty throttle/unthrottle. */
	struct tasklet_struct readlet;	/* Tasklet to issue read request. */
	struct tasklet_struct hanglet;	/* Tasklet to hang up the tty. */
	struct kbd_data *kbd;		/* key_maps stuff. */

	/* Escape sequence parsing. */
	int esc_state, esc_ques, esc_npar;
	int esc_par[ESCAPE_NPAR];
	unsigned int saved_cx, saved_cy;
	unsigned int saved_highlight, saved_f_color;

	/* Command recalling. */
	struct list_head rcl_lines;	/* List of recallable lines. */
	struct list_head *rcl_walk;	/* Point in rcl_lines list. */
	int rcl_nr, rcl_max;		/* Number/max number of rcl_lines. */

	/* Character array for put_char/flush_chars. */
	unsigned int char_count;
	char char_buf[TTY3270_CHAR_BUF_SIZE];
};

/* tty3270->update_flags. See tty3270_update for details. */
#define TTY_UPDATE_ERASE	1	/* Use EWRITEA instead of WRITE. */
#define TTY_UPDATE_LIST		2	/* Update lines in tty3270->update. */
#define TTY_UPDATE_INPUT	4	/* Update input line. */
#define TTY_UPDATE_STATUS	8	/* Update status line. */
#define TTY_UPDATE_ALL		16	/* Recreate screen. */

static void tty3270_update(struct timer_list *);
static void tty3270_resize_work(struct work_struct *work);

/*
 * Setup timeout for a device. On timeout trigger an update.
 */
static void tty3270_set_timer(struct tty3270 *tp, int expires)
{
	mod_timer(&tp->timer, jiffies + expires);
}

/*
 * The input line are the two last lines of the screen.
 */
static void
tty3270_update_prompt(struct tty3270 *tp, char *input, int count)
{
	struct string *line;
	unsigned int off;

	line = tp->prompt;
	if (count != 0)
		line->string[5] = TF_INMDT;
	else
		line->string[5] = tp->inattr;
	if (count > tp->view.cols * 2 - 11)
		count = tp->view.cols * 2 - 11;
	memcpy(line->string + 6, input, count);
	line->string[6 + count] = TO_IC;
	/* Clear to end of input line. */
	if (count < tp->view.cols * 2 - 11) {
		line->string[7 + count] = TO_RA;
		line->string[10 + count] = 0;
		off = tp->view.cols * tp->view.rows - 9;
		raw3270_buffer_address(tp->view.dev, line->string+count+8, off);
		line->len = 11 + count;
	} else
		line->len = 7 + count;
	tp->update_flags |= TTY_UPDATE_INPUT;
}

static void
tty3270_create_prompt(struct tty3270 *tp)
{
	static const unsigned char blueprint[] =
		{ TO_SBA, 0, 0, 0x6e, TO_SF, TF_INPUT,
		  /* empty input string */
		  TO_IC, TO_RA, 0, 0, 0 };
	struct string *line;
	unsigned int offset;

	line = alloc_string(&tp->freemem,
			    sizeof(blueprint) + tp->view.cols * 2 - 9);
	tp->prompt = line;
	tp->inattr = TF_INPUT;
	/* Copy blueprint to status line */
	memcpy(line->string, blueprint, sizeof(blueprint));
	line->len = sizeof(blueprint);
	/* Set output offsets. */
	offset = tp->view.cols * (tp->view.rows - 2);
	raw3270_buffer_address(tp->view.dev, line->string + 1, offset);
	offset = tp->view.cols * tp->view.rows - 9;
	raw3270_buffer_address(tp->view.dev, line->string + 8, offset);

	/* Allocate input string for reading. */
	tp->input = alloc_string(&tp->freemem, tp->view.cols * 2 - 9 + 6);
}

/*
 * The status line is the last line of the screen. It shows the string
 * "Running"/"Holding" in the lower right corner of the screen.
 */
static void
tty3270_update_status(struct tty3270 * tp)
{
	char *str;

	str = (tp->nr_up != 0) ? "History" : "Running";
	memcpy(tp->status->string + 8, str, 7);
	codepage_convert(tp->view.ascebc, tp->status->string + 8, 7);
	tp->update_flags |= TTY_UPDATE_STATUS;
}

static void
tty3270_create_status(struct tty3270 * tp)
{
	static const unsigned char blueprint[] =
		{ TO_SBA, 0, 0, TO_SF, TF_LOG, TO_SA, TAT_COLOR, TAC_GREEN,
		  0, 0, 0, 0, 0, 0, 0, TO_SF, TF_LOG, TO_SA, TAT_COLOR,
		  TAC_RESET };
	struct string *line;
	unsigned int offset;

	line = alloc_string(&tp->freemem,sizeof(blueprint));
	tp->status = line;
	/* Copy blueprint to status line */
	memcpy(line->string, blueprint, sizeof(blueprint));
	/* Set address to start of status string (= last 9 characters). */
	offset = tp->view.cols * tp->view.rows - 9;
	raw3270_buffer_address(tp->view.dev, line->string + 1, offset);
}

/*
 * Set output offsets to 3270 datastream fragment of a tty string.
 * (TO_SBA offset at the start and TO_RA offset at the end of the string)
 */
static void
tty3270_update_string(struct tty3270 *tp, struct string *line, int nr)
{
	unsigned char *cp;

	raw3270_buffer_address(tp->view.dev, line->string + 1,
			       tp->view.cols * nr);
	cp = line->string + line->len - 4;
	if (*cp == TO_RA)
		raw3270_buffer_address(tp->view.dev, cp + 1,
				       tp->view.cols * (nr + 1));
}

/*
 * Rebuild update list to print all lines.
 */
static void
tty3270_rebuild_update(struct tty3270 *tp)
{
	struct string *s, *n;
	int line, nr_up;

	/* 
	 * Throw away update list and create a new one,
	 * containing all lines that will fit on the screen.
	 */
	list_for_each_entry_safe(s, n, &tp->update, update)
		list_del_init(&s->update);
	line = tp->view.rows - 3;
	nr_up = tp->nr_up;
	list_for_each_entry_reverse(s, &tp->lines, list) {
		if (nr_up > 0) {
			nr_up--;
			continue;
		}
		tty3270_update_string(tp, s, line);
		list_add(&s->update, &tp->update);
		if (--line < 0)
			break;
	}
	tp->update_flags |= TTY_UPDATE_LIST;
}

/*
 * Alloc string for size bytes. If there is not enough room in
 * freemem, free strings until there is room.
 */
static struct string *
tty3270_alloc_string(struct tty3270 *tp, size_t size)
{
	struct string *s, *n;

	s = alloc_string(&tp->freemem, size);
	if (s)
		return s;
	list_for_each_entry_safe(s, n, &tp->lines, list) {
		BUG_ON(tp->nr_lines <= tp->view.rows - 2);
		list_del(&s->list);
		if (!list_empty(&s->update))
			list_del(&s->update);
		tp->nr_lines--;
		if (free_string(&tp->freemem, s) >= size)
			break;
	}
	s = alloc_string(&tp->freemem, size);
	BUG_ON(!s);
	if (tp->nr_up != 0 &&
	    tp->nr_up + tp->view.rows - 2 >= tp->nr_lines) {
		tp->nr_up = tp->nr_lines - tp->view.rows + 2;
		tty3270_rebuild_update(tp);
		tty3270_update_status(tp);
	}
	return s;
}

/*
 * Add an empty line to the list.
 */
static void
tty3270_blank_line(struct tty3270 *tp)
{
	static const unsigned char blueprint[] =
		{ TO_SBA, 0, 0, TO_SA, TAT_EXTHI, TAX_RESET,
		  TO_SA, TAT_COLOR, TAC_RESET, TO_RA, 0, 0, 0 };
	struct string *s;

	s = tty3270_alloc_string(tp, sizeof(blueprint));
	memcpy(s->string, blueprint, sizeof(blueprint));
	s->len = sizeof(blueprint);
	list_add_tail(&s->list, &tp->lines);
	tp->nr_lines++;
	if (tp->nr_up != 0)
		tp->nr_up++;
}

/*
 * Create a blank screen and remove all lines from the history.
 */
static void
tty3270_blank_screen(struct tty3270 *tp)
{
	struct string *s, *n;
	int i;

	for (i = 0; i < tp->view.rows - 2; i++)
		tp->screen[i].len = 0;
	tp->nr_up = 0;
	list_for_each_entry_safe(s, n, &tp->lines, list) {
		list_del(&s->list);
		if (!list_empty(&s->update))
			list_del(&s->update);
		tp->nr_lines--;
		free_string(&tp->freemem, s);
	}
}

/*
 * Write request completion callback.
 */
static void
tty3270_write_callback(struct raw3270_request *rq, void *data)
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

/*
 * Update 3270 display.
 */
static void
tty3270_update(struct timer_list *t)
{
	struct tty3270 *tp = from_timer(tp, t, timer);
	static char invalid_sba[2] = { 0xff, 0xff };
	struct raw3270_request *wrq;
	unsigned long updated;
	struct string *s, *n;
	char *sba, *str;
	int rc, len;

	wrq = xchg(&tp->write, 0);
	if (!wrq) {
		tty3270_set_timer(tp, 1);
		return;
	}

	spin_lock(&tp->view.lock);
	updated = 0;
	if (tp->update_flags & TTY_UPDATE_ALL) {
		tty3270_rebuild_update(tp);
		tty3270_update_status(tp);
		tp->update_flags = TTY_UPDATE_ERASE | TTY_UPDATE_LIST |
			TTY_UPDATE_INPUT | TTY_UPDATE_STATUS;
	}
	if (tp->update_flags & TTY_UPDATE_ERASE) {
		/* Use erase write alternate to erase display. */
		raw3270_request_set_cmd(wrq, TC_EWRITEA);
		updated |= TTY_UPDATE_ERASE;
	} else
		raw3270_request_set_cmd(wrq, TC_WRITE);

	raw3270_request_add_data(wrq, &tp->wcc, 1);
	tp->wcc = TW_NONE;

	/*
	 * Update status line.
	 */
	if (tp->update_flags & TTY_UPDATE_STATUS)
		if (raw3270_request_add_data(wrq, tp->status->string,
					     tp->status->len) == 0)
			updated |= TTY_UPDATE_STATUS;

	/*
	 * Write input line.
	 */
	if (tp->update_flags & TTY_UPDATE_INPUT)
		if (raw3270_request_add_data(wrq, tp->prompt->string,
					     tp->prompt->len) == 0)
			updated |= TTY_UPDATE_INPUT;

	sba = invalid_sba;
	
	if (tp->update_flags & TTY_UPDATE_LIST) {
		/* Write strings in the update list to the screen. */
		list_for_each_entry_safe(s, n, &tp->update, update) {
			str = s->string;
			len = s->len;
			/*
			 * Skip TO_SBA at the start of the string if the
			 * last output position matches the start address
			 * of this line.
			 */
			if (s->string[1] == sba[0] && s->string[2] == sba[1])
				str += 3, len -= 3;
			if (raw3270_request_add_data(wrq, str, len) != 0)
				break;
			list_del_init(&s->update);
			if (s->string[s->len - 4] == TO_RA)
				sba = s->string + s->len - 3;
			else
				sba = invalid_sba;
		}
		if (list_empty(&tp->update))
			updated |= TTY_UPDATE_LIST;
	}
	wrq->callback = tty3270_write_callback;
	rc = raw3270_start(&tp->view, wrq);
	if (rc == 0) {
		tp->update_flags &= ~updated;
		if (tp->update_flags)
			tty3270_set_timer(tp, 1);
	} else {
		raw3270_request_reset(wrq);
		xchg(&tp->write, wrq);
	}
	spin_unlock(&tp->view.lock);
}

/*
 * Command recalling.
 */
static void
tty3270_rcl_add(struct tty3270 *tp, char *input, int len)
{
	struct string *s;

	tp->rcl_walk = NULL;
	if (len <= 0)
		return;
	if (tp->rcl_nr >= tp->rcl_max) {
		s = list_entry(tp->rcl_lines.next, struct string, list);
		list_del(&s->list);
		free_string(&tp->freemem, s);
		tp->rcl_nr--;
	}
	s = tty3270_alloc_string(tp, len);
	memcpy(s->string, input, len);
	list_add_tail(&s->list, &tp->rcl_lines);
	tp->rcl_nr++;
}

static void
tty3270_rcl_backward(struct kbd_data *kbd)
{
	struct tty3270 *tp = container_of(kbd->port, struct tty3270, port);
	struct string *s;

	spin_lock_bh(&tp->view.lock);
	if (tp->inattr == TF_INPUT) {
		if (tp->rcl_walk && tp->rcl_walk->prev != &tp->rcl_lines)
			tp->rcl_walk = tp->rcl_walk->prev;
		else if (!list_empty(&tp->rcl_lines))
			tp->rcl_walk = tp->rcl_lines.prev;
		s = tp->rcl_walk ? 
			list_entry(tp->rcl_walk, struct string, list) : NULL;
		if (tp->rcl_walk) {
			s = list_entry(tp->rcl_walk, struct string, list);
			tty3270_update_prompt(tp, s->string, s->len);
		} else
			tty3270_update_prompt(tp, NULL, 0);
		tty3270_set_timer(tp, 1);
	}
	spin_unlock_bh(&tp->view.lock);
}

/*
 * Deactivate tty view.
 */
static void
tty3270_exit_tty(struct kbd_data *kbd)
{
	struct tty3270 *tp = container_of(kbd->port, struct tty3270, port);

	raw3270_deactivate_view(&tp->view);
}

/*
 * Scroll forward in history.
 */
static void
tty3270_scroll_forward(struct kbd_data *kbd)
{
	struct tty3270 *tp = container_of(kbd->port, struct tty3270, port);
	int nr_up;

	spin_lock_bh(&tp->view.lock);
	nr_up = tp->nr_up - tp->view.rows + 2;
	if (nr_up < 0)
		nr_up = 0;
	if (nr_up != tp->nr_up) {
		tp->nr_up = nr_up;
		tty3270_rebuild_update(tp);
		tty3270_update_status(tp);
		tty3270_set_timer(tp, 1);
	}
	spin_unlock_bh(&tp->view.lock);
}

/*
 * Scroll backward in history.
 */
static void
tty3270_scroll_backward(struct kbd_data *kbd)
{
	struct tty3270 *tp = container_of(kbd->port, struct tty3270, port);
	int nr_up;

	spin_lock_bh(&tp->view.lock);
	nr_up = tp->nr_up + tp->view.rows - 2;
	if (nr_up + tp->view.rows - 2 > tp->nr_lines)
		nr_up = tp->nr_lines - tp->view.rows + 2;
	if (nr_up != tp->nr_up) {
		tp->nr_up = nr_up;
		tty3270_rebuild_update(tp);
		tty3270_update_status(tp);
		tty3270_set_timer(tp, 1);
	}
	spin_unlock_bh(&tp->view.lock);
}

/*
 * Pass input line to tty.
 */
static void
tty3270_read_tasklet(unsigned long data)
{
	struct raw3270_request *rrq = (struct raw3270_request *)data;
	static char kreset_data = TW_KR;
	struct tty3270 *tp = container_of(rrq->view, struct tty3270, view);
	char *input;
	int len;

	spin_lock_bh(&tp->view.lock);
	/*
	 * Two AID keys are special: For 0x7d (enter) the input line
	 * has to be emitted to the tty and for 0x6d the screen
	 * needs to be redrawn.
	 */
	input = NULL;
	len = 0;
	if (tp->input->string[0] == 0x7d) {
		/* Enter: write input to tty. */
		input = tp->input->string + 6;
		len = tp->input->len - 6 - rrq->rescnt;
		if (tp->inattr != TF_INPUTN)
			tty3270_rcl_add(tp, input, len);
		if (tp->nr_up > 0) {
			tp->nr_up = 0;
			tty3270_rebuild_update(tp);
			tty3270_update_status(tp);
		}
		/* Clear input area. */
		tty3270_update_prompt(tp, NULL, 0);
		tty3270_set_timer(tp, 1);
	} else if (tp->input->string[0] == 0x6d) {
		/* Display has been cleared. Redraw. */
		tp->update_flags = TTY_UPDATE_ALL;
		tty3270_set_timer(tp, 1);
	}
	spin_unlock_bh(&tp->view.lock);

	/* Start keyboard reset command. */
	raw3270_request_reset(tp->kreset);
	raw3270_request_set_cmd(tp->kreset, TC_WRITE);
	raw3270_request_add_data(tp->kreset, &kreset_data, 1);
	raw3270_start(&tp->view, tp->kreset);

	while (len-- > 0)
		kbd_keycode(tp->kbd, *input++);
	/* Emit keycode for AID byte. */
	kbd_keycode(tp->kbd, 256 + tp->input->string[0]);

	raw3270_request_reset(rrq);
	xchg(&tp->read, rrq);
	raw3270_put_view(&tp->view);
}

/*
 * Read request completion callback.
 */
static void
tty3270_read_callback(struct raw3270_request *rq, void *data)
{
	struct tty3270 *tp = container_of(rq->view, struct tty3270, view);
	raw3270_get_view(rq->view);
	/* Schedule tasklet to pass input to tty. */
	tasklet_schedule(&tp->readlet);
}

/*
 * Issue a read request. Call with device lock.
 */
static void
tty3270_issue_read(struct tty3270 *tp, int lock)
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
	raw3270_request_set_data(rrq, tp->input->string, tp->input->len);
	/* Issue the read modified request. */
	if (lock) {
		rc = raw3270_start(&tp->view, rrq);
	} else
		rc = raw3270_start_irq(&tp->view, rrq);
	if (rc) {
		raw3270_request_reset(rrq);
		xchg(&tp->read, rrq);
	}
}

/*
 * Hang up the tty
 */
static void
tty3270_hangup_tasklet(unsigned long data)
{
	struct tty3270 *tp = (struct tty3270 *)data;
	tty_port_tty_hangup(&tp->port, true);
	raw3270_put_view(&tp->view);
}

/*
 * Switch to the tty view.
 */
static int
tty3270_activate(struct raw3270_view *view)
{
	struct tty3270 *tp = container_of(view, struct tty3270, view);

	tp->update_flags = TTY_UPDATE_ALL;
	tty3270_set_timer(tp, 1);
	return 0;
}

static void
tty3270_deactivate(struct raw3270_view *view)
{
	struct tty3270 *tp = container_of(view, struct tty3270, view);

	del_timer(&tp->timer);
}

static void
tty3270_irq(struct tty3270 *tp, struct raw3270_request *rq, struct irb *irb)
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
static struct tty3270 *
tty3270_alloc_view(void)
{
	struct tty3270 *tp;
	int pages;

	tp = kzalloc(sizeof(struct tty3270), GFP_KERNEL);
	if (!tp)
		goto out_err;
	tp->freemem_pages =
		kmalloc_array(TTY3270_STRING_PAGES, sizeof(void *),
			      GFP_KERNEL);
	if (!tp->freemem_pages)
		goto out_tp;
	INIT_LIST_HEAD(&tp->freemem);
	INIT_LIST_HEAD(&tp->lines);
	INIT_LIST_HEAD(&tp->update);
	INIT_LIST_HEAD(&tp->rcl_lines);
	tp->rcl_max = 20;

	for (pages = 0; pages < TTY3270_STRING_PAGES; pages++) {
		tp->freemem_pages[pages] = (void *)
			__get_free_pages(GFP_KERNEL|GFP_DMA, 0);
		if (!tp->freemem_pages[pages])
			goto out_pages;
		add_string_memory(&tp->freemem,
				  tp->freemem_pages[pages], PAGE_SIZE);
	}
	tp->write = raw3270_request_alloc(TTY3270_OUTPUT_BUFFER_SIZE);
	if (IS_ERR(tp->write))
		goto out_pages;
	tp->read = raw3270_request_alloc(0);
	if (IS_ERR(tp->read))
		goto out_write;
	tp->kreset = raw3270_request_alloc(1);
	if (IS_ERR(tp->kreset))
		goto out_read;
	tp->kbd = kbd_alloc();
	if (!tp->kbd)
		goto out_reset;

	tty_port_init(&tp->port);
	timer_setup(&tp->timer, tty3270_update, 0);
	tasklet_init(&tp->readlet, tty3270_read_tasklet,
		     (unsigned long) tp->read);
	tasklet_init(&tp->hanglet, tty3270_hangup_tasklet,
		     (unsigned long) tp);
	INIT_WORK(&tp->resize_work, tty3270_resize_work);

	return tp;

out_reset:
	raw3270_request_free(tp->kreset);
out_read:
	raw3270_request_free(tp->read);
out_write:
	raw3270_request_free(tp->write);
out_pages:
	while (pages--)
		free_pages((unsigned long) tp->freemem_pages[pages], 0);
	kfree(tp->freemem_pages);
	tty_port_destroy(&tp->port);
out_tp:
	kfree(tp);
out_err:
	return ERR_PTR(-ENOMEM);
}

/*
 * Free tty3270 structure.
 */
static void
tty3270_free_view(struct tty3270 *tp)
{
	int pages;

	kbd_free(tp->kbd);
	raw3270_request_free(tp->kreset);
	raw3270_request_free(tp->read);
	raw3270_request_free(tp->write);
	for (pages = 0; pages < TTY3270_STRING_PAGES; pages++)
		free_pages((unsigned long) tp->freemem_pages[pages], 0);
	kfree(tp->freemem_pages);
	tty_port_destroy(&tp->port);
	kfree(tp);
}

/*
 * Allocate tty3270 screen.
 */
static struct tty3270_line *
tty3270_alloc_screen(unsigned int rows, unsigned int cols)
{
	struct tty3270_line *screen;
	unsigned long size;
	int lines;

	size = sizeof(struct tty3270_line) * (rows - 2);
	screen = kzalloc(size, GFP_KERNEL);
	if (!screen)
		goto out_err;
	for (lines = 0; lines < rows - 2; lines++) {
		size = sizeof(struct tty3270_cell) * cols;
		screen[lines].cells = kzalloc(size, GFP_KERNEL);
		if (!screen[lines].cells)
			goto out_screen;
	}
	return screen;
out_screen:
	while (lines--)
		kfree(screen[lines].cells);
	kfree(screen);
out_err:
	return ERR_PTR(-ENOMEM);
}

/*
 * Free tty3270 screen.
 */
static void
tty3270_free_screen(struct tty3270_line *screen, unsigned int rows)
{
	int lines;

	for (lines = 0; lines < rows - 2; lines++)
		kfree(screen[lines].cells);
	kfree(screen);
}

/*
 * Resize tty3270 screen
 */
static void tty3270_resize_work(struct work_struct *work)
{
	struct tty3270 *tp = container_of(work, struct tty3270, resize_work);
	struct tty3270_line *screen, *oscreen;
	struct tty_struct *tty;
	unsigned int orows;
	struct winsize ws;

	screen = tty3270_alloc_screen(tp->n_rows, tp->n_cols);
	if (IS_ERR(screen))
		return;
	/* Switch to new output size */
	spin_lock_bh(&tp->view.lock);
	tty3270_blank_screen(tp);
	oscreen = tp->screen;
	orows = tp->view.rows;
	tp->view.model = tp->n_model;
	tp->view.rows = tp->n_rows;
	tp->view.cols = tp->n_cols;
	tp->screen = screen;
	free_string(&tp->freemem, tp->prompt);
	free_string(&tp->freemem, tp->status);
	tty3270_create_prompt(tp);
	tty3270_create_status(tp);
	while (tp->nr_lines < tp->view.rows - 2)
		tty3270_blank_line(tp);
	tp->update_flags = TTY_UPDATE_ALL;
	spin_unlock_bh(&tp->view.lock);
	tty3270_free_screen(oscreen, orows);
	tty3270_set_timer(tp, 1);
	/* Informat tty layer about new size */
	tty = tty_port_tty_get(&tp->port);
	if (!tty)
		return;
	ws.ws_row = tp->view.rows - 2;
	ws.ws_col = tp->view.cols;
	tty_do_resize(tty, &ws);
	tty_kref_put(tty);
}

static void
tty3270_resize(struct raw3270_view *view, int model, int rows, int cols)
{
	struct tty3270 *tp = container_of(view, struct tty3270, view);

	if (tp->n_model == model && tp->n_rows == rows && tp->n_cols == cols)
		return;
	tp->n_model = model;
	tp->n_rows = rows;
	tp->n_cols = cols;
	schedule_work(&tp->resize_work);
}

/*
 * Unlink tty3270 data structure from tty.
 */
static void
tty3270_release(struct raw3270_view *view)
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
static void
tty3270_free(struct raw3270_view *view)
{
	struct tty3270 *tp = container_of(view, struct tty3270, view);

	del_timer_sync(&tp->timer);
	tty3270_free_screen(tp->screen, tp->view.rows);
	tty3270_free_view(tp);
}

/*
 * Delayed freeing of tty3270 views.
 */
static void
tty3270_del_views(void)
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
	.intv = (void *) tty3270_irq,
	.release = tty3270_release,
	.free = tty3270_free,
	.resize = tty3270_resize
};

/*
 * This routine is called whenever a 3270 tty is opened first time.
 */
static int tty3270_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct raw3270_view *view;
	struct tty3270 *tp;
	int i, rc;

	/* Check if the tty3270 is already there. */
	view = raw3270_find_view(&tty3270_fn, tty->index + RAW3270_FIRSTMINOR);
	if (!IS_ERR(view)) {
		tp = container_of(view, struct tty3270, view);
		tty->driver_data = tp;
		tty->winsize.ws_row = tp->view.rows - 2;
		tty->winsize.ws_col = tp->view.cols;
		tp->inattr = TF_INPUT;
		goto port_install;
	}
	if (tty3270_max_index < tty->index + 1)
		tty3270_max_index = tty->index + 1;

	/* Allocate tty3270 structure on first open. */
	tp = tty3270_alloc_view();
	if (IS_ERR(tp))
		return PTR_ERR(tp);

	rc = raw3270_add_view(&tp->view, &tty3270_fn,
			      tty->index + RAW3270_FIRSTMINOR,
			      RAW3270_VIEW_LOCK_BH);
	if (rc) {
		tty3270_free_view(tp);
		return rc;
	}

	tp->screen = tty3270_alloc_screen(tp->view.rows, tp->view.cols);
	if (IS_ERR(tp->screen)) {
		rc = PTR_ERR(tp->screen);
		raw3270_put_view(&tp->view);
		raw3270_del_view(&tp->view);
		tty3270_free_view(tp);
		return rc;
	}

	tty->winsize.ws_row = tp->view.rows - 2;
	tty->winsize.ws_col = tp->view.cols;

	tty3270_create_prompt(tp);
	tty3270_create_status(tp);
	tty3270_update_status(tp);

	/* Create blank line for every line in the tty output area. */
	for (i = 0; i < tp->view.rows - 2; i++)
		tty3270_blank_line(tp);

	tp->kbd->port = &tp->port;
	tp->kbd->fn_handler[KVAL(K_INCRCONSOLE)] = tty3270_exit_tty;
	tp->kbd->fn_handler[KVAL(K_SCROLLBACK)] = tty3270_scroll_backward;
	tp->kbd->fn_handler[KVAL(K_SCROLLFORW)] = tty3270_scroll_forward;
	tp->kbd->fn_handler[KVAL(K_CONS)] = tty3270_rcl_backward;
	kbd_ascebc(tp->kbd, tp->view.ascebc);

	raw3270_activate_view(&tp->view);

port_install:
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
static int
tty3270_open(struct tty_struct *tty, struct file *filp)
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
static void
tty3270_close(struct tty_struct *tty, struct file * filp)
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
static int
tty3270_write_room(struct tty_struct *tty)
{
	return INT_MAX;
}

/*
 * Insert character into the screen at the current position with the
 * current color and highlight. This function does NOT do cursor movement.
 */
static void tty3270_put_character(struct tty3270 *tp, char ch)
{
	struct tty3270_line *line;
	struct tty3270_cell *cell;

	line = tp->screen + tp->cy;
	if (line->len <= tp->cx) {
		while (line->len < tp->cx) {
			cell = line->cells + line->len;
			cell->character = tp->view.ascebc[' '];
			cell->highlight = tp->highlight;
			cell->f_color = tp->f_color;
			line->len++;
		}
		line->len++;
	}
	cell = line->cells + tp->cx;
	cell->character = tp->view.ascebc[(unsigned int) ch];
	cell->highlight = tp->highlight;
	cell->f_color = tp->f_color;
}

/*
 * Convert a tty3270_line to a 3270 data fragment usable for output.
 */
static void
tty3270_convert_line(struct tty3270 *tp, int line_nr)
{
	struct tty3270_line *line;
	struct tty3270_cell *cell;
	struct string *s, *n;
	unsigned char highlight;
	unsigned char f_color;
	char *cp;
	int flen, i;

	/* Determine how long the fragment will be. */
	flen = 3;		/* Prefix (TO_SBA). */
	line = tp->screen + line_nr;
	flen += line->len;
	highlight = TAX_RESET;
	f_color = TAC_RESET;
	for (i = 0, cell = line->cells; i < line->len; i++, cell++) {
		if (cell->highlight != highlight) {
			flen += 3;	/* TO_SA to switch highlight. */
			highlight = cell->highlight;
		}
		if (cell->f_color != f_color) {
			flen += 3;	/* TO_SA to switch color. */
			f_color = cell->f_color;
		}
	}
	if (highlight != TAX_RESET)
		flen += 3;	/* TO_SA to reset hightlight. */
	if (f_color != TAC_RESET)
		flen += 3;	/* TO_SA to reset color. */
	if (line->len < tp->view.cols)
		flen += 4;	/* Postfix (TO_RA). */

	/* Find the line in the list. */
	i = tp->view.rows - 2 - line_nr;
	list_for_each_entry_reverse(s, &tp->lines, list)
		if (--i <= 0)
			break;
	/*
	 * Check if the line needs to get reallocated.
	 */
	if (s->len != flen) {
		/* Reallocate string. */
		n = tty3270_alloc_string(tp, flen);
		list_add(&n->list, &s->list);
		list_del_init(&s->list);
		if (!list_empty(&s->update))
			list_del_init(&s->update);
		free_string(&tp->freemem, s);
		s = n;
	}

	/* Write 3270 data fragment. */
	cp = s->string;
	*cp++ = TO_SBA;
	*cp++ = 0;
	*cp++ = 0;

	highlight = TAX_RESET;
	f_color = TAC_RESET;
	for (i = 0, cell = line->cells; i < line->len; i++, cell++) {
		if (cell->highlight != highlight) {
			*cp++ = TO_SA;
			*cp++ = TAT_EXTHI;
			*cp++ = cell->highlight;
			highlight = cell->highlight;
		}
		if (cell->f_color != f_color) {
			*cp++ = TO_SA;
			*cp++ = TAT_COLOR;
			*cp++ = cell->f_color;
			f_color = cell->f_color;
		}
		*cp++ = cell->character;
	}
	if (highlight != TAX_RESET) {
		*cp++ = TO_SA;
		*cp++ = TAT_EXTHI;
		*cp++ = TAX_RESET;
	}
	if (f_color != TAC_RESET) {
		*cp++ = TO_SA;
		*cp++ = TAT_COLOR;
		*cp++ = TAC_RESET;
	}
	if (line->len < tp->view.cols) {
		*cp++ = TO_RA;
		*cp++ = 0;
		*cp++ = 0;
		*cp++ = 0;
	}

	if (tp->nr_up + line_nr < tp->view.rows - 2) {
		/* Line is currently visible on screen. */
		tty3270_update_string(tp, s, line_nr);
		/* Add line to update list. */
		if (list_empty(&s->update)) {
			list_add_tail(&s->update, &tp->update);
			tp->update_flags |= TTY_UPDATE_LIST;
		}
	}
}

/*
 * Do carriage return.
 */
static void
tty3270_cr(struct tty3270 *tp)
{
	tp->cx = 0;
}

/*
 * Do line feed.
 */
static void
tty3270_lf(struct tty3270 *tp)
{
	struct tty3270_line temp;
	int i;

	tty3270_convert_line(tp, tp->cy);
	if (tp->cy < tp->view.rows - 3) {
		tp->cy++;
		return;
	}
	/* Last line just filled up. Add new, blank line. */
	tty3270_blank_line(tp);
	temp = tp->screen[0];
	temp.len = 0;
	for (i = 0; i < tp->view.rows - 3; i++)
		tp->screen[i] = tp->screen[i+1];
	tp->screen[tp->view.rows - 3] = temp;
	tty3270_rebuild_update(tp);
}

static void
tty3270_ri(struct tty3270 *tp)
{
	if (tp->cy > 0) {
	    tty3270_convert_line(tp, tp->cy);
	    tp->cy--;
	}
}

/*
 * Insert characters at current position.
 */
static void
tty3270_insert_characters(struct tty3270 *tp, int n)
{
	struct tty3270_line *line;
	int k;

	line = tp->screen + tp->cy;
	while (line->len < tp->cx) {
		line->cells[line->len].character = tp->view.ascebc[' '];
		line->cells[line->len].highlight = TAX_RESET;
		line->cells[line->len].f_color = TAC_RESET;
		line->len++;
	}
	if (n > tp->view.cols - tp->cx)
		n = tp->view.cols - tp->cx;
	k = min_t(int, line->len - tp->cx, tp->view.cols - tp->cx - n);
	while (k--)
		line->cells[tp->cx + n + k] = line->cells[tp->cx + k];
	line->len += n;
	if (line->len > tp->view.cols)
		line->len = tp->view.cols;
	while (n-- > 0) {
		line->cells[tp->cx + n].character = tp->view.ascebc[' '];
		line->cells[tp->cx + n].highlight = tp->highlight;
		line->cells[tp->cx + n].f_color = tp->f_color;
	}
}

/*
 * Delete characters at current position.
 */
static void
tty3270_delete_characters(struct tty3270 *tp, int n)
{
	struct tty3270_line *line;
	int i;

	line = tp->screen + tp->cy;
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
static void
tty3270_erase_characters(struct tty3270 *tp, int n)
{
	struct tty3270_line *line;
	struct tty3270_cell *cell;

	line = tp->screen + tp->cy;
	while (line->len > tp->cx && n-- > 0) {
		cell = line->cells + tp->cx++;
		cell->character = ' ';
		cell->highlight = TAX_RESET;
		cell->f_color = TAC_RESET;
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
static void
tty3270_erase_line(struct tty3270 *tp, int mode)
{
	struct tty3270_line *line;
	struct tty3270_cell *cell;
	int i;

	line = tp->screen + tp->cy;
	if (mode == 0)
		line->len = tp->cx;
	else if (mode == 1) {
		for (i = 0; i < tp->cx; i++) {
			cell = line->cells + i;
			cell->character = ' ';
			cell->highlight = TAX_RESET;
			cell->f_color = TAC_RESET;
		}
		if (line->len <= tp->cx)
			line->len = tp->cx + 1;
	} else if (mode == 2)
		line->len = 0;
	tty3270_convert_line(tp, tp->cy);
}

/*
 * Erase display, 3 different cases:
 *  Esc [ 0 J	Erase from current position to bottom of screen inclusive
 *  Esc [ 1 J	Erase from top of screen to current position inclusive
 *  Esc [ 2 J	Erase entire screen (without moving the cursor)
 */
static void
tty3270_erase_display(struct tty3270 *tp, int mode)
{
	int i;

	if (mode == 0) {
		tty3270_erase_line(tp, 0);
		for (i = tp->cy + 1; i < tp->view.rows - 2; i++) {
			tp->screen[i].len = 0;
			tty3270_convert_line(tp, i);
		}
	} else if (mode == 1) {
		for (i = 0; i < tp->cy; i++) {
			tp->screen[i].len = 0;
			tty3270_convert_line(tp, i);
		}
		tty3270_erase_line(tp, 1);
	} else if (mode == 2) {
		for (i = 0; i < tp->view.rows - 2; i++) {
			tp->screen[i].len = 0;
			tty3270_convert_line(tp, i);
		}
	}
	tty3270_rebuild_update(tp);
}

/*
 * Set attributes found in an escape sequence.
 *  Esc [ <attr> ; <attr> ; ... m
 */
static void
tty3270_set_attributes(struct tty3270 *tp)
{
	static unsigned char f_colors[] = {
		TAC_DEFAULT, TAC_RED, TAC_GREEN, TAC_YELLOW, TAC_BLUE,
		TAC_PINK, TAC_TURQ, TAC_WHITE, 0, TAC_DEFAULT
	};
	int i, attr;

	for (i = 0; i <= tp->esc_npar; i++) {
		attr = tp->esc_par[i];
		switch (attr) {
		case 0:		/* Reset */
			tp->highlight = TAX_RESET;
			tp->f_color = TAC_RESET;
			break;
		/* Highlight. */
		case 4:		/* Start underlining. */
			tp->highlight = TAX_UNDER;
			break;
		case 5:		/* Start blink. */
			tp->highlight = TAX_BLINK;
			break;
		case 7:		/* Start reverse. */
			tp->highlight = TAX_REVER;
			break;
		case 24:	/* End underlining */
			if (tp->highlight == TAX_UNDER)
				tp->highlight = TAX_RESET;
			break;
		case 25:	/* End blink. */
			if (tp->highlight == TAX_BLINK)
				tp->highlight = TAX_RESET;
			break;
		case 27:	/* End reverse. */
			if (tp->highlight == TAX_REVER)
				tp->highlight = TAX_RESET;
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
			tp->f_color = f_colors[attr - 30];
			break;
		}
	}
}

static inline int
tty3270_getpar(struct tty3270 *tp, int ix)
{
	return (tp->esc_par[ix] > 0) ? tp->esc_par[ix] : 1;
}

static void
tty3270_goto_xy(struct tty3270 *tp, int cx, int cy)
{
	int max_cx = max(0, cx);
	int max_cy = max(0, cy);

	tp->cx = min_t(int, tp->view.cols - 1, max_cx);
	cy = min_t(int, tp->view.rows - 3, max_cy);
	if (cy != tp->cy) {
		tty3270_convert_line(tp, tp->cy);
		tp->cy = cy;
	}
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
static void
tty3270_escape_sequence(struct tty3270 *tp, char ch)
{
	enum { ESnormal, ESesc, ESsquare, ESgetpars };

	if (tp->esc_state == ESnormal) {
		if (ch == 0x1b)
			/* Starting new escape sequence. */
			tp->esc_state = ESesc;
		return;
	}
	if (tp->esc_state == ESesc) {
		tp->esc_state = ESnormal;
		switch (ch) {
		case '[':
			tp->esc_state = ESsquare;
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
			tp->saved_highlight = tp->highlight;
			tp->saved_f_color = tp->f_color;
			break;
		case '8':		/* Restore cursor position. */
			tty3270_convert_line(tp, tp->cy);
			tty3270_goto_xy(tp, tp->saved_cx, tp->saved_cy);
			tp->highlight = tp->saved_highlight;
			tp->f_color = tp->saved_f_color;
			break;
		case 'c':		/* Reset terminal. */
			tp->cx = tp->saved_cx = 0;
			tp->cy = tp->saved_cy = 0;
			tp->highlight = tp->saved_highlight = TAX_RESET;
			tp->f_color = tp->saved_f_color = TAC_RESET;
			tty3270_erase_display(tp, 2);
			break;
		}
		return;
	}
	if (tp->esc_state == ESsquare) {
		tp->esc_state = ESgetpars;
		memset(tp->esc_par, 0, sizeof(tp->esc_par));
		tp->esc_npar = 0;
		tp->esc_ques = (ch == '?');
		if (tp->esc_ques)
			return;
	}
	if (tp->esc_state == ESgetpars) {
		if (ch == ';' && tp->esc_npar < ESCAPE_NPAR - 1) {
			tp->esc_npar++;
			return;
		}
		if (ch >= '0' && ch <= '9') {
			tp->esc_par[tp->esc_npar] *= 10;
			tp->esc_par[tp->esc_npar] += ch - '0';
			return;
		}
	}
	tp->esc_state = ESnormal;
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
		tp->saved_highlight = tp->highlight;
		tp->saved_f_color = tp->f_color;
		break;
	case 'u':	/* Restore cursor position. */
		tty3270_convert_line(tp, tp->cy);
		tty3270_goto_xy(tp, tp->saved_cx, tp->saved_cy);
		tp->highlight = tp->saved_highlight;
		tp->f_color = tp->saved_f_color;
		break;
	}
}

/*
 * String write routine for 3270 ttys
 */
static void
tty3270_do_write(struct tty3270 *tp, struct tty_struct *tty,
		const unsigned char *buf, int count)
{
	int i_msg, i;

	spin_lock_bh(&tp->view.lock);
	for (i_msg = 0; !tty->stopped && i_msg < count; i_msg++) {
		if (tp->esc_state != 0) {
			/* Continue escape sequence. */
			tty3270_escape_sequence(tp, buf[i_msg]);
			continue;
		}

		switch (buf[i_msg]) {
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
			tp->cx = tp->cy = 0;
			break;
		case 0x0d:		/* '\r' -- Carriage Return */
			tp->cx = 0;
			break;
		case 0x0f:		/* SuSE "exit alternate mode" */
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
	/* Convert current line to 3270 data fragment. */
	tty3270_convert_line(tp, tp->cy);

	/* Setup timer to update display after 1/10 second */
	if (!timer_pending(&tp->timer))
		tty3270_set_timer(tp, HZ/10);

	spin_unlock_bh(&tp->view.lock);
}

/*
 * String write routine for 3270 ttys
 */
static int
tty3270_write(struct tty_struct * tty,
	      const unsigned char *buf, int count)
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
static int tty3270_put_char(struct tty_struct *tty, unsigned char ch)
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
static void
tty3270_flush_chars(struct tty_struct *tty)
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
 * Returns the number of characters in the output buffer. This is
 * used in tty_wait_until_sent to wait until all characters have
 * appeared on the screen.
 */
static int
tty3270_chars_in_buffer(struct tty_struct *tty)
{
	return 0;
}

static void
tty3270_flush_buffer(struct tty_struct *tty)
{
}

/*
 * Check for visible/invisible input switches
 */
static void
tty3270_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	struct tty3270 *tp;
	int new;

	tp = tty->driver_data;
	if (!tp)
		return;
	spin_lock_bh(&tp->view.lock);
	if (L_ICANON(tty)) {
		new = L_ECHO(tty) ? TF_INPUT: TF_INPUTN;
		if (new != tp->inattr) {
			tp->inattr = new;
			tty3270_update_prompt(tp, NULL, 0);
			tty3270_set_timer(tp, 1);
		}
	}
	spin_unlock_bh(&tp->view.lock);
}

/*
 * Disable reading from a 3270 tty
 */
static void
tty3270_throttle(struct tty_struct * tty)
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
static void
tty3270_unthrottle(struct tty_struct * tty)
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
static void
tty3270_hangup(struct tty_struct *tty)
{
	struct tty3270 *tp;

	tp = tty->driver_data;
	if (!tp)
		return;
	spin_lock_bh(&tp->view.lock);
	tp->cx = tp->saved_cx = 0;
	tp->cy = tp->saved_cy = 0;
	tp->highlight = tp->saved_highlight = TAX_RESET;
	tp->f_color = tp->saved_f_color = TAC_RESET;
	tty3270_blank_screen(tp);
	while (tp->nr_lines < tp->view.rows - 2)
		tty3270_blank_line(tp);
	tp->update_flags = TTY_UPDATE_ALL;
	spin_unlock_bh(&tp->view.lock);
	tty3270_set_timer(tp, 1);
}

static void
tty3270_wait_until_sent(struct tty_struct *tty, int timeout)
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
	.chars_in_buffer = tty3270_chars_in_buffer,
	.flush_buffer = tty3270_flush_buffer,
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

static struct raw3270_notifier tty3270_notifier =
{
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
		put_tty_driver(driver);
		return ret;
	}
	tty3270_driver = driver;
	raw3270_register_notifier(&tty3270_notifier);
	return 0;
}

static void __exit
tty3270_exit(void)
{
	struct tty_driver *driver;

	raw3270_unregister_notifier(&tty3270_notifier);
	driver = tty3270_driver;
	tty3270_driver = NULL;
	tty_unregister_driver(driver);
	put_tty_driver(driver);
	tty3270_del_views();
}

MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(IBM_TTY3270_MAJOR);

module_init(tty3270_init);
module_exit(tty3270_exit);
