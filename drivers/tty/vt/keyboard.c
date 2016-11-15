/*
 * Written for linux by Johan Myreen as a translation from
 * the assembly version by Linus (with diacriticals added)
 *
 * Some additional features added by Christoph Niemann (ChN), March 1993
 *
 * Loadable keymaps by Risto Kankkunen, May 1993
 *
 * Diacriticals redone & other small changes, aeb@cwi.nl, June 1993
 * Added decr/incr_console, dynamic keymaps, Unicode support,
 * dynamic function/string keys, led setting,  Sept 1994
 * `Sticky' modifier keys, 951006.
 *
 * 11-11-96: SAK should now work in the raw mode (Martin Mares)
 *
 * Modified to provide 'generic' keyboard support by Hamish Macdonald
 * Merge with the m68k keyboard driver and split-off of the PC low-level
 * parts by Geert Uytterhoeven, May 1997
 *
 * 27-05-97: Added support for the Magic SysRq Key (Martin Mares)
 * 30-07-98: Dead keys redone, aeb@cwi.nl.
 * 21-08-02: Converted to input API, major cleanup. (Vojtech Pavlik)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/consolemap.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/leds.h>

#include <linux/kbd_kern.h>
#include <linux/kbd_diacr.h>
#include <linux/vt_kern.h>
#include <linux/input.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>

#include <asm/irq_regs.h>

extern void ctrl_alt_del(void);

/*
 * Exported functions/variables
 */

#define KBD_DEFMODE ((1 << VC_REPEAT) | (1 << VC_META))

#if defined(CONFIG_X86) || defined(CONFIG_PARISC)
#include <asm/kbdleds.h>
#else
static inline int kbd_defleds(void)
{
	return 0;
}
#endif

#define KBD_DEFLOCK 0

/*
 * Handler Tables.
 */

#define K_HANDLERS\
	k_self,		k_fn,		k_spec,		k_pad,\
	k_dead,		k_cons,		k_cur,		k_shift,\
	k_meta,		k_ascii,	k_lock,		k_lowercase,\
	k_slock,	k_dead2,	k_brl,		k_ignore

typedef void (k_handler_fn)(struct vc_data *vc, unsigned char value,
			    char up_flag);
static k_handler_fn K_HANDLERS;
static k_handler_fn *k_handler[16] = { K_HANDLERS };

#define FN_HANDLERS\
	fn_null,	fn_enter,	fn_show_ptregs,	fn_show_mem,\
	fn_show_state,	fn_send_intr,	fn_lastcons,	fn_caps_toggle,\
	fn_num,		fn_hold,	fn_scroll_forw,	fn_scroll_back,\
	fn_boot_it,	fn_caps_on,	fn_compose,	fn_SAK,\
	fn_dec_console, fn_inc_console, fn_spawn_con,	fn_bare_num

typedef void (fn_handler_fn)(struct vc_data *vc);
static fn_handler_fn FN_HANDLERS;
static fn_handler_fn *fn_handler[] = { FN_HANDLERS };

/*
 * Variables exported for vt_ioctl.c
 */

struct vt_spawn_console vt_spawn_con = {
	.lock = __SPIN_LOCK_UNLOCKED(vt_spawn_con.lock),
	.pid  = NULL,
	.sig  = 0,
};


/*
 * Internal Data.
 */

static struct kbd_struct kbd_table[MAX_NR_CONSOLES];
static struct kbd_struct *kbd = kbd_table;

/* maximum values each key_handler can handle */
static const int max_vals[] = {
	255, ARRAY_SIZE(func_table) - 1, ARRAY_SIZE(fn_handler) - 1, NR_PAD - 1,
	NR_DEAD - 1, 255, 3, NR_SHIFT - 1, 255, NR_ASCII - 1, NR_LOCK - 1,
	255, NR_LOCK - 1, 255, NR_BRL - 1
};

static const int NR_TYPES = ARRAY_SIZE(max_vals);

static struct input_handler kbd_handler;
static DEFINE_SPINLOCK(kbd_event_lock);
static DEFINE_SPINLOCK(led_lock);
static unsigned long key_down[BITS_TO_LONGS(KEY_CNT)];	/* keyboard key bitmap */
static unsigned char shift_down[NR_SHIFT];		/* shift state counters.. */
static bool dead_key_next;
static int npadch = -1;					/* -1 or number assembled on pad */
static unsigned int diacr;
static char rep;					/* flag telling character repeat */

static int shift_state = 0;

static unsigned int ledstate = -1U;			/* undefined */
static unsigned char ledioctl;

/*
 * Notifier list for console keyboard events
 */
static ATOMIC_NOTIFIER_HEAD(keyboard_notifier_list);

int register_keyboard_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&keyboard_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(register_keyboard_notifier);

int unregister_keyboard_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&keyboard_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_keyboard_notifier);

/*
 * Translation of scancodes to keycodes. We set them on only the first
 * keyboard in the list that accepts the scancode and keycode.
 * Explanation for not choosing the first attached keyboard anymore:
 *  USB keyboards for example have two event devices: one for all "normal"
 *  keys and one for extra function keys (like "volume up", "make coffee",
 *  etc.). So this means that scancodes for the extra function keys won't
 *  be valid for the first event device, but will be for the second.
 */

struct getset_keycode_data {
	struct input_keymap_entry ke;
	int error;
};

static int getkeycode_helper(struct input_handle *handle, void *data)
{
	struct getset_keycode_data *d = data;

	d->error = input_get_keycode(handle->dev, &d->ke);

	return d->error == 0; /* stop as soon as we successfully get one */
}

static int getkeycode(unsigned int scancode)
{
	struct getset_keycode_data d = {
		.ke	= {
			.flags		= 0,
			.len		= sizeof(scancode),
			.keycode	= 0,
		},
		.error	= -ENODEV,
	};

	memcpy(d.ke.scancode, &scancode, sizeof(scancode));

	input_handler_for_each_handle(&kbd_handler, &d, getkeycode_helper);

	return d.error ?: d.ke.keycode;
}

static int setkeycode_helper(struct input_handle *handle, void *data)
{
	struct getset_keycode_data *d = data;

	d->error = input_set_keycode(handle->dev, &d->ke);

	return d->error == 0; /* stop as soon as we successfully set one */
}

static int setkeycode(unsigned int scancode, unsigned int keycode)
{
	struct getset_keycode_data d = {
		.ke	= {
			.flags		= 0,
			.len		= sizeof(scancode),
			.keycode	= keycode,
		},
		.error	= -ENODEV,
	};

	memcpy(d.ke.scancode, &scancode, sizeof(scancode));

	input_handler_for_each_handle(&kbd_handler, &d, setkeycode_helper);

	return d.error;
}

/*
 * Making beeps and bells. Note that we prefer beeps to bells, but when
 * shutting the sound off we do both.
 */

static int kd_sound_helper(struct input_handle *handle, void *data)
{
	unsigned int *hz = data;
	struct input_dev *dev = handle->dev;

	if (test_bit(EV_SND, dev->evbit)) {
		if (test_bit(SND_TONE, dev->sndbit)) {
			input_inject_event(handle, EV_SND, SND_TONE, *hz);
			if (*hz)
				return 0;
		}
		if (test_bit(SND_BELL, dev->sndbit))
			input_inject_event(handle, EV_SND, SND_BELL, *hz ? 1 : 0);
	}

	return 0;
}

static void kd_nosound(unsigned long ignored)
{
	static unsigned int zero;

	input_handler_for_each_handle(&kbd_handler, &zero, kd_sound_helper);
}

static DEFINE_TIMER(kd_mksound_timer, kd_nosound, 0, 0);

void kd_mksound(unsigned int hz, unsigned int ticks)
{
	del_timer_sync(&kd_mksound_timer);

	input_handler_for_each_handle(&kbd_handler, &hz, kd_sound_helper);

	if (hz && ticks)
		mod_timer(&kd_mksound_timer, jiffies + ticks);
}
EXPORT_SYMBOL(kd_mksound);

