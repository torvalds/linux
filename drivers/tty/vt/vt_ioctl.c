// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1992 obz under the linux copyright
 *
 *  Dynamic diacritical handling - aeb@cwi.nl - Dec 1993
 *  Dynamic keymap and string allocation - aeb@cwi.nl - May 1994
 *  Restrict VT switching via ioctl() - grif@cs.ucr.edu - Dec 1995
 *  Some code moved for less code duplication - Andi Kleen - Mar 1997
 *  Check put/get_user, cleanups - acme@conectiva.com.br - Jun 2001
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/compat.h>
#include <linux/module.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/console.h>
#include <linux/consolemap.h>
#include <linux/signal.h>
#include <linux/suspend.h>
#include <linux/timex.h>

#include <asm/io.h>
#include <linux/uaccess.h>

#include <linux/nospec.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/kbd_diacr.h>
#include <linux/selection.h>

bool vt_dont_switch;

static inline bool vt_in_use(unsigned int i)
{
	const struct vc_data *vc = vc_cons[i].d;

	/*
	 * console_lock must be held to prevent the vc from being deallocated
	 * while we're checking whether it's in-use.
	 */
	WARN_CONSOLE_UNLOCKED();

	return vc && kref_read(&vc->port.kref) > 1;
}

static inline bool vt_busy(int i)
{
	if (vt_in_use(i))
		return true;
	if (i == fg_console)
		return true;
	if (vc_is_sel(vc_cons[i].d))
		return true;

	return false;
}

/*
 * Console (vt and kd) routines, as defined by USL SVR4 manual, and by
 * experimentation and study of X386 SYSV handling.
 *
 * One point of difference: SYSV vt's are /dev/vtX, which X >= 0, and
 * /dev/console is a separate ttyp. Under Linux, /dev/tty0 is /dev/console,
 * and the vc start at /dev/ttyX, X >= 1. We maintain that here, so we will
 * always treat our set of vt as numbered 1..MAX_NR_CONSOLES (corresponding to
 * ttys 0..MAX_NR_CONSOLES-1). Explicitly naming VT 0 is illegal, but using
 * /dev/tty0 (fg_console) as a target is legal, since an implicit aliasing
 * to the current console is done by the main ioctl code.
 */

#ifdef CONFIG_X86
#include <asm/syscalls.h>
#endif

static void complete_change_console(struct vc_data *vc);

/*
 *	User space VT_EVENT handlers
 */

struct vt_event_wait {
	struct list_head list;
	struct vt_event event;
	int done;
};

static LIST_HEAD(vt_events);
static DEFINE_SPINLOCK(vt_event_lock);
static DECLARE_WAIT_QUEUE_HEAD(vt_event_waitqueue);

/**
 *	vt_event_post
 *	@event: the event that occurred
 *	@old: old console
 *	@new: new console
 *
 *	Post an VT event to interested VT handlers
 */

void vt_event_post(unsigned int event, unsigned int old, unsigned int new)
{
	struct list_head *pos, *head;
	unsigned long flags;
	int wake = 0;

	spin_lock_irqsave(&vt_event_lock, flags);
	head = &vt_events;

	list_for_each(pos, head) {
		struct vt_event_wait *ve = list_entry(pos,
						struct vt_event_wait, list);
		if (!(ve->event.event & event))
			continue;
		ve->event.event = event;
		/* kernel view is consoles 0..n-1, user space view is
		   console 1..n with 0 meaning current, so we must bias */
		ve->event.oldev = old + 1;
		ve->event.newev = new + 1;
		wake = 1;
		ve->done = 1;
	}
	spin_unlock_irqrestore(&vt_event_lock, flags);
	if (wake)
		wake_up_interruptible(&vt_event_waitqueue);
}

static void __vt_event_queue(struct vt_event_wait *vw)
{
	unsigned long flags;
	/* Prepare the event */
	INIT_LIST_HEAD(&vw->list);
	vw->done = 0;
	/* Queue our event */
	spin_lock_irqsave(&vt_event_lock, flags);
	list_add(&vw->list, &vt_events);
	spin_unlock_irqrestore(&vt_event_lock, flags);
}

static void __vt_event_wait(struct vt_event_wait *vw)
{
	/* Wait for it to pass */
	wait_event_interruptible(vt_event_waitqueue, vw->done);
}

static void __vt_event_dequeue(struct vt_event_wait *vw)
{
	unsigned long flags;

	/* Dequeue it */
	spin_lock_irqsave(&vt_event_lock, flags);
	list_del(&vw->list);
	spin_unlock_irqrestore(&vt_event_lock, flags);
}

