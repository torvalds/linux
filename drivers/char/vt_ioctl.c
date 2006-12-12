/*
 *  linux/drivers/char/vt_ioctl.c
 *
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
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/console.h>
#include <linux/signal.h>
#include <linux/timex.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/kbd_diacr.h>
#include <linux/selection.h>

static char vt_dont_switch;
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

			key_map = (ushort *) kmalloc(sizeof(plain_map),
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
		    fnw = (char *) kmalloc(sz, GFP_KERNEL);
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
int vt_ioctl(struct tty_struct *tty, struct file * file,
	     unsigned int cmd, unsigned long arg)
{
	struct vc_data *vc = (struct vc_data *)tty->driver_data;
	struct console_font_op op;	/* used in multiple places here */
	struct kbd_struct * kbd;
	unsigned int console;
	unsigned char ucval;
	void __user *up = (void __user *)arg;
	int i, perm;
	
	console = vc->vc_num;

	if (!vc_cons_allocated(console)) 	/* impossible? */
		return -ENOIOCTLCMD;

	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or have CAP_SYS_TTY_CONFIG.
	 */
	perm = 0;
	if (current->signal->tty == tty || capable(CAP_SYS_TTY_CONFIG))
		perm = 1;
 
	kbd = kbd_table + console;
	switch (cmd) {
	case KIOCSOUND:
		if (!perm)
			return -EPERM;
		if (arg)
			arg = CLOCK_TICK_RATE / arg;
		kd_mksound(arg, 0);
		return 0;

	case KDMKTONE:
		if (!perm)
			return -EPERM;
	{
		unsigned int ticks, count;
		
		/*
		 * Generate the tone for the appropriate number of ticks.
		 * If the time is zero, turn off sound ourselves.
		 */
		ticks = HZ * ((arg >> 16) & 0xffff) / 1000;
		count = ticks ? (arg & 0xffff) : 0;
		if (count)
			count = CLOCK_TICK_RATE / count;
		kd_mksound(count, ticks);
		return 0;
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
		if (arg < GPFIRST || arg > GPLAST)
			return -EINVAL;
		return sys_ioperm(arg, 1, (cmd == KDADDIO)) ? -ENXIO : 0;

	case KDENABIO:
	case KDDISABIO:
		return sys_ioperm(GPFIRST, GPNUM,
				  (cmd == KDENABIO)) ? -ENXIO : 0;
#endif

	/* Linux m68k/i386 interface for setting the keyboard delay/repeat rate */
		
	case KDKBDREP:
	{
		struct kbd_repeat kbrep;
		int err;
		
		if (!capable(CAP_SYS_TTY_CONFIG))
			return -EPERM;

		if (copy_from_user(&kbrep, up, sizeof(struct kbd_repeat)))
			return -EFAULT;
		err = kbd_rate(&kbrep);
		if (err)
			return err;
		if (copy_to_user(up, &kbrep, sizeof(struct kbd_repeat)))
			return -EFAULT;
		return 0;
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
			return -EPERM;
		switch (arg) {
		case KD_GRAPHICS:
			break;
		case KD_TEXT0:
		case KD_TEXT1:
			arg = KD_TEXT;
		case KD_TEXT:
			break;
		default:
			return -EINVAL;
		}
		if (vc->vc_mode == (unsigned char) arg)
			return 0;
		vc->vc_mode = (unsigned char) arg;
		if (console != fg_console)
			return 0;
		/*
		 * explicitly blank/unblank the screen if switching modes
		 */
		acquire_console_sem();
		if (arg == KD_TEXT)
			do_unblank_screen(1);
		else
			do_blank_screen(1);
		release_console_sem();
		return 0;

	case KDGETMODE:
		ucval = vc->vc_mode;
		goto setint;

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
		  default:
			return -EINVAL;
		}
		tty_ldisc_flush(tty);
		return 0;

	case KDGKBMODE:
		ucval = ((kbd->kbdmode == VC_RAW) ? K_RAW :
				 (kbd->kbdmode == VC_MEDIUMRAW) ? K_MEDIUMRAW :
				 (kbd->kbdmode == VC_UNICODE) ? K_UNICODE :
				 K_XLATE);
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
			return -EINVAL;
		}
		return 0;

	case KDGKBMETA:
		ucval = (vc_kbd_mode(kbd, VC_META) ? K_ESCPREFIX : K_METABIT);
	setint:
		return put_user(ucval, (int __user *)arg); 

	case KDGETKEYCODE:
	case KDSETKEYCODE:
		if(!capable(CAP_SYS_TTY_CONFIG))
			perm=0;
		return do_kbkeycode_ioctl(cmd, up, perm);

	case KDGKBENT:
	case KDSKBENT:
		return do_kdsk_ioctl(cmd, up, perm, kbd);

	case KDGKBSENT:
	case KDSKBSENT:
		return do_kdgkb_ioctl(cmd, up, perm);

	case KDGKBDIACR:
	{
		struct kbdiacrs __user *a = up;

		if (put_user(accent_table_size, &a->kb_cnt))
			return -EFAULT;
		if (copy_to_user(a->kbdiacr, accent_table, accent_table_size*sizeof(struct kbdiacr)))
			return -EFAULT;
		return 0;
	}

	case KDSKBDIACR:
	{
		struct kbdiacrs __user *a = up;
		unsigned int ct;

		if (!perm)
			return -EPERM;
		if (get_user(ct,&a->kb_cnt))
			return -EFAULT;
		if (ct >= MAX_DIACR)
			return -EINVAL;
		accent_table_size = ct;
		if (copy_from_user(accent_table, a->kbdiacr, ct*sizeof(struct kbdiacr)))
			return -EFAULT;
		return 0;
	}

	/* the ioctls below read/set the flags usually shown in the leds */
	/* don't use them - they will go away without warning */
	case KDGKBLED:
		ucval = kbd->ledflagstate | (kbd->default_ledflagstate << 4);
		goto setchar;

	case KDSKBLED:
		if (!perm)
			return -EPERM;
		if (arg & ~0x77)
			return -EINVAL;
		kbd->ledflagstate = (arg & 7);
		kbd->default_ledflagstate = ((arg >> 4) & 7);
		set_leds();
		return 0;

	/* the ioctls below only set the lights, not the functions */
	/* for those, see KDGKBLED and KDSKBLED above */
	case KDGETLED:
		ucval = getledstate();
	setchar:
		return put_user(ucval, (char __user *)arg);

	case KDSETLED:
		if (!perm)
		  return -EPERM;
		setledstate(kbd, arg);
		return 0;

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
		  return -EPERM;
		if (!valid_signal(arg) || arg < 1 || arg == SIGKILL)
		  return -EINVAL;

		spin_lock_irq(&vt_spawn_con.lock);
		put_pid(vt_spawn_con.pid);
		vt_spawn_con.pid = get_pid(task_pid(current));
		vt_spawn_con.sig = arg;
		spin_unlock_irq(&vt_spawn_con.lock);
		return 0;
	}

	case VT_SETMODE:
	{
		struct vt_mode tmp;

		if (!perm)
			return -EPERM;
		if (copy_from_user(&tmp, up, sizeof(struct vt_mode)))
			return -EFAULT;
		if (tmp.mode != VT_AUTO && tmp.mode != VT_PROCESS)
			return -EINVAL;
		acquire_console_sem();
		vc->vt_mode = tmp;
		/* the frsig is ignored, so we set it to 0 */
		vc->vt_mode.frsig = 0;
		put_pid(xchg(&vc->vt_pid, get_pid(task_pid(current))));
		/* no switch is required -- saw@shade.msu.ru */
		vc->vt_newvt = -1;
		release_console_sem();
		return 0;
	}

	case VT_GETMODE:
	{
		struct vt_mode tmp;
		int rc;

		acquire_console_sem();
		memcpy(&tmp, &vc->vt_mode, sizeof(struct vt_mode));
		release_console_sem();

		rc = copy_to_user(up, &tmp, sizeof(struct vt_mode));
		return rc ? -EFAULT : 0;
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
		for (i = 0, mask = 2; i < MAX_NR_CONSOLES && mask; ++i, mask <<= 1)
			if (VT_IS_IN_USE(i))
				state |= mask;
		return put_user(state, &vtstat->v_state);
	}

	/*
	 * Returns the first available (non-opened) console.
	 */
	case VT_OPENQRY:
		for (i = 0; i < MAX_NR_CONSOLES; ++i)
			if (! VT_IS_IN_USE(i))
				break;
		ucval = i < MAX_NR_CONSOLES ? (i+1) : -1;
		goto setint;		 

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
		acquire_console_sem();
		i = vc_allocate(arg);
		release_console_sem();
		if (i)
			return i;
		set_console(arg);
		return 0;

	/*
	 * wait until the specified VT has been activated
	 */
	case VT_WAITACTIVE:
		if (!perm)
			return -EPERM;
		if (arg == 0 || arg > MAX_NR_CONSOLES)
			return -ENXIO;
		return vt_waitactive(arg-1);

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
		if (vc->vt_mode.mode != VT_PROCESS)
			return -EINVAL;

		/*
		 * Switching-from response
		 */
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
				acquire_console_sem();
				newvt = vc->vt_newvt;
				vc->vt_newvt = -1;
				i = vc_allocate(newvt);
				if (i) {
					release_console_sem();
					return i;
				}
				/*
				 * When we actually do the console switch,
				 * make sure we are atomic with respect to
				 * other console switches..
				 */
				complete_change_console(vc_cons[newvt].d);
				release_console_sem();
			}
		}

		/*
		 * Switched-to response
		 */
		else
		{
			/*
			 * If it's just an ACK, ignore it
			 */
			if (arg != VT_ACKACQ)
				return -EINVAL;
		}

		return 0;

	 /*
	  * Disallocate memory associated to VT (but leave VT1)
	  */
	 case VT_DISALLOCATE:
		if (arg > MAX_NR_CONSOLES)
			return -ENXIO;
		if (arg == 0) {
		    /* deallocate all unused consoles, but leave 0 */
			acquire_console_sem();
			for (i=1; i<MAX_NR_CONSOLES; i++)
				if (! VT_BUSY(i))
					vc_deallocate(i);
			release_console_sem();
		} else {
			/* deallocate a single console, if possible */
			arg--;
			if (VT_BUSY(arg))
				return -EBUSY;
			if (arg) {			      /* leave 0 */
				acquire_console_sem();
				vc_deallocate(arg);
				release_console_sem();
			}
		}
		return 0;

	case VT_RESIZE:
	{
		struct vt_sizes __user *vtsizes = up;
		ushort ll,cc;
		if (!perm)
			return -EPERM;
		if (get_user(ll, &vtsizes->v_rows) ||
		    get_user(cc, &vtsizes->v_cols))
			return -EFAULT;
		for (i = 0; i < MAX_NR_CONSOLES; i++)
			vc_lock_resize(vc_cons[i].d, cc, ll);
		return 0;
	}

	case VT_RESIZEX:
	{
		struct vt_consize __user *vtconsize = up;
		ushort ll,cc,vlin,clin,vcol,ccol;
		if (!perm)
			return -EPERM;
		if (!access_ok(VERIFY_READ, vtconsize,
				sizeof(struct vt_consize)))
			return -EFAULT;
		__get_user(ll, &vtconsize->v_rows);
		__get_user(cc, &vtconsize->v_cols);
		__get_user(vlin, &vtconsize->v_vlin);
		__get_user(clin, &vtconsize->v_clin);
		__get_user(vcol, &vtconsize->v_vcol);
		__get_user(ccol, &vtconsize->v_ccol);
		vlin = vlin ? vlin : vc->vc_scan_lines;
		if (clin) {
			if (ll) {
				if (ll != vlin/clin)
					return -EINVAL; /* Parameters don't add up */
			} else 
				ll = vlin/clin;
		}
		if (vcol && ccol) {
			if (cc) {
				if (cc != vcol/ccol)
					return -EINVAL;
			} else
				cc = vcol/ccol;
		}

		if (clin > 32)
			return -EINVAL;
		    
		for (i = 0; i < MAX_NR_CONSOLES; i++) {
			if (!vc_cons[i].d)
				continue;
			acquire_console_sem();
			if (vlin)
				vc_cons[i].d->vc_scan_lines = vlin;
			if (clin)
				vc_cons[i].d->vc_font.height = clin;
			vc_resize(vc_cons[i].d, cc, ll);
			release_console_sem();
		}
  		return 0;
	}

	case PIO_FONT: {
		if (!perm)
			return -EPERM;
		op.op = KD_FONT_OP_SET;
		op.flags = KD_FONT_FLAG_OLD | KD_FONT_FLAG_DONT_RECALC;	/* Compatibility */
		op.width = 8;
		op.height = 0;
		op.charcount = 256;
		op.data = up;
		return con_font_op(vc_cons[fg_console].d, &op);
	}

	case GIO_FONT: {
		op.op = KD_FONT_OP_GET;
		op.flags = KD_FONT_FLAG_OLD;
		op.width = 8;
		op.height = 32;
		op.charcount = 256;
		op.data = up;
		return con_font_op(vc_cons[fg_console].d, &op);
	}

	case PIO_CMAP:
                if (!perm)
			return -EPERM;
                return con_set_cmap(up);

	case GIO_CMAP:
                return con_get_cmap(up);

	case PIO_FONTX:
	case GIO_FONTX:
		return do_fontx_ioctl(cmd, up, perm, &op);

	case PIO_FONTRESET:
	{
		if (!perm)
			return -EPERM;

#ifdef BROKEN_GRAPHICS_PROGRAMS
		/* With BROKEN_GRAPHICS_PROGRAMS defined, the default
		   font is not saved. */
		return -ENOSYS;
#else
		{
		op.op = KD_FONT_OP_SET_DEFAULT;
		op.data = NULL;
		i = con_font_op(vc_cons[fg_console].d, &op);
		if (i)
			return i;
		con_set_default_unimap(vc_cons[fg_console].d);
		return 0;
		}
#endif
	}

	case KDFONTOP: {
		if (copy_from_user(&op, up, sizeof(op)))
			return -EFAULT;
		if (!perm && op.op != KD_FONT_OP_GET)
			return -EPERM;
		i = con_font_op(vc, &op);
		if (i) return i;
		if (copy_to_user(up, &op, sizeof(op)))
			return -EFAULT;
		return 0;
	}

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
	      { struct unimapinit ui;
		if (!perm)
			return -EPERM;
		i = copy_from_user(&ui, up, sizeof(struct unimapinit));
		if (i) return -EFAULT;
		con_clear_unimap(vc, &ui);
		return 0;
	      }

	case PIO_UNIMAP:
	case GIO_UNIMAP:
		return do_unimap_ioctl(cmd, up, perm, vc);

	case VT_LOCKSWITCH:
		if (!capable(CAP_SYS_TTY_CONFIG))
		   return -EPERM;
		vt_dont_switch = 1;
		return 0;
	case VT_UNLOCKSWITCH:
		if (!capable(CAP_SYS_TTY_CONFIG))
		   return -EPERM;
		vt_dont_switch = 0;
		return 0;
	case VT_GETHIFONTMASK:
		return put_user(vc->vc_hi_font_mask, (unsigned short __user *)arg);
	default:
		return -ENOIOCTLCMD;
	}
}