/*
 * Setting the keyboard rate.
 */

static int kbd_rate_helper(struct input_handle *handle, void *data)
{
	struct input_dev *dev = handle->dev;
	struct kbd_repeat *rpt = data;

	if (test_bit(EV_REP, dev->evbit)) {

		if (rpt[0].delay > 0)
			input_inject_event(handle,
					   EV_REP, REP_DELAY, rpt[0].delay);
		if (rpt[0].period > 0)
			input_inject_event(handle,
					   EV_REP, REP_PERIOD, rpt[0].period);

		rpt[1].delay = dev->rep[REP_DELAY];
		rpt[1].period = dev->rep[REP_PERIOD];
	}

	return 0;
}

int kbd_rate(struct kbd_repeat *rpt)
{
	struct kbd_repeat data[2] = { *rpt };

	input_handler_for_each_handle(&kbd_handler, data, kbd_rate_helper);
	*rpt = data[1];	/* Copy currently used settings */

	return 0;
}

/*
 * Helper Functions.
 */
static void put_queue(struct vc_data *vc, int ch)
{
	tty_insert_flip_char(&vc->port, ch, 0);
	tty_schedule_flip(&vc->port);
}

static void puts_queue(struct vc_data *vc, char *cp)
{
	while (*cp) {
		tty_insert_flip_char(&vc->port, *cp, 0);
		cp++;
	}
	tty_schedule_flip(&vc->port);
}

static void applkey(struct vc_data *vc, int key, char mode)
{
	static char buf[] = { 0x1b, 'O', 0x00, 0x00 };

	buf[1] = (mode ? 'O' : '[');
	buf[2] = key;
	puts_queue(vc, buf);
}

/*
 * Many other routines do put_queue, but I think either
 * they produce ASCII, or they produce some user-assigned
 * string, and in both cases we might assume that it is
 * in utf-8 already.
 */
static void to_utf8(struct vc_data *vc, uint c)
{
	if (c < 0x80)
		/*  0******* */
		put_queue(vc, c);
	else if (c < 0x800) {
		/* 110***** 10****** */
		put_queue(vc, 0xc0 | (c >> 6));
		put_queue(vc, 0x80 | (c & 0x3f));
	} else if (c < 0x10000) {
		if (c >= 0xD800 && c < 0xE000)
			return;
		if (c == 0xFFFF)
			return;
		/* 1110**** 10****** 10****** */
		put_queue(vc, 0xe0 | (c >> 12));
		put_queue(vc, 0x80 | ((c >> 6) & 0x3f));
		put_queue(vc, 0x80 | (c & 0x3f));
	} else if (c < 0x110000) {
		/* 11110*** 10****** 10****** 10****** */
		put_queue(vc, 0xf0 | (c >> 18));
		put_queue(vc, 0x80 | ((c >> 12) & 0x3f));
		put_queue(vc, 0x80 | ((c >> 6) & 0x3f));
		put_queue(vc, 0x80 | (c & 0x3f));
	}
}

/*
 * Called after returning from RAW mode or when changing consoles - recompute
 * shift_down[] and shift_state from key_down[] maybe called when keymap is
 * undefined, so that shiftkey release is seen. The caller must hold the
 * kbd_event_lock.
 */

static void do_compute_shiftstate(void)
{
	unsigned int k, sym, val;

	shift_state = 0;
	memset(shift_down, 0, sizeof(shift_down));

	for_each_set_bit(k, key_down, min(NR_KEYS, KEY_CNT)) {
		sym = U(key_maps[0][k]);
		if (KTYP(sym) != KT_SHIFT && KTYP(sym) != KT_SLOCK)
			continue;

		val = KVAL(sym);
		if (val == KVAL(K_CAPSSHIFT))
			val = KVAL(K_SHIFT);

		shift_down[val]++;
		shift_state |= BIT(val);
	}
}

/* We still have to export this method to vt.c */
void compute_shiftstate(void)
{
	unsigned long flags;
	spin_lock_irqsave(&kbd_event_lock, flags);
	do_compute_shiftstate();
	spin_unlock_irqrestore(&kbd_event_lock, flags);
}

/*
 * We have a combining character DIACR here, followed by the character CH.
 * If the combination occurs in the table, return the corresponding value.
 * Otherwise, if CH is a space or equals DIACR, return DIACR.
 * Otherwise, conclude that DIACR was not combining after all,
 * queue it and return CH.
 */
static unsigned int handle_diacr(struct vc_data *vc, unsigned int ch)
{
	unsigned int d = diacr;
	unsigned int i;

	diacr = 0;

	if ((d & ~0xff) == BRL_UC_ROW) {
		if ((ch & ~0xff) == BRL_UC_ROW)
			return d | ch;
	} else {
		for (i = 0; i < accent_table_size; i++)
			if (accent_table[i].diacr == d && accent_table[i].base == ch)
				return accent_table[i].result;
	}

	if (ch == ' ' || ch == (BRL_UC_ROW|0) || ch == d)
		return d;

	if (kbd->kbdmode == VC_UNICODE)
		to_utf8(vc, d);
	else {
		int c = conv_uni_to_8bit(d);
		if (c != -1)
			put_queue(vc, c);
	}

	return ch;
}

/*
 * Special function handlers
 */
static void fn_enter(struct vc_data *vc)
{
	if (diacr) {
		if (kbd->kbdmode == VC_UNICODE)
			to_utf8(vc, diacr);
		else {
			int c = conv_uni_to_8bit(diacr);
			if (c != -1)
				put_queue(vc, c);
		}
		diacr = 0;
	}

	put_queue(vc, 13);
	if (vc_kbd_mode(kbd, VC_CRLF))
		put_queue(vc, 10);
}

static void fn_caps_toggle(struct vc_data *vc)
{
	if (rep)
		return;

	chg_vc_kbd_led(kbd, VC_CAPSLOCK);
}

static void fn_caps_on(struct vc_data *vc)
{
	if (rep)
		return;

	set_vc_kbd_led(kbd, VC_CAPSLOCK);
}

static void fn_show_ptregs(struct vc_data *vc)
{
	struct pt_regs *regs = get_irq_regs();

	if (regs)
		show_regs(regs);
}

static void fn_hold(struct vc_data *vc)
{
	struct tty_struct *tty = vc->port.tty;

	if (rep || !tty)
		return;

	/*
	 * Note: SCROLLOCK will be set (cleared) by stop_tty (start_tty);
	 * these routines are also activated by ^S/^Q.
	 * (And SCROLLOCK can also be set by the ioctl KDSKBLED.)
	 */
	if (tty->stopped)
		start_tty(tty);
	else
		stop_tty(tty);
}

static void fn_num(struct vc_data *vc)
{
	if (vc_kbd_mode(kbd, VC_APPLIC))
		applkey(vc, 'P', 1);
	else
		fn_bare_num(vc);
}

/*
 * Bind this to Shift-NumLock if you work in application keypad mode
 * but want to be able to change the NumLock flag.
 * Bind this to NumLock if you prefer that the NumLock key always
 * changes the NumLock flag.
 */
static void fn_bare_num(struct vc_data *vc)
{
	if (!rep)
		chg_vc_kbd_led(kbd, VC_NUMLOCK);
}

static void fn_lastcons(struct vc_data *vc)
{
	/* switch to the last used console, ChN */
	set_console(last_console);
}

static void fn_dec_console(struct vc_data *vc)
{
	int i, cur = fg_console;

	/* Currently switching?  Queue this next switch relative to that. */
	if (want_console != -1)
		cur = want_console;

	for (i = cur - 1; i != cur; i--) {
		if (i == -1)
			i = MAX_NR_CONSOLES - 1;
		if (vc_cons_allocated(i))
			break;
	}
	set_console(i);
}