/**
 *	vt_event_wait		-	wait for an event
 *	@vw: our event
 *
 *	Waits for an event to occur which completes our vt_event_wait
 *	structure. On return the structure has wv->done set to 1 for success
 *	or 0 if some event such as a signal ended the wait.
 */

static void vt_event_wait(struct vt_event_wait *vw)
{
	__vt_event_queue(vw);
	__vt_event_wait(vw);
	__vt_event_dequeue(vw);
}

/**
 *	vt_event_wait_ioctl	-	event ioctl handler
 *	@event: argument to ioctl (the event)
 *
 *	Implement the VT_WAITEVENT ioctl using the VT event interface
 */

static int vt_event_wait_ioctl(struct vt_event __user *event)
{
	struct vt_event_wait vw;

	if (copy_from_user(&vw.event, event, sizeof(struct vt_event)))
		return -EFAULT;
	/* Highest supported event for now */
	if (vw.event.event & ~VT_MAX_EVENT)
		return -EINVAL;

	vt_event_wait(&vw);
	/* If it occurred report it */
	if (vw.done) {
		if (copy_to_user(event, &vw.event, sizeof(struct vt_event)))
			return -EFAULT;
		return 0;
	}
	return -EINTR;
}

/**
 *	vt_waitactive	-	active console wait
 *	@n: new console
 *
 *	Helper for event waits. Used to implement the legacy
 *	event waiting ioctls in terms of events
 */

int vt_waitactive(int n)
{
	struct vt_event_wait vw;
	do {
		vw.event.event = VT_EVENT_SWITCH;
		__vt_event_queue(&vw);
		if (n == fg_console + 1) {
			__vt_event_dequeue(&vw);
			break;
		}
		__vt_event_wait(&vw);
		__vt_event_dequeue(&vw);
		if (vw.done == 0)
			return -EINTR;
	} while (vw.event.newev != n);
	return 0;
}

/*
 * these are the valid i/o ports we're allowed to change. they map all the
 * video ports
 */
#define GPFIRST 0x3b4
#define GPLAST 0x3df
#define GPNUM (GPLAST - GPFIRST + 1)

/*
 * currently, setting the mode from KD_TEXT to KD_GRAPHICS doesn't do a whole
 * lot. i'm not sure if it should do any restoration of modes or what...
 *
 * XXX It should at least call into the driver, fbdev's definitely need to
 * restore their engine state. --BenH
 */
static int vt_kdsetmode(struct vc_data *vc, unsigned long mode)
{
	switch (mode) {
	case KD_GRAPHICS:
		break;
	case KD_TEXT0:
	case KD_TEXT1:
		mode = KD_TEXT;
		fallthrough;
	case KD_TEXT:
		break;
	default:
		return -EINVAL;
	}

	/* FIXME: this needs the console lock extending */
	if (vc->vc_mode == mode)
		return 0;

	vc->vc_mode = mode;
	if (vc->vc_num != fg_console)
		return 0;

	/* explicitly blank/unblank the screen if switching modes */
	console_lock();
	if (mode == KD_TEXT)
		do_unblank_screen(1);
	else
		do_blank_screen(1);
	console_unlock();

	return 0;
}

