// SPDX-License-Identifier: GPL-2.0
/*
 * This module exports the functions:
 *
 *     'int set_selection_user(struct tiocl_selection __user *,
 *			       struct tty_struct *)'
 *     'int set_selection_kernel(struct tiocl_selection *, struct tty_struct *)'
 *     'void clear_selection(void)'
 *     'int paste_selection(struct tty_struct *)'
 *     'int sel_loadlut(char __user *)'
 *
 * Now that /dev/vcs exists, most of this can disappear again.
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/uaccess.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/consolemap.h>
#include <linux/selection.h>
#include <linux/tiocl.h>
#include <linux/console.h>
#include <linux/tty_flip.h>

#include <linux/sched/signal.h>

/* Don't take this from <ctype.h>: 011-015 on the screen aren't spaces */
#define isspace(c)	((c) == ' ')

/* FIXME: all this needs locking */
static struct vc_selection {
	struct mutex lock;
	struct vc_data *cons;			/* must not be deallocated */
	char *buffer;
	unsigned int buf_len;
	volatile int start;			/* cleared by clear_selection */
	int end;
} vc_sel = {
	.lock = __MUTEX_INITIALIZER(vc_sel.lock),
	.start = -1,
};

/* clear_selection, highlight and highlight_pointer can be called
   from interrupt (via scrollback/front) */

/* set reverse video on characters s-e of console with selection. */
static inline void highlight(const int s, const int e)
{
	invert_screen(vc_sel.cons, s, e-s+2, 1);
}

/* use complementary color to show the pointer */
static inline void highlight_pointer(const int where)
{
	complement_pos(vc_sel.cons, where);
}

static u32
sel_pos(int n, bool unicode)
{
	if (unicode)
		return screen_glyph_unicode(vc_sel.cons, n / 2);
	return inverse_translate(vc_sel.cons, screen_glyph(vc_sel.cons, n), 0);
}

/**
 *	clear_selection		-	remove current selection
 *
 *	Remove the current selection highlight, if any from the console
 *	holding the selection. The caller must hold the console lock.
 */
void clear_selection(void)
{
	highlight_pointer(-1); /* hide the pointer */
	if (vc_sel.start != -1) {
		highlight(vc_sel.start, vc_sel.end);
		vc_sel.start = -1;
	}
}
EXPORT_SYMBOL_GPL(clear_selection);

bool vc_is_sel(struct vc_data *vc)
{
	return vc == vc_sel.cons;
}

/*
 * User settable table: what characters are to be considered alphabetic?
 * 128 bits. Locked by the console lock.
 */
static u32 inwordLut[]={
  0x00000000, /* control chars     */
  0x03FFE000, /* digits and "-./"  */
  0x87FFFFFE, /* uppercase and '_' */
  0x07FFFFFE, /* lowercase         */
};

static inline int inword(const u32 c)
{
	return c > 0x7f || (( inwordLut[c>>5] >> (c & 0x1F) ) & 1);
}

/**
 *	set loadlut		-	load the LUT table
 *	@p: user table
 *
 *	Load the LUT table from user space. The caller must hold the console
 *	lock. Make a temporary copy so a partial update doesn't make a mess.
 */
int sel_loadlut(char __user *p)
{
	u32 tmplut[ARRAY_SIZE(inwordLut)];
	if (copy_from_user(tmplut, (u32 __user *)(p+4), sizeof(inwordLut)))
		return -EFAULT;
	memcpy(inwordLut, tmplut, sizeof(inwordLut));
	return 0;
}

/* does screen address p correspond to character at LH/RH edge of screen? */
static inline int atedge(const int p, int size_row)
{
	return (!(p % size_row)	|| !((p + 2) % size_row));
}

/* stores the char in UTF8 and returns the number of bytes used (1-4) */
static int store_utf8(u32 c, char *p)
{
	if (c < 0x80) {
		/*  0******* */
		p[0] = c;
		return 1;
	} else if (c < 0x800) {
		/* 110***** 10****** */
		p[0] = 0xc0 | (c >> 6);
		p[1] = 0x80 | (c & 0x3f);
		return 2;
	} else if (c < 0x10000) {
		/* 1110**** 10****** 10****** */
		p[0] = 0xe0 | (c >> 12);
		p[1] = 0x80 | ((c >> 6) & 0x3f);
		p[2] = 0x80 | (c & 0x3f);
		return 3;
	} else if (c < 0x110000) {
		/* 11110*** 10****** 10****** 10****** */
		p[0] = 0xf0 | (c >> 18);
		p[1] = 0x80 | ((c >> 12) & 0x3f);
		p[2] = 0x80 | ((c >> 6) & 0x3f);
		p[3] = 0x80 | (c & 0x3f);
		return 4;
	} else {
		/* outside Unicode, replace with U+FFFD */
		p[0] = 0xef;
		p[1] = 0xbf;
		p[2] = 0xbd;
		return 3;
	}
}