static void fn_inc_console(struct vc_data *vc)
{
	int i, cur = fg_console;

	/* Currently switching?  Queue this next switch relative to that. */
	if (want_console != -1)
		cur = want_console;

	for (i = cur+1; i != cur; i++) {
		if (i == MAX_NR_CONSOLES)
			i = 0;
		if (vc_cons_allocated(i))
			break;
	}
	set_console(i);
}

static void fn_send_intr(struct vc_data *vc)
{
	tty_insert_flip_char(&vc->port, 0, TTY_BREAK);
	tty_schedule_flip(&vc->port);
}

static void fn_scroll_forw(struct vc_data *vc)
{
	scrollfront(vc, 0);
}

static void fn_scroll_back(struct vc_data *vc)
{
	scrollback(vc);
}

static void fn_show_mem(struct vc_data *vc)
{
	show_mem(0);
}

static void fn_show_state(struct vc_data *vc)
{
	show_state();
}

static void fn_boot_it(struct vc_data *vc)
{
	ctrl_alt_del();
}

static void fn_compose(struct vc_data *vc)
{
	dead_key_next = true;
}

static void fn_spawn_con(struct vc_data *vc)
{
	spin_lock(&vt_spawn_con.lock);
	if (vt_spawn_con.pid)
		if (kill_pid(vt_spawn_con.pid, vt_spawn_con.sig, 1)) {
			put_pid(vt_spawn_con.pid);
			vt_spawn_con.pid = NULL;
		}
	spin_unlock(&vt_spawn_con.lock);
}

static void fn_SAK(struct vc_data *vc)
{
	struct work_struct *SAK_work = &vc_cons[fg_console].SAK_work;
	schedule_work(SAK_work);
}

static void fn_null(struct vc_data *vc)
{
	do_compute_shiftstate();
}

/*
 * Special key handlers
 */
static void k_ignore(struct vc_data *vc, unsigned char value, char up_flag)
{
}

static void k_spec(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	if (value >= ARRAY_SIZE(fn_handler))
		return;
	if ((kbd->kbdmode == VC_RAW ||
	     kbd->kbdmode == VC_MEDIUMRAW ||
	     kbd->kbdmode == VC_OFF) &&
	     value != KVAL(K_SAK))
		return;		/* SAK is allowed even in raw mode */
	fn_handler[value](vc);
}

static void k_lowercase(struct vc_data *vc, unsigned char value, char up_flag)
{
	pr_err("k_lowercase was called - impossible\n");
}

static void k_unicode(struct vc_data *vc, unsigned int value, char up_flag)
{
	if (up_flag)
		return;		/* no action, if this is a key release */

	if (diacr)
		value = handle_diacr(vc, value);

	if (dead_key_next) {
		dead_key_next = false;
		diacr = value;
		return;
	}
	if (kbd->kbdmode == VC_UNICODE)
		to_utf8(vc, value);
	else {
		int c = conv_uni_to_8bit(value);
		if (c != -1)
			put_queue(vc, c);
	}
}

/*
 * Handle dead key. Note that we now may have several
 * dead keys modifying the same character. Very useful
 * for Vietnamese.
 */
static void k_deadunicode(struct vc_data *vc, unsigned int value, char up_flag)
{
	if (up_flag)
		return;

	diacr = (diacr ? handle_diacr(vc, value) : value);
}

static void k_self(struct vc_data *vc, unsigned char value, char up_flag)
{
	k_unicode(vc, conv_8bit_to_uni(value), up_flag);
}

static void k_dead2(struct vc_data *vc, unsigned char value, char up_flag)
{
	k_deadunicode(vc, value, up_flag);
}

/*
 * Obsolete - for backwards compatibility only
 */
static void k_dead(struct vc_data *vc, unsigned char value, char up_flag)
{
	static const unsigned char ret_diacr[NR_DEAD] = {'`', '\'', '^', '~', '"', ',' };

	k_deadunicode(vc, ret_diacr[value], up_flag);
}

static void k_cons(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	set_console(value);
}

static void k_fn(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	if ((unsigned)value < ARRAY_SIZE(func_table)) {
		if (func_table[value])
			puts_queue(vc, func_table[value]);
	} else
		pr_err("k_fn called with value=%d\n", value);
}

static void k_cur(struct vc_data *vc, unsigned char value, char up_flag)
{
	static const char cur_chars[] = "BDCA";

	if (up_flag)
		return;

	applkey(vc, cur_chars[value], vc_kbd_mode(kbd, VC_CKMODE));
}

static void k_pad(struct vc_data *vc, unsigned char value, char up_flag)
{
	static const char pad_chars[] = "0123456789+-*/\015,.?()#";
	static const char app_map[] = "pqrstuvwxylSRQMnnmPQS";

	if (up_flag)
		return;		/* no action, if this is a key release */

	/* kludge... shift forces cursor/number keys */
	if (vc_kbd_mode(kbd, VC_APPLIC) && !shift_down[KG_SHIFT]) {
		applkey(vc, app_map[value], 1);
		return;
	}

	if (!vc_kbd_led(kbd, VC_NUMLOCK)) {

		switch (value) {
		case KVAL(K_PCOMMA):
		case KVAL(K_PDOT):
			k_fn(vc, KVAL(K_REMOVE), 0);
			return;
		case KVAL(K_P0):
			k_fn(vc, KVAL(K_INSERT), 0);
			return;
		case KVAL(K_P1):
			k_fn(vc, KVAL(K_SELECT), 0);
			return;
		case KVAL(K_P2):
			k_cur(vc, KVAL(K_DOWN), 0);
			return;
		case KVAL(K_P3):
			k_fn(vc, KVAL(K_PGDN), 0);
			return;
		case KVAL(K_P4):
			k_cur(vc, KVAL(K_LEFT), 0);
			return;
		case KVAL(K_P6):
			k_cur(vc, KVAL(K_RIGHT), 0);
			return;
		case KVAL(K_P7):
			k_fn(vc, KVAL(K_FIND), 0);
			return;
		case KVAL(K_P8):
			k_cur(vc, KVAL(K_UP), 0);
			return;
		case KVAL(K_P9):
			k_fn(vc, KVAL(K_PGUP), 0);
			return;
		case KVAL(K_P5):
			applkey(vc, 'G', vc_kbd_mode(kbd, VC_APPLIC));
			return;
		}
	}

	put_queue(vc, pad_chars[value]);
	if (value == KVAL(K_PENTER) && vc_kbd_mode(kbd, VC_CRLF))
		put_queue(vc, 10);
}

static void k_shift(struct vc_data *vc, unsigned char value, char up_flag)
{
	int old_state = shift_state;

	if (rep)
		return;
	/*
	 * Mimic typewriter:
	 * a CapsShift key acts like Shift but undoes CapsLock
	 */
	if (value == KVAL(K_CAPSSHIFT)) {
		value = KVAL(K_SHIFT);
		if (!up_flag)
			clr_vc_kbd_led(kbd, VC_CAPSLOCK);
	}

	if (up_flag) {
		/*
		 * handle the case that two shift or control
		 * keys are depressed simultaneously
		 */
		if (shift_down[value])
			shift_down[value]--;
	} else
		shift_down[value]++;

	if (shift_down[value])
		shift_state |= (1 << value);
	else
		shift_state &= ~(1 << value);

	/* kludge */
	if (up_flag && shift_state != old_state && npadch != -1) {
		if (kbd->kbdmode == VC_UNICODE)
			to_utf8(vc, npadch);
		else
			put_queue(vc, npadch & 0xff);
		npadch = -1;
	}
}

static void k_meta(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	if (vc_kbd_mode(kbd, VC_META)) {
		put_queue(vc, '\033');
		put_queue(vc, value);
	} else
		put_queue(vc, value | 0x80);
}

