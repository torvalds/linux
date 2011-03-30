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
#include <linux/sched.h>
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
#include <linux/timex.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/kbd_diacr.h>
#include <linux/selection.h>

char vt_dont_switch;
extern struct tty_driver *console_driver;

#define VT_IS_IN_USE(i)	(console_driver->ttys[i] && console_driver->ttys[i]->count)
#define VT_BUSY(i)	(VT_IS_IN_USE(i) || i == fg_console || vc_cons[i].d == sel_cons)

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
#include <linux/syscalls.h>
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
	unsigned long flags;
	/* Prepare the event */
	INIT_LIST_HEAD(&vw->list);
	vw->done = 0;
	/* Queue our event */
	spin_lock_irqsave(&vt_event_lock, flags);
	list_add(&vw->list, &vt_events);
	spin_unlock_irqrestore(&vt_event_lock, flags);
	/* Wait for it to pass */
	wait_event_interruptible_tty(vt_event_waitqueue, vw->done);
	/* Dequeue it */
	spin_lock_irqsave(&vt_event_lock, flags);
	list_del(&vw->list);
	spin_unlock_irqrestore(&vt_event_lock, flags);
}

/**
 *	vt_event_wait_ioctl	-	event ioctl handler
 *	@arg: argument to ioctl
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
 *	@event: event code
 *	@n: new console
 *
 *	Helper for event waits. Used to implement the legacy
 *	event waiting ioctls in terms of events
 */