/**
 *	set_selection_user	-	set the current selection.
 *	@sel: user selection info
 *	@tty: the console tty
 *
 *	Invoked by the ioctl handle for the vt layer.
 *
 *	The entire selection process is managed under the console_lock. It's
 *	 a lot under the lock but its hardly a performance path
 */
int set_selection_user(const struct tiocl_selection __user *sel,
		       struct tty_struct *tty)
{
	struct tiocl_selection v;

	if (copy_from_user(&v, sel, sizeof(*sel)))
		return -EFAULT;

	return set_selection_kernel(&v, tty);
}

static int vc_selection_store_chars(struct vc_data *vc, bool unicode)
{
	char *bp, *obp;
	unsigned int i;

	/* Allocate a new buffer before freeing the old one ... */
	/* chars can take up to 4 bytes with unicode */
	bp = kmalloc_array((vc_sel.end - vc_sel.start) / 2 + 1, unicode ? 4 : 1,
			   GFP_KERNEL);
	if (!bp) {
		printk(KERN_WARNING "selection: kmalloc() failed\n");
		clear_selection();
		return -ENOMEM;
	}
	kfree(vc_sel.buffer);
	vc_sel.buffer = bp;

	obp = bp;
	for (i = vc_sel.start; i <= vc_sel.end; i += 2) {
		u32 c = sel_pos(i, unicode);
		if (unicode)
			bp += store_utf8(c, bp);
		else
			*bp++ = c;
		if (!isspace(c))
			obp = bp;
		if (!((i + 2) % vc->vc_size_row)) {
			/* strip trailing blanks from line and add newline,
			   unless non-space at end of line. */
			if (obp != bp) {
				bp = obp;
				*bp++ = '\r';
			}
			obp = bp;
		}
	}
	vc_sel.buf_len = bp - vc_sel.buffer;

	return 0;
}

static int vc_do_selection(struct vc_data *vc, unsigned short mode, int ps,
		int pe)
{
	int new_sel_start, new_sel_end, spc;
	bool unicode = vt_do_kdgkbmode(fg_console) == K_UNICODE;

	switch (mode) {
	case TIOCL_SELCHAR:	/* character-by-character selection */
		new_sel_start = ps;
		new_sel_end = pe;
		break;
	case TIOCL_SELWORD:	/* word-by-word selection */
		spc = isspace(sel_pos(ps, unicode));
		for (new_sel_start = ps; ; ps -= 2) {
			if ((spc && !isspace(sel_pos(ps, unicode))) ||
			    (!spc && !inword(sel_pos(ps, unicode))))
				break;
			new_sel_start = ps;
			if (!(ps % vc->vc_size_row))
				break;
		}

		spc = isspace(sel_pos(pe, unicode));
		for (new_sel_end = pe; ; pe += 2) {
			if ((spc && !isspace(sel_pos(pe, unicode))) ||
			    (!spc && !inword(sel_pos(pe, unicode))))
				break;
			new_sel_end = pe;
			if (!((pe + 2) % vc->vc_size_row))
				break;
		}
		break;
	case TIOCL_SELLINE:	/* line-by-line selection */
		new_sel_start = rounddown(ps, vc->vc_size_row);
		new_sel_end = rounddown(pe, vc->vc_size_row) +
			vc->vc_size_row - 2;
		break;
	case TIOCL_SELPOINTER:
		highlight_pointer(pe);
		return 0;
	default:
		return -EINVAL;
	}

	/* remove the pointer */
	highlight_pointer(-1);

	/* select to end of line if on trailing space */
	if (new_sel_end > new_sel_start &&
		!atedge(new_sel_end, vc->vc_size_row) &&
		isspace(sel_pos(new_sel_end, unicode))) {
		for (pe = new_sel_end + 2; ; pe += 2)
			if (!isspace(sel_pos(pe, unicode)) ||
			    atedge(pe, vc->vc_size_row))
				break;
		if (isspace(sel_pos(pe, unicode)))
			new_sel_end = pe;
	}
	if (vc_sel.start == -1)	/* no current selection */
		highlight(new_sel_start, new_sel_end);
	else if (new_sel_start == vc_sel.start)
	{
		if (new_sel_end == vc_sel.end)	/* no action required */
			return 0;
		else if (new_sel_end > vc_sel.end)	/* extend to right */
			highlight(vc_sel.end + 2, new_sel_end);
		else				/* contract from right */
			highlight(new_sel_end + 2, vc_sel.end);
	}
	else if (new_sel_end == vc_sel.end)
	{
		if (new_sel_start < vc_sel.start) /* extend to left */
			highlight(new_sel_start, vc_sel.start - 2);
		else				/* contract from left */
			highlight(vc_sel.start, new_sel_start - 2);
	}
	else	/* some other case; start selection from scratch */
	{
		clear_selection();
		highlight(new_sel_start, new_sel_end);
	}
	vc_sel.start = new_sel_start;
	vc_sel.end = new_sel_end;

	return vc_selection_store_chars(vc, unicode);
}