static void k_ascii(struct vc_data *vc, unsigned char value, char up_flag)
{
	int base;

	if (up_flag)
		return;

	if (value < 10) {
		/* decimal input of code, while Alt depressed */
		base = 10;
	} else {
		/* hexadecimal input of code, while AltGr depressed */
		value -= 10;
		base = 16;
	}

	if (npadch == -1)
		npadch = value;
	else
		npadch = npadch * base + value;
}

static void k_lock(struct vc_data *vc, unsigned char value, char up_flag)
{
	if (up_flag || rep)
		return;

	chg_vc_kbd_lock(kbd, value);
}

static void k_slock(struct vc_data *vc, unsigned char value, char up_flag)
{
	k_shift(vc, value, up_flag);
	if (up_flag || rep)
		return;

	chg_vc_kbd_slock(kbd, value);
	/* try to make Alt, oops, AltGr and such work */
	if (!key_maps[kbd->lockstate ^ kbd->slockstate]) {
		kbd->slockstate = 0;
		chg_vc_kbd_slock(kbd, value);
	}
}

/* by default, 300ms interval for combination release */
static unsigned brl_timeout = 300;
MODULE_PARM_DESC(brl_timeout, "Braille keys release delay in ms (0 for commit on first key release)");
module_param(brl_timeout, uint, 0644);

static unsigned brl_nbchords = 1;
MODULE_PARM_DESC(brl_nbchords, "Number of chords that produce a braille pattern (0 for dead chords)");
module_param(brl_nbchords, uint, 0644);

static void k_brlcommit(struct vc_data *vc, unsigned int pattern, char up_flag)
{
	static unsigned long chords;
	static unsigned committed;

	if (!brl_nbchords)
		k_deadunicode(vc, BRL_UC_ROW | pattern, up_flag);
	else {
		committed |= pattern;
		chords++;
		if (chords == brl_nbchords) {
			k_unicode(vc, BRL_UC_ROW | committed, up_flag);
			chords = 0;
			committed = 0;
		}
	}
}

static void k_brl(struct vc_data *vc, unsigned char value, char up_flag)
{
	static unsigned pressed, committing;
	static unsigned long releasestart;

	if (kbd->kbdmode != VC_UNICODE) {
		if (!up_flag)
			pr_warn("keyboard mode must be unicode for braille patterns\n");
		return;
	}

	if (!value) {
		k_unicode(vc, BRL_UC_ROW, up_flag);
		return;
	}

	if (value > 8)
		return;

	if (!up_flag) {
		pressed |= 1 << (value - 1);
		if (!brl_timeout)
			committing = pressed;
	} else if (brl_timeout) {
		if (!committing ||
		    time_after(jiffies,
			       releasestart + msecs_to_jiffies(brl_timeout))) {
			committing = pressed;
			releasestart = jiffies;
		}
		pressed &= ~(1 << (value - 1));
		if (!pressed && committing) {
			k_brlcommit(vc, committing, 0);
			committing = 0;
		}
	} else {
		if (committing) {
			k_brlcommit(vc, committing, 0);
			committing = 0;
		}
		pressed &= ~(1 << (value - 1));
	}
}

#if IS_ENABLED(CONFIG_INPUT_LEDS) && IS_ENABLED(CONFIG_LEDS_TRIGGERS)

struct kbd_led_trigger {
	struct led_trigger trigger;
	unsigned int mask;
};

static void kbd_led_trigger_activate(struct led_classdev *cdev)
{
	struct kbd_led_trigger *trigger =
		container_of(cdev->trigger, struct kbd_led_trigger, trigger);

	tasklet_disable(&keyboard_tasklet);
	if (ledstate != -1U)
		led_trigger_event(&trigger->trigger,
				  ledstate & trigger->mask ?
					LED_FULL : LED_OFF);
	tasklet_enable(&keyboard_tasklet);
}

#define KBD_LED_TRIGGER(_led_bit, _name) {			\
		.trigger = {					\
			.name = _name,				\
			.activate = kbd_led_trigger_activate,	\
		},						\
		.mask	= BIT(_led_bit),			\
	}

#define KBD_LOCKSTATE_TRIGGER(_led_bit, _name)		\
	KBD_LED_TRIGGER((_led_bit) + 8, _name)

static struct kbd_led_trigger kbd_led_triggers[] = {
	KBD_LED_TRIGGER(VC_SCROLLOCK, "kbd-scrolllock"),
	KBD_LED_TRIGGER(VC_NUMLOCK,   "kbd-numlock"),
	KBD_LED_TRIGGER(VC_CAPSLOCK,  "kbd-capslock"),
	KBD_LED_TRIGGER(VC_KANALOCK,  "kbd-kanalock"),

	KBD_LOCKSTATE_TRIGGER(VC_SHIFTLOCK,  "kbd-shiftlock"),
	KBD_LOCKSTATE_TRIGGER(VC_ALTGRLOCK,  "kbd-altgrlock"),
	KBD_LOCKSTATE_TRIGGER(VC_CTRLLOCK,   "kbd-ctrllock"),
	KBD_LOCKSTATE_TRIGGER(VC_ALTLOCK,    "kbd-altlock"),
	KBD_LOCKSTATE_TRIGGER(VC_SHIFTLLOCK, "kbd-shiftllock"),
	KBD_LOCKSTATE_TRIGGER(VC_SHIFTRLOCK, "kbd-shiftrlock"),
	KBD_LOCKSTATE_TRIGGER(VC_CTRLLLOCK,  "kbd-ctrlllock"),
	KBD_LOCKSTATE_TRIGGER(VC_CTRLRLOCK,  "kbd-ctrlrlock"),
};

static void kbd_propagate_led_state(unsigned int old_state,
				    unsigned int new_state)
{
	struct kbd_led_trigger *trigger;
	unsigned int changed = old_state ^ new_state;
	int i;

	for (i = 0; i < ARRAY_SIZE(kbd_led_triggers); i++) {
		trigger = &kbd_led_triggers[i];

		if (changed & trigger->mask)
			led_trigger_event(&trigger->trigger,
					  new_state & trigger->mask ?
						LED_FULL : LED_OFF);
	}
}

static int kbd_update_leds_helper(struct input_handle *handle, void *data)
{
	unsigned int led_state = *(unsigned int *)data;

	if (test_bit(EV_LED, handle->dev->evbit))
		kbd_propagate_led_state(~led_state, led_state);

	return 0;
}

static void kbd_init_leds(void)
{
	int error;
	int i;

	for (i = 0; i < ARRAY_SIZE(kbd_led_triggers); i++) {
		error = led_trigger_register(&kbd_led_triggers[i].trigger);
		if (error)
			pr_err("error %d while registering trigger %s\n",
			       error, kbd_led_triggers[i].trigger.name);
	}
}

#else

static int kbd_update_leds_helper(struct input_handle *handle, void *data)
{
	unsigned int leds = *(unsigned int *)data;

	if (test_bit(EV_LED, handle->dev->evbit)) {
		input_inject_event(handle, EV_LED, LED_SCROLLL, !!(leds & 0x01));
		input_inject_event(handle, EV_LED, LED_NUML,    !!(leds & 0x02));
		input_inject_event(handle, EV_LED, LED_CAPSL,   !!(leds & 0x04));
		input_inject_event(handle, EV_SYN, SYN_REPORT, 0);
	}

	return 0;
}

static void kbd_propagate_led_state(unsigned int old_state,
				    unsigned int new_state)
{
	input_handler_for_each_handle(&kbd_handler, &new_state,
				      kbd_update_leds_helper);
}

static void kbd_init_leds(void)
{
}

#endif

/*
 * The leds display either (i) the status of NumLock, CapsLock, ScrollLock,
 * or (ii) whatever pattern of lights people want to show using KDSETLED,
 * or (iii) specified bits of specified words in kernel memory.
 */