/*
 * Sometimes we want to wait until a particular VT has been activated. We
 * do it in a very simple manner. Everybody waits on a single queue and
 * get woken up at once. Those that are satisfied go on with their business,
 * while those not ready go back to sleep. Seems overkill to add a wait
 * to each vt just for this - usually this does nothing!
 */
static DECLARE_WAIT_QUEUE_HEAD(vt_activate_queue);

/*
 * Sleeps until a vt is activated, or the task is interrupted. Returns
 * 0 if activation, -EINTR if interrupted.
 */
int vt_waitactive(int vt)
{
	int retval;
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&vt_activate_queue, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		retval = 0;
		if (vt == fg_console)
			break;
		retval = -EINTR;
		if (signal_pending(current))
			break;
		schedule();
	}
	remove_wait_queue(&vt_activate_queue, &wait);
	current->state = TASK_RUNNING;
	return retval;
}

#define vt_wake_waitactive() wake_up(&vt_activate_queue)

void reset_vc(struct vc_data *vc)
{
	vc->vc_mode = KD_TEXT;
	kbd_table[vc->vc_num].kbdmode = VC_XLATE;
	vc->vt_mode.mode = VT_AUTO;
	vc->vt_mode.waitv = 0;
	vc->vt_mode.relsig = 0;
	vc->vt_mode.acqsig = 0;
	vc->vt_mode.frsig = 0;
	put_pid(xchg(&vc->vt_pid, NULL));
	vc->vt_newvt = -1;
	if (!in_interrupt())    /* Via keyboard.c:SAK() - akpm */
		reset_palette(vc);
}

/*
 * Performs the back end of a vt switch
 */
static void complete_change_console(struct vc_data *vc)
{
	unsigned char old_vc_mode;

	last_console = fg_console;

	/*
	 * If we're switching, we could be going from KD_GRAPHICS to
	 * KD_TEXT mode or vice versa, which means we need to blank or
	 * unblank the screen later.
	 */
	old_vc_mode = vc_cons[fg_console].d->vc_mode;
	switch_screen(vc);

	/*
	 * This can't appear below a successful kill_proc().  If it did,
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
		 * Send the signal as privileged - kill_proc() will
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
	vt_wake_waitactive();
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
		 * Send the signal as privileged - kill_proc() will
		 * tell us if the process has gone or something else
		 * is awry
		 */
		if (kill_pid(vc->vt_pid, vc->vt_mode.relsig, 1) == 0) {
			/*
			 * It worked. Mark the vt to switch to and
			 * return. The process needs to send us a
			 * VT_RELDISP ioctl to complete the switch.
			 */
			vc->vt_newvt = new_vc->vc_num;
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