static int vc_selection(struct vc_data *vc, struct tiocl_selection *v,
		struct tty_struct *tty)
{
	int ps, pe;

	poke_blanked_console();

	if (v->sel_mode == TIOCL_SELCLEAR) {
		/* useful for screendump without selection highlights */
		clear_selection();
		return 0;
	}

	v->xs = min_t(u16, v->xs - 1, vc->vc_cols - 1);
	v->ys = min_t(u16, v->ys - 1, vc->vc_rows - 1);
	v->xe = min_t(u16, v->xe - 1, vc->vc_cols - 1);
	v->ye = min_t(u16, v->ye - 1, vc->vc_rows - 1);

	if (mouse_reporting() && (v->sel_mode & TIOCL_SELMOUSEREPORT)) {
		mouse_report(tty, v->sel_mode & TIOCL_SELBUTTONMASK, v->xs,
			     v->ys);
		return 0;
	}

	ps = v->ys * vc->vc_size_row + (v->xs << 1);
	pe = v->ye * vc->vc_size_row + (v->xe << 1);
	if (ps > pe)	/* make vc_sel.start <= vc_sel.end */
		swap(ps, pe);

	if (vc_sel.cons != vc) {
		clear_selection();
		vc_sel.cons = vc;
	}

	return vc_do_selection(vc, v->sel_mode, ps, pe);
}

int set_selection_kernel(struct tiocl_selection *v, struct tty_struct *tty)
{
	int ret;

	mutex_lock(&vc_sel.lock);
	console_lock();
	ret = vc_selection(vc_cons[fg_console].d, v, tty);
	console_unlock();
	mutex_unlock(&vc_sel.lock);

	return ret;
}
EXPORT_SYMBOL_GPL(set_selection_kernel);

/* Insert the contents of the selection buffer into the
 * queue of the tty associated with the current console.
 * Invoked by ioctl().
 *
 * Locking: called without locks. Calls the ldisc wrongly with
 * unsafe methods,
 */
int paste_selection(struct tty_struct *tty)
{
	struct vc_data *vc = tty->driver_data;
	int	pasted = 0;
	unsigned int count;
	struct  tty_ldisc *ld;
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

	console_lock();
	poke_blanked_console();
	console_unlock();

	ld = tty_ldisc_ref_wait(tty);
	if (!ld)
		return -EIO;	/* ldisc was hung up */
	tty_buffer_lock_exclusive(&vc->port);

	add_wait_queue(&vc->paste_wait, &wait);
	mutex_lock(&vc_sel.lock);
	while (vc_sel.buffer && vc_sel.buf_len > pasted) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
		if (tty_throttled(tty)) {
			mutex_unlock(&vc_sel.lock);
			schedule();
			mutex_lock(&vc_sel.lock);
			continue;
		}
		__set_current_state(TASK_RUNNING);
		count = vc_sel.buf_len - pasted;
		count = tty_ldisc_receive_buf(ld, vc_sel.buffer + pasted, NULL,
					      count);
		pasted += count;
	}
	mutex_unlock(&vc_sel.lock);
	remove_wait_queue(&vc->paste_wait, &wait);
	__set_current_state(TASK_RUNNING);

	tty_buffer_unlock_exclusive(&vc->port);
	tty_ldisc_deref(ld);
	return ret;
}
EXPORT_SYMBOL_GPL(paste_selection);