static unsigned char getledstate(void)
{
	return ledstate & 0xff;
}

void setledstate(struct kbd_struct *kb, unsigned int led)
{
        unsigned long flags;
        spin_lock_irqsave(&led_lock, flags);
	if (!(led & ~7)) {
		ledioctl = led;
		kb->ledmode = LED_SHOW_IOCTL;
	} else
		kb->ledmode = LED_SHOW_FLAGS;

	set_leds();
	spin_unlock_irqrestore(&led_lock, flags);
}

static inline unsigned char getleds(void)
{
	struct kbd_struct *kb = kbd_table + fg_console;

	if (kb->ledmode == LED_SHOW_IOCTL)
		return ledioctl;

	return kb->ledflagstate;
}

/**
 *	vt_get_leds	-	helper for braille console
 *	@console: console to read
 *	@flag: flag we want to check
 *
 *	Check the status of a keyboard led flag and report it back
 */
int vt_get_leds(int console, int flag)
{
	struct kbd_struct *kb = kbd_table + console;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&led_lock, flags);
	ret = vc_kbd_led(kb, flag);
	spin_unlock_irqrestore(&led_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(vt_get_leds);

/**
 *	vt_set_led_state	-	set LED state of a console
 *	@console: console to set
 *	@leds: LED bits
 *
 *	Set the LEDs on a console. This is a wrapper for the VT layer
 *	so that we can keep kbd knowledge internal
 */
void vt_set_led_state(int console, int leds)
{
	struct kbd_struct *kb = kbd_table + console;
	setledstate(kb, leds);
}

/**
 *	vt_kbd_con_start	-	Keyboard side of console start
 *	@console: console
 *
 *	Handle console start. This is a wrapper for the VT layer
 *	so that we can keep kbd knowledge internal
 *
 *	FIXME: We eventually need to hold the kbd lock here to protect
 *	the LED updating. We can't do it yet because fn_hold calls stop_tty
 *	and start_tty under the kbd_event_lock, while normal tty paths
 *	don't hold the lock. We probably need to split out an LED lock
 *	but not during an -rc release!
 */
void vt_kbd_con_start(int console)
{
	struct kbd_struct *kb = kbd_table + console;
	unsigned long flags;
	spin_lock_irqsave(&led_lock, flags);
	clr_vc_kbd_led(kb, VC_SCROLLOCK);
	set_leds();
	spin_unlock_irqrestore(&led_lock, flags);
}

/**
 *	vt_kbd_con_stop		-	Keyboard side of console stop
 *	@console: console
 *
 *	Handle console stop. This is a wrapper for the VT layer
 *	so that we can keep kbd knowledge internal
 */
void vt_kbd_con_stop(int console)
{
	struct kbd_struct *kb = kbd_table + console;
	unsigned long flags;
	spin_lock_irqsave(&led_lock, flags);
	set_vc_kbd_led(kb, VC_SCROLLOCK);
	set_leds();
	spin_unlock_irqrestore(&led_lock, flags);
}

/*
 * This is the tasklet that updates LED state of LEDs using standard
 * keyboard triggers. The reason we use tasklet is that we need to
 * handle the scenario when keyboard handler is not registered yet
 * but we already getting updates from the VT to update led state.
 */
static void kbd_bh(unsigned long dummy)
{
	unsigned int leds;
	unsigned long flags;

	spin_lock_irqsave(&led_lock, flags);
	leds = getleds();
	leds |= (unsigned int)kbd->lockstate << 8;
	spin_unlock_irqrestore(&led_lock, flags);

	if (leds != ledstate) {
		kbd_propagate_led_state(ledstate, leds);
		ledstate = leds;
	}
}

DECLARE_TASKLET_DISABLED(keyboard_tasklet, kbd_bh, 0);

#if defined(CONFIG_X86) || defined(CONFIG_IA64) || defined(CONFIG_ALPHA) ||\
    defined(CONFIG_MIPS) || defined(CONFIG_PPC) || defined(CONFIG_SPARC) ||\
    defined(CONFIG_PARISC) || defined(CONFIG_SUPERH) ||\
    (defined(CONFIG_ARM) && defined(CONFIG_KEYBOARD_ATKBD) && !defined(CONFIG_ARCH_RPC)) ||\
    defined(CONFIG_AVR32)

#define HW_RAW(dev) (test_bit(EV_MSC, dev->evbit) && test_bit(MSC_RAW, dev->mscbit) &&\
			((dev)->id.bustype == BUS_I8042) && ((dev)->id.vendor == 0x0001) && ((dev)->id.product == 0x0001))

static const unsigned short x86_keycodes[256] =
	{ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
	 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
	 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
	 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
	 80, 81, 82, 83, 84,118, 86, 87, 88,115,120,119,121,112,123, 92,
	284,285,309,  0,312, 91,327,328,329,331,333,335,336,337,338,339,
	367,288,302,304,350, 89,334,326,267,126,268,269,125,347,348,349,
	360,261,262,263,268,376,100,101,321,316,373,286,289,102,351,355,
	103,104,105,275,287,279,258,106,274,107,294,364,358,363,362,361,
	291,108,381,281,290,272,292,305,280, 99,112,257,306,359,113,114,
	264,117,271,374,379,265,266, 93, 94, 95, 85,259,375,260, 90,116,
	377,109,111,277,278,282,283,295,296,297,299,300,301,293,303,307,
	308,310,313,314,315,317,318,319,320,357,322,323,324,325,276,330,
	332,340,365,342,343,344,345,346,356,270,341,368,369,370,371,372 };

#ifdef CONFIG_SPARC
static int sparc_l1_a_state;
extern void sun_do_break(void);
#endif

static int emulate_raw(struct vc_data *vc, unsigned int keycode,
		       unsigned char up_flag)
{
	int code;

	switch (keycode) {

	case KEY_PAUSE:
		put_queue(vc, 0xe1);
		put_queue(vc, 0x1d | up_flag);
		put_queue(vc, 0x45 | up_flag);
		break;

	case KEY_HANGEUL:
		if (!up_flag)
			put_queue(vc, 0xf2);
		break;

	case KEY_HANJA:
		if (!up_flag)
			put_queue(vc, 0xf1);
		break;

	case KEY_SYSRQ:
		/*
		 * Real AT keyboards (that's what we're trying
		 * to emulate here emit 0xe0 0x2a 0xe0 0x37 when
		 * pressing PrtSc/SysRq alone, but simply 0x54
		 * when pressing Alt+PrtSc/SysRq.
		 */
		if (test_bit(KEY_LEFTALT, key_down) ||
		    test_bit(KEY_RIGHTALT, key_down)) {
			put_queue(vc, 0x54 | up_flag);
		} else {
			put_queue(vc, 0xe0);
			put_queue(vc, 0x2a | up_flag);
			put_queue(vc, 0xe0);
			put_queue(vc, 0x37 | up_flag);
		}
		break;

	default:
		if (keycode > 255)
			return -1;

		code = x86_keycodes[keycode];
		if (!code)
			return -1;

		if (code & 0x100)
			put_queue(vc, 0xe0);
		put_queue(vc, (code & 0x7f) | up_flag);

		break;
	}

	return 0;
}

#else

#define HW_RAW(dev)	0

static int emulate_raw(struct vc_data *vc, unsigned int keycode, unsigned char up_flag)
{
	if (keycode > 127)
		return -1;

	put_queue(vc, keycode | up_flag);
	return 0;
}
#endif

static void kbd_rawcode(unsigned char data)
{
	struct vc_data *vc = vc_cons[fg_console].d;

	kbd = kbd_table + vc->vc_num;
	if (kbd->kbdmode == VC_RAW)
		put_queue(vc, data);
}