int vt_waitactive(int n)
{
	struct vt_event_wait vw;
	do {
		if (n == fg_console + 1)
			break;
		vw.event.event = VT_EVENT_SWITCH;
		vt_event_wait(&vw);
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

#define i (tmp.kb_index)
#define s (tmp.kb_table)
#define v (tmp.kb_value)
static inline int
do_kdsk_ioctl(int cmd, struct kbentry __user *user_kbe, int perm, struct kbd_struct *kbd)
{
	struct kbentry tmp;
	ushort *key_map, val, ov;

	if (copy_from_user(&tmp, user_kbe, sizeof(struct kbentry)))
		return -EFAULT;

	if (!capable(CAP_SYS_TTY_CONFIG))
		perm = 0;

	switch (cmd) {
	case KDGKBENT:
		key_map = key_maps[s];
		if (key_map) {
		    val = U(key_map[i]);
		    if (kbd->kbdmode != VC_UNICODE && KTYP(val) >= NR_TYPES)
			val = K_HOLE;
		} else
		    val = (i ? K_HOLE : K_NOSUCHMAP);
		return put_user(val, &user_kbe->kb_value);
	case KDSKBENT:
		if (!perm)
			return -EPERM;
		if (!i && v == K_NOSUCHMAP) {
			/* deallocate map */
			key_map = key_maps[s];
			if (s && key_map) {
			    key_maps[s] = NULL;
			    if (key_map[0] == U(K_ALLOCATED)) {
					kfree(key_map);
					keymap_count--;
			    }
			}
			break;
		}

		if (KTYP(v) < NR_TYPES) {
		    if (KVAL(v) > max_vals[KTYP(v)])
				return -EINVAL;
		} else
		    if (kbd->kbdmode != VC_UNICODE)
				return -EINVAL;

		/* ++Geert: non-PC keyboards may generate keycode zero */
#if !defined(__mc68000__) && !defined(__powerpc__)
		/* assignment to entry 0 only tests validity of args */
		if (!i)
			break;
#endif

		if (!(key_map = key_maps[s])) {
			int j;

			if (keymap_count >= MAX_NR_OF_USER_KEYMAPS &&
			    !capable(CAP_SYS_RESOURCE))
				return -EPERM;

			key_map = kmalloc(sizeof(plain_map),
						     GFP_KERNEL);
			if (!key_map)
				return -ENOMEM;
			key_maps[s] = key_map;
			key_map[0] = U(K_ALLOCATED);
			for (j = 1; j < NR_KEYS; j++)
				key_map[j] = U(K_HOLE);
			keymap_count++;
		}
		ov = U(key_map[i]);
		if (v == ov)
			break;	/* nothing to do */
		/*
		 * Attention Key.
		 */
		if (((ov == K_SAK) || (v == K_SAK)) && !capable(CAP_SYS_ADMIN))
			return -EPERM;
		key_map[i] = U(v);
		if (!s && (KTYP(ov) == KT_SHIFT || KTYP(v) == KT_SHIFT))
			compute_shiftstate();
		break;
	}
	return 0;
}
#undef i
#undef s
#undef v

static inline int 
do_kbkeycode_ioctl(int cmd, struct kbkeycode __user *user_kbkc, int perm)
{
	struct kbkeycode tmp;
	int kc = 0;

	if (copy_from_user(&tmp, user_kbkc, sizeof(struct kbkeycode)))
		return -EFAULT;
	switch (cmd) {
	case KDGETKEYCODE:
		kc = getkeycode(tmp.scancode);
		if (kc >= 0)
			kc = put_user(kc, &user_kbkc->keycode);
		break;
	case KDSETKEYCODE:
		if (!perm)
			return -EPERM;
		kc = setkeycode(tmp.scancode, tmp.keycode);
		break;
	}
	return kc;
}

static inline int
do_kdgkb_ioctl(int cmd, struct kbsentry __user *user_kdgkb, int perm)
{
	struct kbsentry *kbs;
	char *p;
	u_char *q;
	u_char __user *up;
	int sz;
	int delta;
	char *first_free, *fj, *fnw;
	int i, j, k;
	int ret;

	if (!capable(CAP_SYS_TTY_CONFIG))
		perm = 0;

	kbs = kmalloc(sizeof(*kbs), GFP_KERNEL);
	if (!kbs) {
		ret = -ENOMEM;
		goto reterr;
	}

	/* we mostly copy too much here (512bytes), but who cares ;) */
	if (copy_from_user(kbs, user_kdgkb, sizeof(struct kbsentry))) {
		ret = -EFAULT;
		goto reterr;
	}
	kbs->kb_string[sizeof(kbs->kb_string)-1] = '\0';
	i = kbs->kb_func;

	switch (cmd) {
	case KDGKBSENT:
		sz = sizeof(kbs->kb_string) - 1; /* sz should have been
						  a struct member */
		up = user_kdgkb->kb_string;
		p = func_table[i];
		if(p)
			for ( ; *p && sz; p++, sz--)
				if (put_user(*p, up++)) {
					ret = -EFAULT;
					goto reterr;
				}
		if (put_user('\0', up)) {
			ret = -EFAULT;
			goto reterr;
		}
		kfree(kbs);
		return ((p && *p) ? -EOVERFLOW : 0);
	case KDSKBSENT:
		if (!perm) {
			ret = -EPERM;
			goto reterr;
		}

		q = func_table[i];
		first_free = funcbufptr + (funcbufsize - funcbufleft);
		for (j = i+1; j < MAX_NR_FUNC && !func_table[j]; j++) 
			;
		if (j < MAX_NR_FUNC)
			fj = func_table[j];
		else
			fj = first_free;

		delta = (q ? -strlen(q) : 1) + strlen(kbs->kb_string);
		if (delta <= funcbufleft) { 	/* it fits in current buf */
		    if (j < MAX_NR_FUNC) {
			memmove(fj + delta, fj, first_free - fj);
			for (k = j; k < MAX_NR_FUNC; k++)
			    if (func_table[k])
				func_table[k] += delta;
		    }
		    if (!q)
		      func_table[i] = fj;
		    funcbufleft -= delta;
		} else {			/* allocate a larger buffer */
		    sz = 256;
		    while (sz < funcbufsize - funcbufleft + delta)
		      sz <<= 1;
		    fnw = kmalloc(sz, GFP_KERNEL);
		    if(!fnw) {
		      ret = -ENOMEM;
		      goto reterr;
		    }

		    if (!q)
		      func_table[i] = fj;
		    if (fj > funcbufptr)
			memmove(fnw, funcbufptr, fj - funcbufptr);
		    for (k = 0; k < j; k++)
		      if (func_table[k])
			func_table[k] = fnw + (func_table[k] - funcbufptr);

		    if (first_free > fj) {
			memmove(fnw + (fj - funcbufptr) + delta, fj, first_free - fj);
			for (k = j; k < MAX_NR_FUNC; k++)
			  if (func_table[k])
			    func_table[k] = fnw + (func_table[k] - funcbufptr) + delta;
		    }
		    if (funcbufptr != func_buf)
		      kfree(funcbufptr);
		    funcbufptr = fnw;
		    funcbufleft = funcbufleft - delta + sz - funcbufsize;
		    funcbufsize = sz;
		}
		strcpy(func_table[i], kbs->kb_string);
		break;
	}
	ret = 0;
reterr:
	kfree(kbs);
	return ret;
}

static inline int 
do_fontx_ioctl(int cmd, struct consolefontdesc __user *user_cfd, int perm, struct console_font_op *op)
{
	struct consolefontdesc cfdarg;
	int i;

	if (copy_from_user(&cfdarg, user_cfd, sizeof(struct consolefontdesc))) 
		return -EFAULT;
 	
	switch (cmd) {
	case PIO_FONTX:
		if (!perm)
			return -EPERM;
		op->op = KD_FONT_OP_SET;
		op->flags = KD_FONT_FLAG_OLD;
		op->width = 8;
		op->height = cfdarg.charheight;
		op->charcount = cfdarg.charcount;
		op->data = cfdarg.chardata;
		return con_font_op(vc_cons[fg_console].d, op);
	case GIO_FONTX: {
		op->op = KD_FONT_OP_GET;
		op->flags = KD_FONT_FLAG_OLD;
		op->width = 8;
		op->height = cfdarg.charheight;
		op->charcount = cfdarg.charcount;
		op->data = cfdarg.chardata;
		i = con_font_op(vc_cons[fg_console].d, op);
		if (i)
			return i;
		cfdarg.charheight = op->height;
		cfdarg.charcount = op->charcount;
		if (copy_to_user(user_cfd, &cfdarg, sizeof(struct consolefontdesc)))
			return -EFAULT;
		return 0;
		}
	}
	return -EINVAL;
}

static inline int 
do_unimap_ioctl(int cmd, struct unimapdesc __user *user_ud, int perm, struct vc_data *vc)
{
	struct unimapdesc tmp;

	if (copy_from_user(&tmp, user_ud, sizeof tmp))
		return -EFAULT;
	if (tmp.entries)
		if (!access_ok(VERIFY_WRITE, tmp.entries,
				tmp.entry_ct*sizeof(struct unipair)))
			return -EFAULT;
	switch (cmd) {
	case PIO_UNIMAP:
		if (!perm)
			return -EPERM;
		return con_set_unimap(vc, tmp.entry_ct, tmp.entries);
	case GIO_UNIMAP:
		if (!perm && fg_console != vc->vc_num)
			return -EPERM;
		return con_get_unimap(vc, tmp.entry_ct, &(user_ud->entry_ct), tmp.entries);
	}
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
	struct console_font_op op;	/* used in multiple places here */
	struct kbd_struct * kbd;
	unsigned int console;
	unsigned char ucval;
	unsigned int uival;
	void __user *up = (void __user *)arg;
	int i, perm;
	int ret = 0;

	console = vc->vc_num;

	tty_lock();

	if (!vc_cons_allocated(console)) { 	/* impossible? */
		ret = -ENOIOCTLCMD;
		goto out;
	}


	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or have CAP_SYS_TTY_CONFIG.
	 */
	perm = 0;
	if (current->signal->tty == tty || capable(CAP_SYS_TTY_CONFIG))
		perm = 1;
 
	kbd = kbd_table + console;
	switch (cmd) {
	case TIOCLINUX:
		ret = tioclinux(tty, arg);
		break;
	case KIOCSOUND:
		if (!perm)
			goto eperm;
		/*
		 * The use of PIT_TICK_RATE is historic, it used to be
		 * the platform-dependent CLOCK_TICK_RATE between 2.6.12
		 * and 2.6.36, which was a minor but unfortunate ABI
		 * change.
		 */
		if (arg)
			arg = PIT_TICK_RATE / arg;
		kd_mksound(arg, 0);
		break;

	case KDMKTONE:
		if (!perm)
			goto eperm;
	{
		unsigned int ticks, count;
		
		/*
		 * Generate the tone for the appropriate number of ticks.
		 * If the time is zero, turn off sound ourselves.
		 */
		ticks = HZ * ((arg >> 16) & 0xffff) / 1000;
		count = ticks ? (arg & 0xffff) : 0;
		if (count)
			count = PIT_TICK_RATE / count;
		kd_mksound(count, ticks);
		break;
	}

	case KDGKBTYPE:
		/*
		 * this is naive.
		 */
		ucval = KB_101;
		goto setchar;

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
		 */
		if (arg < GPFIRST || arg > GPLAST) {
			ret = -EINVAL;
			break;
		}
		ret = sys_ioperm(arg, 1, (cmd == KDADDIO)) ? -ENXIO : 0;
		break;

	case KDENABIO:
	case KDDISABIO:
		ret = sys_ioperm(GPFIRST, GPNUM,
				  (cmd == KDENABIO)) ? -ENXIO : 0;
		break;
#endif

	/* Linux m68k/i386 interface for setting the keyboard delay/repeat rate */
		
	case KDKBDREP:
	{
		struct kbd_repeat kbrep;
		
		if (!capable(CAP_SYS_TTY_CONFIG))
			goto eperm;

		if (copy_from_user(&kbrep, up, sizeof(struct kbd_repeat))) {
			ret =  -EFAULT;
			break;
		}
		ret = kbd_rate(&kbrep);
		if (ret)
			break;
		if (copy_to_user(up, &kbrep, sizeof(struct kbd_repeat)))
			ret = -EFAULT;
		break;
	}

	case KDSETMODE:
		/*
		 * currently, setting the mode from KD_TEXT to KD_GRAPHICS
		 * doesn't do a whole lot. i'm not sure if it should do any
		 * restoration of modes or what...
		 *
		 * XXX It should at least call into the driver, fbdev's definitely
		 * need to restore their engine state. --BenH
		 */
		if (!perm)
			goto eperm;
		switch (arg) {
		case KD_GRAPHICS:
			break;
		case KD_TEXT0:
		case KD_TEXT1:
			arg = KD_TEXT;
		case KD_TEXT:
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
		if (vc->vc_mode == (unsigned char) arg)
			break;
		vc->vc_mode = (unsigned char) arg;
		if (console != fg_console)
			break;
		/*
		 * explicitly blank/unblank the screen if switching modes
		 */
		console_lock();
		if (arg == KD_TEXT)
			do_unblank_screen(1);
		else
			do_blank_screen(1);
		console_unlock();
		break;

	case KDGETMODE:
		uival = vc->vc_mode;
		goto setint;

	case KDMAPDISP:
	case KDUNMAPDISP:
		/*
		 * these work like a combination of mmap and KDENABIO.
		 * this could be easily finished.
		 */
		ret = -EINVAL;
		break;

	case KDSKBMODE:
		if (!perm)
			goto eperm;
		switch(arg) {
		  case K_RAW:
			kbd->kbdmode = VC_RAW;
			break;
		  case K_MEDIUMRAW:
			kbd->kbdmode = VC_MEDIUMRAW;
			break;
		  case K_XLATE:
			kbd->kbdmode = VC_XLATE;
			compute_shiftstate();
			break;
		  case K_UNICODE:
			kbd->kbdmode = VC_UNICODE;
			compute_shiftstate();
			break;
		  case K_OFF:
			kbd->kbdmode = VC_OFF;
			break;
		  default:
			ret = -EINVAL;
			goto out;
		}
		tty_ldisc_flush(tty);
		break;

	case KDGKBMODE:
		switch (kbd->kbdmode) {
		case VC_RAW:
			uival = K_RAW;
			break;
		case VC_MEDIUMRAW:
			uival = K_MEDIUMRAW;
			break;
		case VC_UNICODE:
			uival = K_UNICODE;
			break;
		case VC_OFF:
			uival = K_OFF;
			break;
		default:
			uival = K_XLATE;
			break;
		}
		goto setint;

	/* this could be folded into KDSKBMODE, but for compatibility
	   reasons it is not so easy to fold KDGKBMETA into KDGKBMODE */
	case KDSKBMETA:
		switch(arg) {
		  case K_METABIT:
			clr_vc_kbd_mode(kbd, VC_META);
			break;
		  case K_ESCPREFIX:
			set_vc_kbd_mode(kbd, VC_META);
			break;
		  default:
			ret = -EINVAL;
		}
		break;

	case KDGKBMETA:
		uival = (vc_kbd_mode(kbd, VC_META) ? K_ESCPREFIX : K_METABIT);
	setint:
		ret = put_user(uival, (int __user *)arg);
		break;

	case KDGETKEYCODE:
	case KDSETKEYCODE:
		if(!capable(CAP_SYS_TTY_CONFIG))
			perm = 0;
		ret = do_kbkeycode_ioctl(cmd, up, perm);
		break;

	case KDGKBENT:
	case KDSKBENT:
		ret = do_kdsk_ioctl(cmd, up, perm, kbd);
		break;

	case KDGKBSENT:
	case KDSKBSENT:
		ret = do_kdgkb_ioctl(cmd, up, perm);
		break;

	case KDGKBDIACR:
	{
		struct kbdiacrs __user *a = up;
		struct kbdiacr diacr;
		int i;

		if (put_user(accent_table_size, &a->kb_cnt)) {
			ret = -EFAULT;
			break;
		}
		for (i = 0; i < accent_table_size; i++) {
			diacr.diacr = conv_uni_to_8bit(accent_table[i].diacr);
			diacr.base = conv_uni_to_8bit(accent_table[i].base);
			diacr.result = conv_uni_to_8bit(accent_table[i].result);
			if (copy_to_user(a->kbdiacr + i, &diacr, sizeof(struct kbdiacr))) {
				ret = -EFAULT;
				break;
			}
		}
		break;
	}
	case KDGKBDIACRUC:
	{
		struct kbdiacrsuc __user *a = up;

		if (put_user(accent_table_size, &a->kb_cnt))
			ret = -EFAULT;
		else if (copy_to_user(a->kbdiacruc, accent_table,
				accent_table_size*sizeof(struct kbdiacruc)))
			ret = -EFAULT;
		break;
	}

	case KDSKBDIACR:
	{
		struct kbdiacrs __user *a = up;
		struct kbdiacr diacr;
		unsigned int ct;
		int i;

		if (!perm)
			goto eperm;
		if (get_user(ct,&a->kb_cnt)) {
			ret = -EFAULT;
			break;
		}
		if (ct >= MAX_DIACR) {
			ret = -EINVAL;
			break;
		}
		accent_table_size = ct;
		for (i = 0; i < ct; i++) {
			if (copy_from_user(&diacr, a->kbdiacr + i, sizeof(struct kbdiacr))) {
				ret = -EFAULT;
				break;
			}
			accent_table[i].diacr = conv_8bit_to_uni(diacr.diacr);
			accent_table[i].base = conv_8bit_to_uni(diacr.base);
			accent_table[i].result = conv_8bit_to_uni(diacr.result);
		}
		break;
	}

	case KDSKBDIACRUC:
	{
		struct kbdiacrsuc __user *a = up;
		unsigned int ct;

		if (!perm)
			goto eperm;
		if (get_user(ct,&a->kb_cnt)) {
			ret = -EFAULT;
			break;
		}
		if (ct >= MAX_DIACR) {
			ret = -EINVAL;
			break;
		}
		accent_table_size = ct;
		if (copy_from_user(accent_table, a->kbdiacruc, ct*sizeof(struct kbdiacruc)))
			ret = -EFAULT;
		break;
	}

	/* the ioctls below read/set the flags usually shown in the leds */
	/* don't use them - they will go away without warning */
	case KDGKBLED:
		ucval = kbd->ledflagstate | (kbd->default_ledflagstate << 4);
		goto setchar;

	case KDSKBLED:
		if (!perm)
			goto eperm;
		if (arg & ~0x77) {
			ret = -EINVAL;
			break;
		}
		kbd->ledflagstate = (arg & 7);
		kbd->default_ledflagstate = ((arg >> 4) & 7);
		set_leds();
		break;

	/* the ioctls below only set the lights, not the functions */
	/* for those, see KDGKBLED and KDSKBLED above */
	case KDGETLED:
		ucval = getledstate();
	setchar:
		ret = put_user(ucval, (char __user *)arg);
		break;

	case KDSETLED:
		if (!perm)
			goto eperm;
		setledstate(kbd, arg);
		break;

	/*
	 * A process can indicate its willingness to accept signals
	 * generated by pressing an appropriate key combination.
	 * Thus, one can have a daemon that e.g. spawns a new console
	 * upon a keypress and then changes to it.
	 * See also the kbrequest field of inittab(5).
	 */
	case KDSIGACCEPT:
	{
		if (!perm || !capable(CAP_KILL))
			goto eperm;
		if (!valid_signal(arg) || arg < 1 || arg == SIGKILL)
			ret = -EINVAL;
		else {
			spin_lock_irq(&vt_spawn_con.lock);
			put_pid(vt_spawn_con.pid);
			vt_spawn_con.pid = get_pid(task_pid(current));
			vt_spawn_con.sig = arg;
			spin_unlock_irq(&vt_spawn_con.lock);
		}
		break;
	}

	case VT_SETMODE:
	{
		struct vt_mode tmp;

		if (!perm)
			goto eperm;
		if (copy_from_user(&tmp, up, sizeof(struct vt_mode))) {
			ret = -EFAULT;
			goto out;
		}
		if (tmp.mode != VT_AUTO && tmp.mode != VT_PROCESS) {
			ret = -EINVAL;
			goto out;
		}
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
			ret = -EFAULT;
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
			ret = -EFAULT;
		else {
			state = 1;	/* /dev/tty0 is always open */
			for (i = 0, mask = 2; i < MAX_NR_CONSOLES && mask;
							++i, mask <<= 1)
				if (VT_IS_IN_USE(i))
					state |= mask;
			ret = put_user(state, &vtstat->v_state);
		}
		break;
	}

	/*
	 * Returns the first available (non-opened) console.
	 */
	case VT_OPENQRY:
		for (i = 0; i < MAX_NR_CONSOLES; ++i)
			if (! VT_IS_IN_USE(i))
				break;
		uival = i < MAX_NR_CONSOLES ? (i+1) : -1;
		goto setint;		 

	/*
	 * ioctl(fd, VT_ACTIVATE, num) will cause us to switch to vt # num,
	 * with num >= 1 (switches to vt 0, our console, are not allowed, just
	 * to preserve sanity).
	 */
	case VT_ACTIVATE:
		if (!perm)
			goto eperm;
		if (arg == 0 || arg > MAX_NR_CONSOLES)
			ret =  -ENXIO;
		else {
			arg--;
			console_lock();
			ret = vc_allocate(arg);
			console_unlock();
			if (ret)
				break;
			set_console(arg);
		}
		break;

	case VT_SETACTIVATE:
	{
		struct vt_setactivate vsa;

		if (!perm)
			goto eperm;

		if (copy_from_user(&vsa, (struct vt_setactivate __user *)arg,
					sizeof(struct vt_setactivate))) {
			ret = -EFAULT;
			goto out;
		}
		if (vsa.console == 0 || vsa.console > MAX_NR_CONSOLES)
			ret = -ENXIO;
		else {
			vsa.console--;
			console_lock();
			ret = vc_allocate(vsa.console);
			if (ret == 0) {
				struct vc_data *nvc;
				/* This is safe providing we don't drop the
				   console sem between vc_allocate and
				   finishing referencing nvc */
				nvc = vc_cons[vsa.console].d;
				nvc->vt_mode = vsa.mode;
				nvc->vt_mode.frsig = 0;
				put_pid(nvc->vt_pid);
				nvc->vt_pid = get_pid(task_pid(current));
			}
			console_unlock();
			if (ret)
				break;
			/* Commence switch and lock */
			set_console(vsa.console);
		}
		break;
	}

	/*
	 * wait until the specified VT has been activated
	 */
	case VT_WAITACTIVE:
		if (!perm)
			goto eperm;
		if (arg == 0 || arg > MAX_NR_CONSOLES)
			ret = -ENXIO;
		else
			ret = vt_waitactive(arg);
		break;

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
			goto eperm;

		if (vc->vt_mode.mode != VT_PROCESS) {
			ret = -EINVAL;
			break;
		}
		/*
		 * Switching-from response
		 */
		console_lock();
		if (vc->vt_newvt >= 0) {
			if (arg == 0)
				/*
				 * Switch disallowed, so forget we were trying
				 * to do it.
				 */
				vc->vt_newvt = -1;

			else {
				/*
				 * The current vt has been released, so
				 * complete the switch.
				 */
				int newvt;
				newvt = vc->vt_newvt;
				vc->vt_newvt = -1;
				ret = vc_allocate(newvt);
				if (ret) {
					console_unlock();
					break;
				}
				/*
				 * When we actually do the console switch,
				 * make sure we are atomic with respect to
				 * other console switches..
				 */
				complete_change_console(vc_cons[newvt].d);
			}
		} else {
			/*
			 * Switched-to response
			 */
			/*
			 * If it's just an ACK, ignore it
			 */
			if (arg != VT_ACKACQ)
				ret = -EINVAL;
		}
		console_unlock();
		break;

	 /*
	  * Disallocate memory associated to VT (but leave VT1)
	  */
	 case VT_DISALLOCATE:
		if (arg > MAX_NR_CONSOLES) {
			ret = -ENXIO;
			break;
		}
		if (arg == 0) {
		    /* deallocate all unused consoles, but leave 0 */
			console_lock();
			for (i=1; i<MAX_NR_CONSOLES; i++)
				if (! VT_BUSY(i))
					vc_deallocate(i);
			console_unlock();
		} else {
			/* deallocate a single console, if possible */
			arg--;
			if (VT_BUSY(arg))
				ret = -EBUSY;
			else if (arg) {			      /* leave 0 */
				console_lock();
				vc_deallocate(arg);
				console_unlock();
			}
		}
		break;

	case VT_RESIZE:
	{
		struct vt_sizes __user *vtsizes = up;
		struct vc_data *vc;

		ushort ll,cc;
		if (!perm)
			goto eperm;
		if (get_user(ll, &vtsizes->v_rows) ||
		    get_user(cc, &vtsizes->v_cols))
			ret = -EFAULT;
		else {
			console_lock();
			for (i = 0; i < MAX_NR_CONSOLES; i++) {
				vc = vc_cons[i].d;

				if (vc) {
					vc->vc_resize_user = 1;
					vc_resize(vc_cons[i].d, cc, ll);
				}
			}
			console_unlock();
		}
		break;
	}

	case VT_RESIZEX:
	{
		struct vt_consize __user *vtconsize = up;
		ushort ll,cc,vlin,clin,vcol,ccol;
		if (!perm)
			goto eperm;
		if (!access_ok(VERIFY_READ, vtconsize,
				sizeof(struct vt_consize))) {
			ret = -EFAULT;
			break;
		}
		/* FIXME: Should check the copies properly */
		__get_user(ll, &vtconsize->v_rows);
		__get_user(cc, &vtconsize->v_cols);
		__get_user(vlin, &vtconsize->v_vlin);
		__get_user(clin, &vtconsize->v_clin);
		__get_user(vcol, &vtconsize->v_vcol);
		__get_user(ccol, &vtconsize->v_ccol);
		vlin = vlin ? vlin : vc->vc_scan_lines;
		if (clin) {
			if (ll) {
				if (ll != vlin/clin) {
					/* Parameters don't add up */
					ret = -EINVAL;
					break;
				}
			} else 
				ll = vlin/clin;
		}
		if (vcol && ccol) {
			if (cc) {
				if (cc != vcol/ccol) {
					ret = -EINVAL;
					break;
				}
			} else
				cc = vcol/ccol;
		}

		if (clin > 32) {
			ret =  -EINVAL;
			break;
		}
		    
		for (i = 0; i < MAX_NR_CONSOLES; i++) {
			if (!vc_cons[i].d)
				continue;
			console_lock();
			if (vlin)
				vc_cons[i].d->vc_scan_lines = vlin;
			if (clin)
				vc_cons[i].d->vc_font.height = clin;
			vc_cons[i].d->vc_resize_user = 1;
			vc_resize(vc_cons[i].d, cc, ll);
			console_unlock();
		}
		break;
	}

	case PIO_FONT: {
		if (!perm)
			goto eperm;
		op.op = KD_FONT_OP_SET;
		op.flags = KD_FONT_FLAG_OLD | KD_FONT_FLAG_DONT_RECALC;	/* Compatibility */
		op.width = 8;
		op.height = 0;
		op.charcount = 256;
		op.data = up;
		ret = con_font_op(vc_cons[fg_console].d, &op);
		break;
	}

	case GIO_FONT: {
		op.op = KD_FONT_OP_GET;
		op.flags = KD_FONT_FLAG_OLD;
		op.width = 8;
		op.height = 32;
		op.charcount = 256;
		op.data = up;
		ret = con_font_op(vc_cons[fg_console].d, &op);
		break;
	}

	case PIO_CMAP:
                if (!perm)
			ret = -EPERM;
		else
	                ret = con_set_cmap(up);
		break;

	case GIO_CMAP:
                ret = con_get_cmap(up);
		break;

	case PIO_FONTX:
	case GIO_FONTX:
		ret = do_fontx_ioctl(cmd, up, perm, &op);
		break;

	case PIO_FONTRESET:
	{
		if (!perm)
			goto eperm;

#ifdef BROKEN_GRAPHICS_PROGRAMS
		/* With BROKEN_GRAPHICS_PROGRAMS defined, the default
		   font is not saved. */
		ret = -ENOSYS;
		break;
#else
		{
		op.op = KD_FONT_OP_SET_DEFAULT;
		op.data = NULL;
		ret = con_font_op(vc_cons[fg_console].d, &op);
		if (ret)
			break;
		con_set_default_unimap(vc_cons[fg_console].d);
		break;
		}
#endif
	}

	case KDFONTOP: {
		if (copy_from_user(&op, up, sizeof(op))) {
			ret = -EFAULT;
			break;
		}
		if (!perm && op.op != KD_FONT_OP_GET)
			goto eperm;
		ret = con_font_op(vc, &op);
		if (ret)
			break;
		if (copy_to_user(up, &op, sizeof(op)))
			ret = -EFAULT;
		break;
	}

	case PIO_SCRNMAP:
		if (!perm)
			ret = -EPERM;
		else
			ret = con_set_trans_old(up);
		break;

	case GIO_SCRNMAP:
		ret = con_get_trans_old(up);
		break;

	case PIO_UNISCRNMAP:
		if (!perm)
			ret = -EPERM;
		else
			ret = con_set_trans_new(up);
		break;

	case GIO_UNISCRNMAP:
		ret = con_get_trans_new(up);
		break;

	case PIO_UNIMAPCLR:
	      { struct unimapinit ui;
		if (!perm)
			goto eperm;
		ret = copy_from_user(&ui, up, sizeof(struct unimapinit));
		if (ret)
			ret = -EFAULT;
		else
			con_clear_unimap(vc, &ui);
		break;
	      }

	case PIO_UNIMAP:
	case GIO_UNIMAP:
		ret = do_unimap_ioctl(cmd, up, perm, vc);
		break;

	case VT_LOCKSWITCH:
		if (!capable(CAP_SYS_TTY_CONFIG))
			goto eperm;
		vt_dont_switch = 1;
		break;
	case VT_UNLOCKSWITCH:
		if (!capable(CAP_SYS_TTY_CONFIG))
			goto eperm;
		vt_dont_switch = 0;
		break;
	case VT_GETHIFONTMASK:
		ret = put_user(vc->vc_hi_font_mask,
					(unsigned short __user *)arg);
		break;
	case VT_WAITEVENT:
		ret = vt_event_wait_ioctl((struct vt_event __user *)arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}
out:
	tty_unlock();
	return ret;
eperm:
	ret = -EPERM;
	goto out;
}

void reset_vc(struct vc_data *vc)
{
	vc->vc_mode = KD_TEXT;
	kbd_table[vc->vc_num].kbdmode = default_utf8 ? VC_UNICODE : VC_XLATE;
	vc->vt_mode.mode = VT_AUTO;
	vc->vt_mode.waitv = 0;
	vc->vt_mode.relsig = 0;
	vc->vt_mode.acqsig = 0;
	vc->vt_mode.frsig = 0;
	put_pid(vc->vt_pid);
	vc->vt_pid = NULL;
	vc->vt_newvt = -1;
	if (!in_interrupt())    /* Via keyboard.c:SAK() - akpm */
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

struct compat_consolefontdesc {
	unsigned short charcount;       /* characters in font (256 or 512) */
	unsigned short charheight;      /* scan lines per character (1-32) */
	compat_caddr_t chardata;	/* font data in expanded form */
};

static inline int
compat_fontx_ioctl(int cmd, struct compat_consolefontdesc __user *user_cfd,
			 int perm, struct console_font_op *op)
{
	struct compat_consolefontdesc cfdarg;
	int i;

	if (copy_from_user(&cfdarg, user_cfd, sizeof(struct compat_consolefontdesc)))
		return -EFAULT;

	switch (cmd) {
	case PIO_FONTX:
		if (!perm)
			return -EPERM;
		op->op = KD_FONT_OP_SET;
		op->flags = KD_FONT_FLAG_OLD;
		op->width = 8;
		op->height = cfdarg.charheight;
		op->charcount = cfdarg.charcount;
		op->data = compat_ptr(cfdarg.chardata);
		return con_font_op(vc_cons[fg_console].d, op);
	case GIO_FONTX:
		op->op = KD_FONT_OP_GET;
		op->flags = KD_FONT_FLAG_OLD;
		op->width = 8;
		op->height = cfdarg.charheight;
		op->charcount = cfdarg.charcount;
		op->data = compat_ptr(cfdarg.chardata);
		i = con_font_op(vc_cons[fg_console].d, op);
		if (i)
			return i;
		cfdarg.charheight = op->height;
		cfdarg.charcount = op->charcount;
		if (copy_to_user(user_cfd, &cfdarg, sizeof(struct compat_consolefontdesc)))
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}

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
	op->flags |= KD_FONT_FLAG_OLD;
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
	if (tmp_entries)
		if (!access_ok(VERIFY_WRITE, tmp_entries,
				tmp.entry_ct*sizeof(struct unipair)))
			return -EFAULT;
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
	unsigned int console;
	void __user *up = (void __user *)arg;
	int perm;
	int ret = 0;

	console = vc->vc_num;

	tty_lock();

	if (!vc_cons_allocated(console)) { 	/* impossible? */
		ret = -ENOIOCTLCMD;
		goto out;
	}

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
	case PIO_FONTX:
	case GIO_FONTX:
		ret = compat_fontx_ioctl(cmd, up, perm, &op);
		break;

	case KDFONTOP:
		ret = compat_kdfontop_ioctl(up, perm, &op, vc);
		break;

	case PIO_UNIMAP:
	case GIO_UNIMAP:
		ret = compat_unimap_ioctl(cmd, up, perm, vc);
		break;

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
		goto fallback;

	/*
	 * the rest has a compatible data structure behind arg,
	 * but we have to convert it to a proper 64 bit pointer.
	 */
	default:
		arg = (unsigned long)compat_ptr(arg);
		goto fallback;
	}
out:
	tty_unlock();
	return ret;

fallback:
	tty_unlock();
	return vt_ioctl(tty, cmd, arg);
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
	tty_lock();
	if (vt_waitactive(vt + 1)) {
		pr_debug("Suspend: Can't switch VCs.");
		tty_unlock();
		return -EINTR;
	}
	tty_unlock();
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
