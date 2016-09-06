#include <linux/slab.h> /* for kmalloc */
#include <linux/consolemap.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/device.h> /* for dev_warn */
#include <linux/selection.h>
#include <linux/workqueue.h>
#include <linux/tty.h>
#include <asm/cmpxchg.h>

#include "speakup.h"

/* ------ cut and paste ----- */
/* Don't take this from <ctype.h>: 011-015 on the screen aren't spaces */
#define ishardspace(c)      ((c) == ' ')

unsigned short spk_xs, spk_ys, spk_xe, spk_ye; /* our region points */

/* Variables for selection control. */
/* must not be deallocated */
struct vc_data *spk_sel_cons;
/* cleared by clear_selection */
static int sel_start = -1;
static int sel_end;
static int sel_buffer_lth;
static char *sel_buffer;

static unsigned char sel_pos(int n)
{
	return inverse_translate(spk_sel_cons,
		screen_glyph(spk_sel_cons, n), 0);
}

void speakup_clear_selection(void)
{
	sel_start = -1;
}

/* does screen address p correspond to character at LH/RH edge of screen? */
static int atedge(const int p, int size_row)
{
	return !(p % size_row) || !((p + 2) % size_row);
}

/* constrain v such that v <= u */
static unsigned short limit(const unsigned short v, const unsigned short u)
{
	return (v > u) ? u : v;
}

int speakup_set_selection(struct tty_struct *tty)
{
	int new_sel_start, new_sel_end;
	char *bp, *obp;
	int i, ps, pe;
	struct vc_data *vc = vc_cons[fg_console].d;

	spk_xs = limit(spk_xs, vc->vc_cols - 1);
	spk_ys = limit(spk_ys, vc->vc_rows - 1);
	spk_xe = limit(spk_xe, vc->vc_cols - 1);
	spk_ye = limit(spk_ye, vc->vc_rows - 1);
	ps = spk_ys * vc->vc_size_row + (spk_xs << 1);
	pe = spk_ye * vc->vc_size_row + (spk_xe << 1);

	if (ps > pe) {
		/* make sel_start <= sel_end */
		int tmp = ps;
		ps = pe;
		pe = tmp;
	}

	if (spk_sel_cons != vc_cons[fg_console].d) {
		speakup_clear_selection();
		spk_sel_cons = vc_cons[fg_console].d;
		dev_warn(tty->dev,
			"Selection: mark console not the same as cut\n");
		return -EINVAL;
	}

	new_sel_start = ps;
	new_sel_end = pe;

	/* select to end of line if on trailing space */
	if (new_sel_end > new_sel_start &&
	    !atedge(new_sel_end, vc->vc_size_row) &&
	    ishardspace(sel_pos(new_sel_end))) {
		for (pe = new_sel_end + 2; ; pe += 2)
			if (!ishardspace(sel_pos(pe)) ||
			    atedge(pe, vc->vc_size_row))
				break;
		if (ishardspace(sel_pos(pe)))
			new_sel_end = pe;
	}
	if ((new_sel_start == sel_start) && (new_sel_end == sel_end))
		return 0; /* no action required */

	sel_start = new_sel_start;
	sel_end = new_sel_end;
	/* Allocate a new buffer before freeing the old one ... */
	bp = kmalloc((sel_end-sel_start)/2+1, GFP_ATOMIC);
	if (!bp) {
		speakup_clear_selection();
		return -ENOMEM;
	}
	kfree(sel_buffer);
	sel_buffer = bp;

	obp = bp;
	for (i = sel_start; i <= sel_end; i += 2) {
		*bp = sel_pos(i);
		if (!ishardspace(*bp++))
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
	sel_buffer_lth = bp - sel_buffer;
	return 0;
}

struct speakup_paste_work {
	struct work_struct work;
	struct tty_struct *tty;
};

static void __speakup_paste_selection(struct work_struct *work)
{
	struct speakup_paste_work *spw =
		container_of(work, struct speakup_paste_work, work);
	struct tty_struct *tty = xchg(&spw->tty, NULL);
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	int pasted = 0, count;
	struct tty_ldisc *ld;
	DECLARE_WAITQUEUE(wait, current);

	ld = tty_ldisc_ref(tty);
	if (!ld)
		goto tty_unref;

	/* FIXME: this is completely unsafe */
	add_wait_queue(&vc->paste_wait, &wait);
	while (sel_buffer && sel_buffer_lth > pasted) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (test_bit(TTY_THROTTLED, &tty->flags)) {
			schedule();
			continue;
		}
		count = sel_buffer_lth - pasted;
		count = min_t(int, count, tty->receive_room);
		ld->ops->receive_buf(tty, sel_buffer + pasted, NULL, count);
		pasted += count;
	}
	remove_wait_queue(&vc->paste_wait, &wait);
	current->state = TASK_RUNNING;

	tty_ldisc_deref(ld);
tty_unref:
	tty_kref_put(tty);
}

static struct speakup_paste_work speakup_paste_work = {
	.work = __WORK_INITIALIZER(speakup_paste_work.work,
				   __speakup_paste_selection)
};

int speakup_paste_selection(struct tty_struct *tty)
{
	if (cmpxchg(&speakup_paste_work.tty, NULL, tty) != NULL)
		return -EBUSY;

	tty_kref_get(tty);
	schedule_work_on(WORK_CPU_UNBOUND, &speakup_paste_work.work);
	return 0;
}

void speakup_cancel_paste(void)
{
	cancel_work_sync(&speakup_paste_work.work);
	tty_kref_put(speakup_paste_work.tty);
}