static void kbd_keycode(unsigned int keycode, int down, int hw_raw)
{
	struct vc_data *vc = vc_cons[fg_console].d;
	unsigned short keysym, *key_map;
	unsigned char type;
	bool raw_mode;
	struct tty_struct *tty;
	int shift_final;
	struct keyboard_notifier_param param = { .vc = vc, .value = keycode, .down = down };
	int rc;

	tty = vc->port.tty;

	if (tty && (!tty->driver_data)) {
		/* No driver data? Strange. Okay we fix it then. */
		tty->driver_data = vc;
	}

	kbd = kbd_table + vc->vc_num;

#ifdef CONFIG_SPARC
	if (keycode == KEY_STOP)
		sparc_l1_a_state = down;
#endif

	rep = (down == 2);

	raw_mode = (kbd->kbdmode == VC_RAW);
	if (raw_mode && !hw_raw)
		if (emulate_raw(vc, keycode, !down << 7))
			if (keycode < BTN_MISC && printk_ratelimit())
				pr_warn("can't emulate rawmode for keycode %d\n",
					keycode);

#ifdef CONFIG_SPARC
	if (keycode == KEY_A && sparc_l1_a_state) {
		sparc_l1_a_state = false;
		sun_do_break();
	}
#endif

	if (kbd->kbdmode == VC_MEDIUMRAW) {
		/*
		 * This is extended medium raw mode, with keys above 127
		 * encoded as 0, high 7 bits, low 7 bits, with the 0 bearing
		 * the 'up' flag if needed. 0 is reserved, so this shouldn't
		 * interfere with anything else. The two bytes after 0 will
		 * always have the up flag set not to interfere with older
		 * applications. This allows for 16384 different keycodes,
		 * which should be enough.
		 */
		if (keycode < 128) {
			put_queue(vc, keycode | (!down << 7));
		} else {
			put_queue(vc, !down << 7);
			put_queue(vc, (keycode >> 7) | 0x80);
			put_queue(vc, keycode | 0x80);
		}
		raw_mode = true;
	}

	if (down)
		set_bit(keycode, key_down);
	else
		clear_bit(keycode, key_down);

	if (rep &&
	    (!vc_kbd_mode(kbd, VC_REPEAT) ||
	     (tty && !L_ECHO(tty) && tty_chars_in_buffer(tty)))) {
		/*
		 * Don't repeat a key if the input buffers are not empty and the
		 * characters get aren't echoed locally. This makes key repeat
		 * usable with slow applications and under heavy loads.
		 */
		return;
	}

	param.shift = shift_final = (shift_state | kbd->slockstate) ^ kbd->lockstate;
	param.ledstate = kbd->ledflagstate;
	key_map = key_maps[shift_final];

	rc = atomic_notifier_call_chain(&keyboard_notifier_list,
					KBD_KEYCODE, &param);
	if (rc == NOTIFY_STOP || !key_map) {
		atomic_notifier_call_chain(&keyboard_notifier_list,
					   KBD_UNBOUND_KEYCODE, &param);
		do_compute_shiftstate();
		kbd->slockstate = 0;
		return;
	}

	if (keycode < NR_KEYS)
		keysym = key_map[keycode];
	else if (keycode >= KEY_BRL_DOT1 && keycode <= KEY_BRL_DOT8)
		keysym = U(K(KT_BRL, keycode - KEY_BRL_DOT1 + 1));
	else
		return;

	type = KTYP(keysym);

	if (type < 0xf0) {
		param.value = keysym;
		rc = atomic_notifier_call_chain(&keyboard_notifier_list,
						KBD_UNICODE, &param);
		if (rc != NOTIFY_STOP)
			if (down && !raw_mode)
				to_utf8(vc, keysym);
		return;
	}

	type -= 0xf0;

	if (type == KT_LETTER) {
		type = KT_LATIN;
		if (vc_kbd_led(kbd, VC_CAPSLOCK)) {
			key_map = key_maps[shift_final ^ (1 << KG_SHIFT)];
			if (key_map)
				keysym = key_map[keycode];
		}
	}

	param.value = keysym;
	rc = atomic_notifier_call_chain(&keyboard_notifier_list,
					KBD_KEYSYM, &param);
	if (rc == NOTIFY_STOP)
		return;

	if ((raw_mode || kbd->kbdmode == VC_OFF) && type != KT_SPEC && type != KT_SHIFT)
		return;

	(*k_handler[type])(vc, keysym & 0xff, !down);

	param.ledstate = kbd->ledflagstate;
	atomic_notifier_call_chain(&keyboard_notifier_list, KBD_POST_KEYSYM, &param);

	if (type != KT_SLOCK)
		kbd->slockstate = 0;
}

static void kbd_event(struct input_handle *handle, unsigned int event_type,
		      unsigned int event_code, int value)
{
	/* We are called with interrupts disabled, just take the lock */
	spin_lock(&kbd_event_lock);

	if (event_type == EV_MSC && event_code == MSC_RAW && HW_RAW(handle->dev))
		kbd_rawcode(value);
	if (event_type == EV_KEY)
		kbd_keycode(event_code, value, HW_RAW(handle->dev));

	spin_unlock(&kbd_event_lock);

	tasklet_schedule(&keyboard_tasklet);
	do_poke_blanked_console = 1;
	schedule_console_callback();
}

static bool kbd_match(struct input_handler *handler, struct input_dev *dev)
{
	int i;

	if (test_bit(EV_SND, dev->evbit))
		return true;

	if (test_bit(EV_KEY, dev->evbit)) {
		for (i = KEY_RESERVED; i < BTN_MISC; i++)
			if (test_bit(i, dev->keybit))
				return true;
		for (i = KEY_BRL_DOT1; i <= KEY_BRL_DOT10; i++)
			if (test_bit(i, dev->keybit))
				return true;
	}

	return false;
}

/*
 * When a keyboard (or other input device) is found, the kbd_connect
 * function is called. The function then looks at the device, and if it
 * likes it, it can open it and get events from it. In this (kbd_connect)
 * function, we should decide which VT to bind that keyboard to initially.
 */
static int kbd_connect(struct input_handler *handler, struct input_dev *dev,
			const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "kbd";

	error = input_register_handle(handle);
	if (error)
		goto err_free_handle;

	error = input_open_device(handle);
	if (error)
		goto err_unregister_handle;

	return 0;

 err_unregister_handle:
	input_unregister_handle(handle);
 err_free_handle:
	kfree(handle);
	return error;
}

static void kbd_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

/*
 * Start keyboard handler on the new keyboard by refreshing LED state to
 * match the rest of the system.
 */
static void kbd_start(struct input_handle *handle)
{
	tasklet_disable(&keyboard_tasklet);

	if (ledstate != -1U)
		kbd_update_leds_helper(handle, &ledstate);

	tasklet_enable(&keyboard_tasklet);
}

static const struct input_device_id kbd_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},

	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_SND) },
	},

	{ },    /* Terminating entry */
};

MODULE_DEVICE_TABLE(input, kbd_ids);

static struct input_handler kbd_handler = {
	.event		= kbd_event,
	.match		= kbd_match,
	.connect	= kbd_connect,
	.disconnect	= kbd_disconnect,
	.start		= kbd_start,
	.name		= "kbd",
	.id_table	= kbd_ids,
};

int __init kbd_init(void)
{
	int i;
	int error;

	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		kbd_table[i].ledflagstate = kbd_defleds();
		kbd_table[i].default_ledflagstate = kbd_defleds();
		kbd_table[i].ledmode = LED_SHOW_FLAGS;
		kbd_table[i].lockstate = KBD_DEFLOCK;
		kbd_table[i].slockstate = 0;
		kbd_table[i].modeflags = KBD_DEFMODE;
		kbd_table[i].kbdmode = default_utf8 ? VC_UNICODE : VC_XLATE;
	}

	kbd_init_leds();

	error = input_register_handler(&kbd_handler);
	if (error)
		return error;

	tasklet_enable(&keyboard_tasklet);
	tasklet_schedule(&keyboard_tasklet);

	return 0;
}

