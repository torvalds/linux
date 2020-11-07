// SPDX-License-Identifier: GPL-2.0
#include <linux/slab.h> /* for kmalloc */
#include <linux/consolemap.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/device.h> /* for dev_warn */
#include <linux/selection.h>
#include <linux/workqueue.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/atomic.h>
#include <linux/console.h>

#include "speakup.h"

unsigned short spk_xs, spk_ys, spk_xe, spk_ye; /* our region points */
struct vc_data *spk_sel_cons;

struct speakup_selection_work {
	struct work_struct work;
	struct tiocl_selection sel;
	struct tty_struct *tty;
};

static void __speakup_set_selection(struct work_struct *work)
{
	struct speakup_selection_work *ssw =
		container_of(work, struct speakup_selection_work, work);

	struct tty_struct *tty;
	struct tiocl_selection sel;

	sel = ssw->sel;

	/* this ensures we copy sel before releasing the lock below */
	rmb();

	/* release the lock by setting tty of the struct to NULL */
	tty = xchg(&ssw->tty, NULL);

	if (spk_sel_cons != vc_cons[fg_console].d) {
		spk_sel_cons = vc_cons[fg_console].d;
		pr_warn("Selection: mark console not the same as cut\n");
		goto unref;
	}

	console_lock();
	clear_selection();
	console_unlock();

	set_selection_kernel(&sel, tty);

unref:
	tty_kref_put(tty);
}

static struct speakup_selection_work speakup_sel_work = {
	.work = __WORK_INITIALIZER(speakup_sel_work.work,
				   __speakup_set_selection)
};

int speakup_set_selection(struct tty_struct *tty)
{
	/* we get kref here first in order to avoid a subtle race when
	 * cancelling selection work. getting kref first establishes the
	 * invariant that if speakup_sel_work.tty is not NULL when
	 * speakup_cancel_selection() is called, it must be the case that a put
	 * kref is pending.
	 */
	tty_kref_get(tty);
	if (cmpxchg(&speakup_sel_work.tty, NULL, tty)) {
		tty_kref_put(tty);
		return -EBUSY;
	}
	/* now we have the 'lock' by setting tty member of
	 * speakup_selection_work. wmb() ensures that writes to
	 * speakup_sel_work don't happen before cmpxchg() above.
	 */
	wmb();

	speakup_sel_work.sel.xs = spk_xs + 1;
	speakup_sel_work.sel.ys = spk_ys + 1;
	speakup_sel_work.sel.xe = spk_xe + 1;
	speakup_sel_work.sel.ye = spk_ye + 1;
	speakup_sel_work.sel.sel_mode = TIOCL_SELCHAR;

	schedule_work_on(WORK_CPU_UNBOUND, &speakup_sel_work.work);

	return 0;
}

void speakup_cancel_selection(void)
{
	struct tty_struct *tty;

	cancel_work_sync(&speakup_sel_work.work);
	/* setting to null so that if work fails to run and we cancel it,
	 * we can run it again without getting EBUSY forever from there on.
	 * we need to use xchg here to avoid race with speakup_set_selection()
	 */
	tty = xchg(&speakup_sel_work.tty, NULL);
	if (tty)
		tty_kref_put(tty);
}

static void __speakup_paste_selection(struct work_struct *work)
{
	struct speakup_selection_work *ssw =
		container_of(work, struct speakup_selection_work, work);
	struct tty_struct *tty = xchg(&ssw->tty, NULL);

	paste_selection(tty);
	tty_kref_put(tty);
}

static struct speakup_selection_work speakup_paste_work = {
	.work = __WORK_INITIALIZER(speakup_paste_work.work,
				   __speakup_paste_selection)
};

int speakup_paste_selection(struct tty_struct *tty)
{
	tty_kref_get(tty);
	if (cmpxchg(&speakup_paste_work.tty, NULL, tty)) {
		tty_kref_put(tty);
		return -EBUSY;
	}

	schedule_work_on(WORK_CPU_UNBOUND, &speakup_paste_work.work);
	return 0;
}

void speakup_cancel_paste(void)
{
	struct tty_struct *tty;

	cancel_work_sync(&speakup_paste_work.work);
	tty = xchg(&speakup_paste_work.tty, NULL);
	if (tty)
		tty_kref_put(tty);
}