static int vt_k_ioctl(struct tty_struct *tty, unsigned int cmd,
		unsigned long arg, bool perm)
{
	struct vc_data *vc = tty->driver_data;
	void __user *up = (void __user *)arg;
	unsigned int console = vc->vc_num;
	int ret;

	switch (cmd) {
	case KIOCSOUND:
		if (!perm)
			return -EPERM;
		/*
		 * The use of PIT_TICK_RATE is historic, it used to be
		 * the platform-dependent CLOCK_TICK_RATE between 2.6.12
		 * and 2.6.36, which was a minor but unfortunate ABI
		 * change. kd_mksound is locked by the input layer.
		 */
		if (arg)
			arg = PIT_TICK_RATE / arg;
		kd_mksound(arg, 0);
		break;

	case KDMKTONE:
		if (!perm)
			return -EPERM;
	{
		unsigned int ticks, count;

		/*
		 * Generate the tone for the appropriate number of ticks.
		 * If the time is zero, turn off sound ourselves.
		 */
		ticks = msecs_to_jiffies((arg >> 16) & 0xffff);
		count = ticks ? (arg & 0xffff) : 0;
		if (count)
			count = PIT_TICK_RATE / count;
		kd_mksound(count, ticks);
		break;
	}

	case KDGKBTYPE:
		/*
		 * this is naïve.
		 */
		return put_user(KB_101, (char __user *)arg);

		/*
		 * These cannot be implemented on any machine that implements
		 * ioperm() in user level (such as Alpha PCs) or not at all.
		 *
		 * XXX: you should never use these, just call ioperm directly..
		 */
#ifdef CONFIG_X86
	case KDADDIO:
	case KDDELIO:
		/*
		 * KDADDIO and KDDELIO may be able to add ports beyond what
		 * we reject here, but to be safe...
		 *
		 * These are locked internally via sys_ioperm
		 */
		if (arg < GPFIRST || arg > GPLAST)
			return -EINVAL;

		return ksys_ioperm(arg, 1, (cmd == KDADDIO)) ? -ENXIO : 0;

	case KDENABIO:
	case KDDISABIO:
		return ksys_ioperm(GPFIRST, GPNUM,
				  (cmd == KDENABIO)) ? -ENXIO : 0;
#endif

	/* Linux m68k/i386 interface for setting the keyboard delay/repeat rate */

	case KDKBDREP:
	{
		struct kbd_repeat kbrep;

		if (!capable(CAP_SYS_TTY_CONFIG))
			return -EPERM;

		if (copy_from_user(&kbrep, up, sizeof(struct kbd_repeat)))
			return -EFAULT;

		ret = kbd_rate(&kbrep);
		if (ret)
			return ret;
		if (copy_to_user(up, &kbrep, sizeof(struct kbd_repeat)))
			return -EFAULT;
		break;
	}

	case KDSETMODE:
		if (!perm)
			return -EPERM;

		return vt_kdsetmode(vc, arg);

	case KDGETMODE:
		return put_user(vc->vc_mode, (int __user *)arg);

	case KDMAPDISP:
	case KDUNMAPDISP:
		/*
		 * these work like a combination of mmap and KDENABIO.
		 * this could be easily finished.
		 */
		return -EINVAL;

	case KDSKBMODE:
		if (!perm)
			return -EPERM;
		ret = vt_do_kdskbmode(console, arg);
		if (ret)
			return ret;
		tty_ldisc_flush(tty);
		break;

	case KDGKBMODE:
		return put_user(vt_do_kdgkbmode(console), (int __user *)arg);

	/* this could be folded into KDSKBMODE, but for compatibility
	   reasons it is not so easy to fold KDGKBMETA into KDGKBMODE */
	case KDSKBMETA:
		return vt_do_kdskbmeta(console, arg);

	case KDGKBMETA:
		/* FIXME: should review whether this is worth locking */
		return put_user(vt_do_kdgkbmeta(console), (int __user *)arg);

	case KDGETKEYCODE:
	case KDSETKEYCODE:
		if(!capable(CAP_SYS_TTY_CONFIG))
			perm = 0;
		return vt_do_kbkeycode_ioctl(cmd, up, perm);

	case KDGKBENT:
	case KDSKBENT:
		return vt_do_kdsk_ioctl(cmd, up, perm, console);

	case KDGKBSENT:
	case KDSKBSENT:
		return vt_do_kdgkb_ioctl(cmd, up, perm);

	/* Diacritical processing. Handled in keyboard.c as it has
	   to operate on the keyboard locks and structures */
	case KDGKBDIACR:
	case KDGKBDIACRUC:
	case KDSKBDIACR:
	case KDSKBDIACRUC:
		return vt_do_diacrit(cmd, up, perm);

	/* the ioctls below read/set the flags usually shown in the leds */
	/* don't use them - they will go away without warning */
	case KDGKBLED:
	case KDSKBLED:
	case KDGETLED:
	case KDSETLED:
		return vt_do_kdskled(console, cmd, arg, perm);

	/*
	 * A process can indicate its willingness to accept signals
	 * generated by pressing an appropriate key combination.
	 * Thus, one can have a daemon that e.g. spawns a new console
	 * upon a keypress and then changes to it.
	 * See also the kbrequest field of inittab(5).
	 */
	case KDSIGACCEPT:
		if (!perm || !capable(CAP_KILL))
			return -EPERM;
		if (!valid_signal(arg) || arg < 1 || arg == SIGKILL)
			return -EINVAL;

		spin_lock_irq(&vt_spawn_con.lock);
		put_pid(vt_spawn_con.pid);
		vt_spawn_con.pid = get_pid(task_pid(current));
		vt_spawn_con.sig = arg;
		spin_unlock_irq(&vt_spawn_con.lock);
		break;

	case KDFONTOP: {
		struct console_font_op op;

		if (copy_from_user(&op, up, sizeof(op)))
			return -EFAULT;
		if (!perm && op.op != KD_FONT_OP_GET)
			return -EPERM;
		ret = con_font_op(vc, &op);
		if (ret)
			return ret;
		if (copy_to_user(up, &op, sizeof(op)))
			return -EFAULT;
		break;
	}

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static inline int do_unimap_ioctl(int cmd, struct unimapdesc __user *user_ud,
		bool perm, struct vc_data *vc)
{
	struct unimapdesc tmp;

	if (copy_from_user(&tmp, user_ud, sizeof tmp))
		return -EFAULT;
	switch (cmd) {
	case PIO_UNIMAP:
		if (!perm)
			return -EPERM;
		return con_set_unimap(vc, tmp.entry_ct, tmp.entries);
	case GIO_UNIMAP:
		if (!perm && fg_console != vc->vc_num)
			return -EPERM;
		return con_get_unimap(vc, tmp.entry_ct, &(user_ud->entry_ct),
				tmp.entries);
	}
	return 0;
}

static int vt_io_ioctl(struct vc_data *vc, unsigned int cmd, void __user *up,
		bool perm)
{
	switch (cmd) {
	case PIO_CMAP:
		if (!perm)
			return -EPERM;
		return con_set_cmap(up);

	case GIO_CMAP:
		return con_get_cmap(up);

	case PIO_SCRNMAP:
		if (!perm)
			return -EPERM;
		return con_set_trans_old(up);

	case GIO_SCRNMAP:
		return con_get_trans_old(up);

	case PIO_UNISCRNMAP:
		if (!perm)
			return -EPERM;
		return con_set_trans_new(up);

	case GIO_UNISCRNMAP:
		return con_get_trans_new(up);

	case PIO_UNIMAPCLR:
		if (!perm)
			return -EPERM;
		con_clear_unimap(vc);
		break;

	case PIO_UNIMAP:
	case GIO_UNIMAP:
		return do_unimap_ioctl(cmd, up, perm, vc);

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static int vt_reldisp(struct vc_data *vc, unsigned int swtch)
{
	int newvt, ret;

	if (vc->vt_mode.mode != VT_PROCESS)
		return -EINVAL;

	/* Switched-to response */
	if (vc->vt_newvt < 0) {
		 /* If it's just an ACK, ignore it */
		return swtch == VT_ACKACQ ? 0 : -EINVAL;
	}

	/* Switching-from response */
	if (swtch == 0) {
		/* Switch disallowed, so forget we were trying to do it. */
		vc->vt_newvt = -1;
		return 0;
	}

	/* The current vt has been released, so complete the switch. */
	newvt = vc->vt_newvt;
	vc->vt_newvt = -1;
	ret = vc_allocate(newvt);
	if (ret)
		return ret;

	/*
	 * When we actually do the console switch, make sure we are atomic with
	 * respect to other console switches..
	 */
	complete_change_console(vc_cons[newvt].d);

	return 0;
}

static int vt_setactivate(struct vt_setactivate __user *sa)
{
	struct vt_setactivate vsa;
	struct vc_data *nvc;
	int ret;

	if (copy_from_user(&vsa, sa, sizeof(vsa)))
		return -EFAULT;
	if (vsa.console == 0 || vsa.console > MAX_NR_CONSOLES)
		return -ENXIO;

	vsa.console = array_index_nospec(vsa.console, MAX_NR_CONSOLES + 1);
	vsa.console--;
	console_lock();
	ret = vc_allocate(vsa.console);
	if (ret) {
		console_unlock();
		return ret;
	}

	/*
	 * This is safe providing we don't drop the console sem between
	 * vc_allocate and finishing referencing nvc.
	 */
	nvc = vc_cons[vsa.console].d;
	nvc->vt_mode = vsa.mode;
	nvc->vt_mode.frsig = 0;
	put_pid(nvc->vt_pid);
	nvc->vt_pid = get_pid(task_pid(current));
	console_unlock();

	/* Commence switch and lock */
	/* Review set_console locks */
	set_console(vsa.console);

	return 0;
}

/* deallocate a single console, if possible (leave 0) */
static int vt_disallocate(unsigned int vc_num)
{
	struct vc_data *vc = NULL;
	int ret = 0;

	console_lock();
	if (vt_busy(vc_num))
		ret = -EBUSY;
	else if (vc_num)
		vc = vc_deallocate(vc_num);
	console_unlock();

	if (vc && vc_num >= MIN_NR_CONSOLES)
		tty_port_put(&vc->port);

	return ret;
}

/* deallocate all unused consoles, but leave 0 */
static void vt_disallocate_all(void)
{
	struct vc_data *vc[MAX_NR_CONSOLES];
	int i;

	console_lock();
	for (i = 1; i < MAX_NR_CONSOLES; i++)
		if (!vt_busy(i))
			vc[i] = vc_deallocate(i);
		else
			vc[i] = NULL;
	console_unlock();

	for (i = 1; i < MAX_NR_CONSOLES; i++) {
		if (vc[i] && i >= MIN_NR_CONSOLES)
			tty_port_put(&vc[i]->port);
	}
}

static int vt_resizex(struct vc_data *vc, struct vt_consize __user *cs)
{
	struct vt_consize v;
	int i;

	if (copy_from_user(&v, cs, sizeof(struct vt_consize)))
		return -EFAULT;

	if (v.v_vlin)
		pr_info_once("\"struct vt_consize\"->v_vlin is ignored. Please report if you need this.\n");
	if (v.v_clin)
		pr_info_once("\"struct vt_consize\"->v_clin is ignored. Please report if you need this.\n");

	console_lock();
	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		vc = vc_cons[i].d;

		if (vc) {
			vc->vc_resize_user = 1;
			vc_resize(vc, v.v_cols, v.v_rows);
		}
	}
	console_unlock();

	return 0;
}

/*
 * We handle the console-specific ioctl's here.  We allow the
 * capability to modify any console, not just the fg_console.
 */
int vt_ioctl(struct tty_struct *tty,
	     unsigned int cmd, unsigned long arg)
{
	struct vc_data *vc = tty->driver_data;
	void __user *up = (void __user *)arg;
	int i, perm;
	int ret;

	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or have CAP_SYS_TTY_CONFIG.
	 */
	perm = 0;
	if (current->signal->tty == tty || capable(CAP_SYS_TTY_CONFIG))
		perm = 1;

	ret = vt_k_ioctl(tty, cmd, arg, perm);
	if (ret != -ENOIOCTLCMD)
		return ret;

	ret = vt_io_ioctl(vc, cmd, up, perm);
	if (ret != -ENOIOCTLCMD)
		return ret;

	switch (cmd) {
	case TIOCLINUX:
		return tioclinux(tty, arg);
	case VT_SETMODE:
	{
		struct vt_mode tmp;

		if (!perm)
			return -EPERM;
		if (copy_from_user(&tmp, up, sizeof(struct vt_mode)))
			return -EFAULT;
		if (tmp.mode != VT_AUTO && tmp.mode != VT_PROCESS)
			return -EINVAL;

		console_lock();
		vc->vt_mode = tmp;
		/* the frsig is ignored, so we set it to 0 */
		vc->vt_mode.frsig = 0;
		put_pid(vc->vt_pid);
		vc->vt_pid = get_pid(task_pid(current));
		/* no switch is required -- saw@shade.msu.ru */
		vc->vt_newvt = -1;
		console_unlock();
		break;
	}

	case VT_GETMODE:
	{
		struct vt_mode tmp;
		int rc;

		console_lock();
		memcpy(&tmp, &vc->vt_mode, sizeof(struct vt_mode));
		console_unlock();

		rc = copy_to_user(up, &tmp, sizeof(struct vt_mode));
		if (rc)
			return -EFAULT;
		break;
	}

	/*
	 * Returns global vt state. Note that VT 0 is always open, since
	 * it's an alias for the current VT, and people can't use it here.
	 * We cannot return state for more than 16 VTs, since v_state is short.
	 */
	case VT_GETSTATE:
	{
		struct vt_stat __user *vtstat = up;
		unsigned short state, mask;

		if (put_user(fg_console + 1, &vtstat->v_active))
			return -EFAULT;

		state = 1;	/* /dev/tty0 is always open */
		console_lock(); /* required by vt_in_use() */
		for (i = 0, mask = 2; i < MAX_NR_CONSOLES && mask;
				++i, mask <<= 1)
			if (vt_in_use(i))
				state |= mask;
		console_unlock();
		return put_user(state, &vtstat->v_state);
	}

	/*
	 * Returns the first available (non-opened) console.
	 */
	case VT_OPENQRY:
		console_lock(); /* required by vt_in_use() */
		for (i = 0; i < MAX_NR_CONSOLES; ++i)
			if (!vt_in_use(i))
				break;
		console_unlock();
		i = i < MAX_NR_CONSOLES ? (i+1) : -1;
		return put_user(i, (int __user *)arg);

	/*
	 * ioctl(fd, VT_ACTIVATE, num) will cause us to switch to vt # num,
	 * with num >= 1 (switches to vt 0, our console, are not allowed, just
	 * to preserve sanity).
	 */
	case VT_ACTIVATE:
		if (!perm)
			return -EPERM;
		if (arg == 0 || arg > MAX_NR_CONSOLES)
			return -ENXIO;

		arg--;
		console_lock();
		ret = vc_allocate(arg);
		console_unlock();
		if (ret)
			return ret;
		set_console(arg);
		break;

	case VT_SETACTIVATE:
		if (!perm)
			return -EPERM;

		return vt_setactivate(up);

	/*
	 * wait until the specified VT has been activated
	 */
	case VT_WAITACTIVE:
		if (!perm)
			return -EPERM;
		if (arg == 0 || arg > MAX_NR_CONSOLES)
			return -ENXIO;
		return vt_waitactive(arg);

	/*
	 * If a vt is under process control, the kernel will not switch to it
	 * immediately, but postpone the operation until the process calls this
	 * ioctl, allowing the switch to complete.
	 *
	 * According to the X sources this is the behavior:
	 *	0:	pending switch-from not OK
	 *	1:	pending switch-from OK
	 *	2:	completed switch-to OK
	 */
	case VT_RELDISP:
		if (!perm)
			return -EPERM;

		console_lock();
		ret = vt_reldisp(vc, arg);
		console_unlock();

		return ret;


	 /*
	  * Disallocate memory associated to VT (but leave VT1)
	  */
	 case VT_DISALLOCATE:
		if (arg > MAX_NR_CONSOLES)
			return -ENXIO;

		if (arg == 0)
			vt_disallocate_all();
		else
			return vt_disallocate(--arg);
		break;

	case VT_RESIZE:
	{
		struct vt_sizes __user *vtsizes = up;
		struct vc_data *vc;
		ushort ll,cc;

		if (!perm)
			return -EPERM;
		if (get_user(ll, &vtsizes->v_rows) ||
		    get_user(cc, &vtsizes->v_cols))
			return -EFAULT;

		console_lock();
		for (i = 0; i < MAX_NR_CONSOLES; i++) {
			vc = vc_cons[i].d;

			if (vc) {
				vc->vc_resize_user = 1;
				/* FIXME: review v tty lock */
				vc_resize(vc_cons[i].d, cc, ll);
			}
		}
		console_unlock();
		break;
	}

	case VT_RESIZEX:
		if (!perm)
			return -EPERM;

		return vt_resizex(vc, up);

	case VT_LOCKSWITCH:
		if (!capable(CAP_SYS_TTY_CONFIG))
			return -EPERM;
		vt_dont_switch = true;
		break;
	case VT_UNLOCKSWITCH:
		if (!capable(CAP_SYS_TTY_CONFIG))
			return -EPERM;
		vt_dont_switch = false;
		break;
	case VT_GETHIFONTMASK:
		return put_user(vc->vc_hi_font_mask,
					(unsigned short __user *)arg);
	case VT_WAITEVENT:
		return vt_event_wait_ioctl((struct vt_event __user *)arg);
	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

void reset_vc(struct vc_data *vc)
{
	vc->vc_mode = KD_TEXT;
	vt_reset_unicode(vc->vc_num);
	vc->vt_mode.mode = VT_AUTO;
	vc->vt_mode.waitv = 0;
	vc->vt_mode.relsig = 0;
	vc->vt_mode.acqsig = 0;
	vc->vt_mode.frsig = 0;
	put_pid(vc->vt_pid);
	vc->vt_pid = NULL;
	vc->vt_newvt = -1;
	reset_palette(vc);
}

void vc_SAK(struct work_struct *work)
{
	struct vc *vc_con =
		container_of(work, struct vc, SAK_work);
	struct vc_data *vc;
	struct tty_struct *tty;

	console_lock();
	vc = vc_con->d;
	if (vc) {
		/* FIXME: review tty ref counting */
		tty = vc->port.tty;
		/*
		 * SAK should also work in all raw modes and reset
		 * them properly.
		 */
		if (tty)
			__do_SAK(tty);
		reset_vc(vc);
	}
	console_unlock();
}

#ifdef CONFIG_COMPAT

struct compat_console_font_op {
	compat_uint_t op;        /* operation code KD_FONT_OP_* */
	compat_uint_t flags;     /* KD_FONT_FLAG_* */
	compat_uint_t width, height;     /* font size */
	compat_uint_t charcount;
	compat_caddr_t data;    /* font data with height fixed to 32 */
};

static inline int
compat_kdfontop_ioctl(struct compat_console_font_op __user *fontop,
			 int perm, struct console_font_op *op, struct vc_data *vc)
{
	int i;

	if (copy_from_user(op, fontop, sizeof(struct compat_console_font_op)))
		return -EFAULT;
	if (!perm && op->op != KD_FONT_OP_GET)
		return -EPERM;
	op->data = compat_ptr(((struct compat_console_font_op *)op)->data);
	i = con_font_op(vc, op);
	if (i)
		return i;
	((struct compat_console_font_op *)op)->data = (unsigned long)op->data;
	if (copy_to_user(fontop, op, sizeof(struct compat_console_font_op)))
		return -EFAULT;
	return 0;
}

struct compat_unimapdesc {
	unsigned short entry_ct;
	compat_caddr_t entries;
};

static inline int
compat_unimap_ioctl(unsigned int cmd, struct compat_unimapdesc __user *user_ud,
			 int perm, struct vc_data *vc)
{
	struct compat_unimapdesc tmp;
	struct unipair __user *tmp_entries;

	if (copy_from_user(&tmp, user_ud, sizeof tmp))
		return -EFAULT;
	tmp_entries = compat_ptr(tmp.entries);
	switch (cmd) {
	case PIO_UNIMAP:
		if (!perm)
			return -EPERM;
		return con_set_unimap(vc, tmp.entry_ct, tmp_entries);
	case GIO_UNIMAP:
		if (!perm && fg_console != vc->vc_num)
			return -EPERM;
		return con_get_unimap(vc, tmp.entry_ct, &(user_ud->entry_ct), tmp_entries);
	}
	return 0;
}

long vt_compat_ioctl(struct tty_struct *tty,
	     unsigned int cmd, unsigned long arg)
{
	struct vc_data *vc = tty->driver_data;
	struct console_font_op op;	/* used in multiple places here */
	void __user *up = compat_ptr(arg);
	int perm;

	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or have CAP_SYS_TTY_CONFIG.
	 */
	perm = 0;
	if (current->signal->tty == tty || capable(CAP_SYS_TTY_CONFIG))
		perm = 1;

	switch (cmd) {
	/*
	 * these need special handlers for incompatible data structures
	 */

	case KDFONTOP:
		return compat_kdfontop_ioctl(up, perm, &op, vc);

	case PIO_UNIMAP:
	case GIO_UNIMAP:
		return compat_unimap_ioctl(cmd, up, perm, vc);

	/*
	 * all these treat 'arg' as an integer
	 */
	case KIOCSOUND:
	case KDMKTONE:
#ifdef CONFIG_X86
	case KDADDIO:
	case KDDELIO:
#endif
	case KDSETMODE:
	case KDMAPDISP:
	case KDUNMAPDISP:
	case KDSKBMODE:
	case KDSKBMETA:
	case KDSKBLED:
	case KDSETLED:
	case KDSIGACCEPT:
	case VT_ACTIVATE:
	case VT_WAITACTIVE:
	case VT_RELDISP:
	case VT_DISALLOCATE:
	case VT_RESIZE:
	case VT_RESIZEX:
		return vt_ioctl(tty, cmd, arg);

	/*
	 * the rest has a compatible data structure behind arg,
	 * but we have to convert it to a proper 64 bit pointer.
	 */
	default:
		return vt_ioctl(tty, cmd, (unsigned long)up);
	}
}


#endif /* CONFIG_COMPAT */


/*
 * Performs the back end of a vt switch. Called under the console
 * semaphore.
 */
static void complete_change_console(struct vc_data *vc)
{
	unsigned char old_vc_mode;
	int old = fg_console;

	last_console = fg_console;

	/*
	 * If we're switching, we could be going from KD_GRAPHICS to
	 * KD_TEXT mode or vice versa, which means we need to blank or
	 * unblank the screen later.
	 */
	old_vc_mode = vc_cons[fg_console].d->vc_mode;
	switch_screen(vc);

	/*
	 * This can't appear below a successful kill_pid().  If it did,
	 * then the *blank_screen operation could occur while X, having
	 * received acqsig, is waking up on another processor.  This
	 * condition can lead to overlapping accesses to the VGA range
	 * and the framebuffer (causing system lockups).
	 *
	 * To account for this we duplicate this code below only if the
	 * controlling process is gone and we've called reset_vc.
	 */
	if (old_vc_mode != vc->vc_mode) {
		if (vc->vc_mode == KD_TEXT)
			do_unblank_screen(1);
		else
			do_blank_screen(1);
	}

	/*
	 * If this new console is under process control, send it a signal
	 * telling it that it has acquired. Also check if it has died and
	 * clean up (similar to logic employed in change_console())
	 */
	if (vc->vt_mode.mode == VT_PROCESS) {
		/*
		 * Send the signal as privileged - kill_pid() will
		 * tell us if the process has gone or something else
		 * is awry
		 */
		if (kill_pid(vc->vt_pid, vc->vt_mode.acqsig, 1) != 0) {
		/*
		 * The controlling process has died, so we revert back to
		 * normal operation. In this case, we'll also change back
		 * to KD_TEXT mode. I'm not sure if this is strictly correct
		 * but it saves the agony when the X server dies and the screen
		 * remains blanked due to KD_GRAPHICS! It would be nice to do
		 * this outside of VT_PROCESS but there is no single process
		 * to account for and tracking tty count may be undesirable.
		 */
			reset_vc(vc);

			if (old_vc_mode != vc->vc_mode) {
				if (vc->vc_mode == KD_TEXT)
					do_unblank_screen(1);
				else
					do_blank_screen(1);
			}
		}
	}

	/*
	 * Wake anyone waiting for their VT to activate
	 */
	vt_event_post(VT_EVENT_SWITCH, old, vc->vc_num);
	return;
}

/*
 * Performs the front-end of a vt switch
 */
void change_console(struct vc_data *new_vc)
{
	struct vc_data *vc;

	if (!new_vc || new_vc->vc_num == fg_console || vt_dont_switch)
		return;

	/*
	 * If this vt is in process mode, then we need to handshake with
	 * that process before switching. Essentially, we store where that
	 * vt wants to switch to and wait for it to tell us when it's done
	 * (via VT_RELDISP ioctl).
	 *
	 * We also check to see if the controlling process still exists.
	 * If it doesn't, we reset this vt to auto mode and continue.
	 * This is a cheap way to track process control. The worst thing
	 * that can happen is: we send a signal to a process, it dies, and
	 * the switch gets "lost" waiting for a response; hopefully, the
	 * user will try again, we'll detect the process is gone (unless
	 * the user waits just the right amount of time :-) and revert the
	 * vt to auto control.
	 */
	vc = vc_cons[fg_console].d;
	if (vc->vt_mode.mode == VT_PROCESS) {
		/*
		 * Send the signal as privileged - kill_pid() will
		 * tell us if the process has gone or something else
		 * is awry.
		 *
		 * We need to set vt_newvt *before* sending the signal or we
		 * have a race.
		 */
		vc->vt_newvt = new_vc->vc_num;
		if (kill_pid(vc->vt_pid, vc->vt_mode.relsig, 1) == 0) {
			/*
			 * It worked. Mark the vt to switch to and
			 * return. The process needs to send us a
			 * VT_RELDISP ioctl to complete the switch.
			 */
			return;
		}

		/*
		 * The controlling process has died, so we revert back to
		 * normal operation. In this case, we'll also change back
		 * to KD_TEXT mode. I'm not sure if this is strictly correct
		 * but it saves the agony when the X server dies and the screen
		 * remains blanked due to KD_GRAPHICS! It would be nice to do
		 * this outside of VT_PROCESS but there is no single process
		 * to account for and tracking tty count may be undesirable.
		 */
		reset_vc(vc);

		/*
		 * Fall through to normal (VT_AUTO) handling of the switch...
		 */
	}

	/*
	 * Ignore all switches in KD_GRAPHICS+VT_AUTO mode
	 */
	if (vc->vc_mode == KD_GRAPHICS)
		return;

	complete_change_console(new_vc);
}

/* Perform a kernel triggered VT switch for suspend/resume */

static int disable_vt_switch;

int vt_move_to_console(unsigned int vt, int alloc)
{
	int prev;

	console_lock();
	/* Graphics mode - up to X */
	if (disable_vt_switch) {
		console_unlock();
		return 0;
	}
	prev = fg_console;

	if (alloc && vc_allocate(vt)) {
		/* we can't have a free VC for now. Too bad,
		 * we don't want to mess the screen for now. */
		console_unlock();
		return -ENOSPC;
	}

	if (set_console(vt)) {
		/*
		 * We're unable to switch to the SUSPEND_CONSOLE.
		 * Let the calling function know so it can decide
		 * what to do.
		 */
		console_unlock();
		return -EIO;
	}
	console_unlock();
	if (vt_waitactive(vt + 1)) {
		pr_debug("Suspend: Can't switch VCs.");
		return -EINTR;
	}
	return prev;
}

/*
 * Normally during a suspend, we allocate a new console and switch to it.
 * When we resume, we switch back to the original console.  This switch
 * can be slow, so on systems where the framebuffer can handle restoration
 * of video registers anyways, there's little point in doing the console
 * switch.  This function allows you to disable it by passing it '0'.
 */
void pm_set_vt_switch(int do_switch)
{
	console_lock();
	disable_vt_switch = !do_switch;
	console_unlock();
}
EXPORT_SYMBOL(pm_set_vt_switch);