/* Ioctl support code */

/**
 *	vt_do_diacrit		-	diacritical table updates
 *	@cmd: ioctl request
 *	@udp: pointer to user data for ioctl
 *	@perm: permissions check computed by caller
 *
 *	Update the diacritical tables atomically and safely. Lock them
 *	against simultaneous keypresses
 */
int vt_do_diacrit(unsigned int cmd, void __user *udp, int perm)
{
	unsigned long flags;
	int asize;
	int ret = 0;

	switch (cmd) {
	case KDGKBDIACR:
	{
		struct kbdiacrs __user *a = udp;
		struct kbdiacr *dia;
		int i;

		dia = kmalloc(MAX_DIACR * sizeof(struct kbdiacr),
								GFP_KERNEL);
		if (!dia)
			return -ENOMEM;

		/* Lock the diacriticals table, make a copy and then
		   copy it after we unlock */
		spin_lock_irqsave(&kbd_event_lock, flags);

		asize = accent_table_size;
		for (i = 0; i < asize; i++) {
			dia[i].diacr = conv_uni_to_8bit(
						accent_table[i].diacr);
			dia[i].base = conv_uni_to_8bit(
						accent_table[i].base);
			dia[i].result = conv_uni_to_8bit(
						accent_table[i].result);
		}
		spin_unlock_irqrestore(&kbd_event_lock, flags);

		if (put_user(asize, &a->kb_cnt))
			ret = -EFAULT;
		else  if (copy_to_user(a->kbdiacr, dia,
				asize * sizeof(struct kbdiacr)))
			ret = -EFAULT;
		kfree(dia);
		return ret;
	}
	case KDGKBDIACRUC:
	{
		struct kbdiacrsuc __user *a = udp;
		void *buf;

		buf = kmalloc(MAX_DIACR * sizeof(struct kbdiacruc),
								GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;

		/* Lock the diacriticals table, make a copy and then
		   copy it after we unlock */
		spin_lock_irqsave(&kbd_event_lock, flags);

		asize = accent_table_size;
		memcpy(buf, accent_table, asize * sizeof(struct kbdiacruc));

		spin_unlock_irqrestore(&kbd_event_lock, flags);

		if (put_user(asize, &a->kb_cnt))
			ret = -EFAULT;
		else if (copy_to_user(a->kbdiacruc, buf,
				asize*sizeof(struct kbdiacruc)))
			ret = -EFAULT;
		kfree(buf);
		return ret;
	}

	case KDSKBDIACR:
	{
		struct kbdiacrs __user *a = udp;
		struct kbdiacr *dia = NULL;
		unsigned int ct;
		int i;

		if (!perm)
			return -EPERM;
		if (get_user(ct, &a->kb_cnt))
			return -EFAULT;
		if (ct >= MAX_DIACR)
			return -EINVAL;

		if (ct) {

			dia = memdup_user(a->kbdiacr,
					sizeof(struct kbdiacr) * ct);
			if (IS_ERR(dia))
				return PTR_ERR(dia);

		}

		spin_lock_irqsave(&kbd_event_lock, flags);
		accent_table_size = ct;
		for (i = 0; i < ct; i++) {
			accent_table[i].diacr =
					conv_8bit_to_uni(dia[i].diacr);
			accent_table[i].base =
					conv_8bit_to_uni(dia[i].base);
			accent_table[i].result =
					conv_8bit_to_uni(dia[i].result);
		}
		spin_unlock_irqrestore(&kbd_event_lock, flags);
		kfree(dia);
		return 0;
	}

	case KDSKBDIACRUC:
	{
		struct kbdiacrsuc __user *a = udp;
		unsigned int ct;
		void *buf = NULL;

		if (!perm)
			return -EPERM;

		if (get_user(ct, &a->kb_cnt))
			return -EFAULT;

		if (ct >= MAX_DIACR)
			return -EINVAL;

		if (ct) {
			buf = memdup_user(a->kbdiacruc,
					  ct * sizeof(struct kbdiacruc));
			if (IS_ERR(buf))
				return PTR_ERR(buf);
		} 
		spin_lock_irqsave(&kbd_event_lock, flags);
		if (ct)
			memcpy(accent_table, buf,
					ct * sizeof(struct kbdiacruc));
		accent_table_size = ct;
		spin_unlock_irqrestore(&kbd_event_lock, flags);
		kfree(buf);
		return 0;
	}
	}
	return ret;
}

/**
 *	vt_do_kdskbmode		-	set keyboard mode ioctl
 *	@console: the console to use
 *	@arg: the requested mode
 *
 *	Update the keyboard mode bits while holding the correct locks.
 *	Return 0 for success or an error code.
 */
int vt_do_kdskbmode(int console, unsigned int arg)
{
	struct kbd_struct *kb = kbd_table + console;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&kbd_event_lock, flags);
	switch(arg) {
	case K_RAW:
		kb->kbdmode = VC_RAW;
		break;
	case K_MEDIUMRAW:
		kb->kbdmode = VC_MEDIUMRAW;
		break;
	case K_XLATE:
		kb->kbdmode = VC_XLATE;
		do_compute_shiftstate();
		break;
	case K_UNICODE:
		kb->kbdmode = VC_UNICODE;
		do_compute_shiftstate();
		break;
	case K_OFF:
		kb->kbdmode = VC_OFF;
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&kbd_event_lock, flags);
	return ret;
}

/**
 *	vt_do_kdskbmeta		-	set keyboard meta state
 *	@console: the console to use
 *	@arg: the requested meta state
 *
 *	Update the keyboard meta bits while holding the correct locks.
 *	Return 0 for success or an error code.
 */
int vt_do_kdskbmeta(int console, unsigned int arg)
{
	struct kbd_struct *kb = kbd_table + console;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&kbd_event_lock, flags);
	switch(arg) {
	case K_METABIT:
		clr_vc_kbd_mode(kb, VC_META);
		break;
	case K_ESCPREFIX:
		set_vc_kbd_mode(kb, VC_META);
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&kbd_event_lock, flags);
	return ret;
}

int vt_do_kbkeycode_ioctl(int cmd, struct kbkeycode __user *user_kbkc,
								int perm)
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

#define i (tmp.kb_index)
#define s (tmp.kb_table)
#define v (tmp.kb_value)

int vt_do_kdsk_ioctl(int cmd, struct kbentry __user *user_kbe, int perm,
						int console)
{
	struct kbd_struct *kb = kbd_table + console;
	struct kbentry tmp;
	ushort *key_map, *new_map, val, ov;
	unsigned long flags;

	if (copy_from_user(&tmp, user_kbe, sizeof(struct kbentry)))
		return -EFAULT;

	if (!capable(CAP_SYS_TTY_CONFIG))
		perm = 0;

	switch (cmd) {
	case KDGKBENT:
		/* Ensure another thread doesn't free it under us */
		spin_lock_irqsave(&kbd_event_lock, flags);
		key_map = key_maps[s];
		if (key_map) {
		    val = U(key_map[i]);
		    if (kb->kbdmode != VC_UNICODE && KTYP(val) >= NR_TYPES)
			val = K_HOLE;
		} else
		    val = (i ? K_HOLE : K_NOSUCHMAP);
		spin_unlock_irqrestore(&kbd_event_lock, flags);
		return put_user(val, &user_kbe->kb_value);
	case KDSKBENT:
		if (!perm)
			return -EPERM;
		if (!i && v == K_NOSUCHMAP) {
			spin_lock_irqsave(&kbd_event_lock, flags);
			/* deallocate map */
			key_map = key_maps[s];
			if (s && key_map) {
			    key_maps[s] = NULL;
			    if (key_map[0] == U(K_ALLOCATED)) {
					kfree(key_map);
					keymap_count--;
			    }
			}
			spin_unlock_irqrestore(&kbd_event_lock, flags);
			break;
		}

		if (KTYP(v) < NR_TYPES) {
		    if (KVAL(v) > max_vals[KTYP(v)])
				return -EINVAL;
		} else
		    if (kb->kbdmode != VC_UNICODE)
				return -EINVAL;

		/* ++Geert: non-PC keyboards may generate keycode zero */
#if !defined(__mc68000__) && !defined(__powerpc__)
		/* assignment to entry 0 only tests validity of args */
		if (!i)
			break;
#endif

		new_map = kmalloc(sizeof(plain_map), GFP_KERNEL);
		if (!new_map)
			return -ENOMEM;
		spin_lock_irqsave(&kbd_event_lock, flags);
		key_map = key_maps[s];
		if (key_map == NULL) {
			int j;

			if (keymap_count >= MAX_NR_OF_USER_KEYMAPS &&
			    !capable(CAP_SYS_RESOURCE)) {
				spin_unlock_irqrestore(&kbd_event_lock, flags);
				kfree(new_map);
				return -EPERM;
			}
			key_maps[s] = new_map;
			key_map = new_map;
			key_map[0] = U(K_ALLOCATED);
			for (j = 1; j < NR_KEYS; j++)
				key_map[j] = U(K_HOLE);
			keymap_count++;
		} else
			kfree(new_map);

		ov = U(key_map[i]);
		if (v == ov)
			goto out;
		/*
		 * Attention Key.
		 */
		if (((ov == K_SAK) || (v == K_SAK)) && !capable(CAP_SYS_ADMIN)) {
			spin_unlock_irqrestore(&kbd_event_lock, flags);
			return -EPERM;
		}
		key_map[i] = U(v);
		if (!s && (KTYP(ov) == KT_SHIFT || KTYP(v) == KT_SHIFT))
			do_compute_shiftstate();
out:
		spin_unlock_irqrestore(&kbd_event_lock, flags);
		break;
	}
	return 0;
}
#undef i
#undef s
#undef v

/* FIXME: This one needs untangling and locking */
int vt_do_kdgkb_ioctl(int cmd, struct kbsentry __user *user_kdgkb, int perm)
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

int vt_do_kdskled(int console, int cmd, unsigned long arg, int perm)
{
	struct kbd_struct *kb = kbd_table + console;
        unsigned long flags;
	unsigned char ucval;

        switch(cmd) {
	/* the ioctls below read/set the flags usually shown in the leds */
	/* don't use them - they will go away without warning */
	case KDGKBLED:
                spin_lock_irqsave(&kbd_event_lock, flags);
		ucval = kb->ledflagstate | (kb->default_ledflagstate << 4);
                spin_unlock_irqrestore(&kbd_event_lock, flags);
		return put_user(ucval, (char __user *)arg);

	case KDSKBLED:
		if (!perm)
			return -EPERM;
		if (arg & ~0x77)
			return -EINVAL;
                spin_lock_irqsave(&led_lock, flags);
		kb->ledflagstate = (arg & 7);
		kb->default_ledflagstate = ((arg >> 4) & 7);
		set_leds();
                spin_unlock_irqrestore(&led_lock, flags);
		return 0;

	/* the ioctls below only set the lights, not the functions */
	/* for those, see KDGKBLED and KDSKBLED above */
	case KDGETLED:
		ucval = getledstate();
		return put_user(ucval, (char __user *)arg);

	case KDSETLED:
		if (!perm)
			return -EPERM;
		setledstate(kb, arg);
		return 0;
        }
        return -ENOIOCTLCMD;
}

int vt_do_kdgkbmode(int console)
{
	struct kbd_struct *kb = kbd_table + console;
	/* This is a spot read so needs no locking */
	switch (kb->kbdmode) {
	case VC_RAW:
		return K_RAW;
	case VC_MEDIUMRAW:
		return K_MEDIUMRAW;
	case VC_UNICODE:
		return K_UNICODE;
	case VC_OFF:
		return K_OFF;
	default:
		return K_XLATE;
	}
}

/**
 *	vt_do_kdgkbmeta		-	report meta status
 *	@console: console to report
 *
 *	Report the meta flag status of this console
 */
int vt_do_kdgkbmeta(int console)
{
	struct kbd_struct *kb = kbd_table + console;
        /* Again a spot read so no locking */
	return vc_kbd_mode(kb, VC_META) ? K_ESCPREFIX : K_METABIT;
}

/**
 *	vt_reset_unicode	-	reset the unicode status
 *	@console: console being reset
 *
 *	Restore the unicode console state to its default
 */
void vt_reset_unicode(int console)
{
	unsigned long flags;

	spin_lock_irqsave(&kbd_event_lock, flags);
	kbd_table[console].kbdmode = default_utf8 ? VC_UNICODE : VC_XLATE;
	spin_unlock_irqrestore(&kbd_event_lock, flags);
}

/**
 *	vt_get_shiftstate	-	shift bit state
 *
 *	Report the shift bits from the keyboard state. We have to export
 *	this to support some oddities in the vt layer.
 */
int vt_get_shift_state(void)
{
        /* Don't lock as this is a transient report */
        return shift_state;
}

/**
 *	vt_reset_keyboard	-	reset keyboard state
 *	@console: console to reset
 *
 *	Reset the keyboard bits for a console as part of a general console
 *	reset event
 */
void vt_reset_keyboard(int console)
{
	struct kbd_struct *kb = kbd_table + console;
	unsigned long flags;

	spin_lock_irqsave(&kbd_event_lock, flags);
	set_vc_kbd_mode(kb, VC_REPEAT);
	clr_vc_kbd_mode(kb, VC_CKMODE);
	clr_vc_kbd_mode(kb, VC_APPLIC);
	clr_vc_kbd_mode(kb, VC_CRLF);
	kb->lockstate = 0;
	kb->slockstate = 0;
	spin_lock(&led_lock);
	kb->ledmode = LED_SHOW_FLAGS;
	kb->ledflagstate = kb->default_ledflagstate;
	spin_unlock(&led_lock);
	/* do not do set_leds here because this causes an endless tasklet loop
	   when the keyboard hasn't been initialized yet */
	spin_unlock_irqrestore(&kbd_event_lock, flags);
}

/**
 *	vt_get_kbd_mode_bit	-	read keyboard status bits
 *	@console: console to read from
 *	@bit: mode bit to read
 *
 *	Report back a vt mode bit. We do this without locking so the
 *	caller must be sure that there are no synchronization needs
 */

int vt_get_kbd_mode_bit(int console, int bit)
{
	struct kbd_struct *kb = kbd_table + console;
	return vc_kbd_mode(kb, bit);
}

/**
 *	vt_set_kbd_mode_bit	-	read keyboard status bits
 *	@console: console to read from
 *	@bit: mode bit to read
 *
 *	Set a vt mode bit. We do this without locking so the
 *	caller must be sure that there are no synchronization needs
 */

void vt_set_kbd_mode_bit(int console, int bit)
{
	struct kbd_struct *kb = kbd_table + console;
	unsigned long flags;

	spin_lock_irqsave(&kbd_event_lock, flags);
	set_vc_kbd_mode(kb, bit);
	spin_unlock_irqrestore(&kbd_event_lock, flags);
}

/**
 *	vt_clr_kbd_mode_bit	-	read keyboard status bits
 *	@console: console to read from
 *	@bit: mode bit to read
 *
 *	Report back a vt mode bit. We do this without locking so the
 *	caller must be sure that there are no synchronization needs
 */

void vt_clr_kbd_mode_bit(int console, int bit)
{
	struct kbd_struct *kb = kbd_table + console;
	unsigned long flags;

	spin_lock_irqsave(&kbd_event_lock, flags);
	clr_vc_kbd_mode(kb, bit);
	spin_unlock_irqrestore(&kbd_event_lock, flags);
}
