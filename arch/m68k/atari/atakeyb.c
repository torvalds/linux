/*
 * Atari Keyboard driver for 680x0 Linux
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Atari support by Robert de Vries
 * enhanced by Bjoern Brauel and Roman Hodek
 *
 * 2.6 and input cleanup (removed autorepeat stuff) for 2.6.21
 * 06/07 Michael Schmitz
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/keyboard.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/kd.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/kbd_kern.h>

#include <asm/atariints.h>
#include <asm/atarihw.h>
#include <asm/atarikb.h>
#include <asm/atari_joystick.h>
#include <asm/irq.h>


/* Hook for MIDI serial driver */
void (*atari_MIDI_interrupt_hook) (void);
/* Hook for keyboard inputdev  driver */
void (*atari_input_keyboard_interrupt_hook) (unsigned char, char);
/* Hook for mouse inputdev  driver */
void (*atari_input_mouse_interrupt_hook) (char *);
EXPORT_SYMBOL(atari_input_keyboard_interrupt_hook);
EXPORT_SYMBOL(atari_input_mouse_interrupt_hook);

/* variables for IKBD self test: */

/* state: 0: off; >0: in progress; >1: 0xf1 received */
static volatile int ikbd_self_test;
/* timestamp when last received a char */
static volatile unsigned long self_test_last_rcv;
/* bitmap of keys reported as broken */
static unsigned long broken_keys[128/(sizeof(unsigned long)*8)] = { 0, };

#define BREAK_MASK	(0x80)

/*
 * ++roman: The following changes were applied manually:
 *
 *  - The Alt (= Meta) key works in combination with Shift and
 *    Control, e.g. Alt+Shift+a sends Meta-A (0xc1), Alt+Control+A sends
 *    Meta-Ctrl-A (0x81) ...
 *
 *  - The parentheses on the keypad send '(' and ')' with all
 *    modifiers (as would do e.g. keypad '+'), but they cannot be used as
 *    application keys (i.e. sending Esc O c).
 *
 *  - HELP and UNDO are mapped to be F21 and F24, resp, that send the
 *    codes "\E[M" and "\E[P". (This is better than the old mapping to
 *    F11 and F12, because these codes are on Shift+F1/2 anyway.) This
 *    way, applications that allow their own keyboard mappings
 *    (e.g. tcsh, X Windows) can be configured to use them in the way
 *    the label suggests (providing help or undoing).
 *
 *  - Console switching is done with Alt+Fx (consoles 1..10) and
 *    Shift+Alt+Fx (consoles 11..20).
 *
 *  - The misc. special function implemented in the kernel are mapped
 *    to the following key combinations:
 *
 *      ClrHome          -> Home/Find
 *      Shift + ClrHome  -> End/Select
 *      Shift + Up       -> Page Up
 *      Shift + Down     -> Page Down
 *      Alt + Help       -> show system status
 *      Shift + Help     -> show memory info
 *      Ctrl + Help      -> show registers
 *      Ctrl + Alt + Del -> Reboot
 *      Alt + Undo       -> switch to last console
 *      Shift + Undo     -> send interrupt
 *      Alt + Insert     -> stop/start output (same as ^S/^Q)
 *      Alt + Up         -> Scroll back console (if implemented)
 *      Alt + Down       -> Scroll forward console (if implemented)
 *      Alt + CapsLock   -> NumLock
 *
 * ++Andreas:
 *
 *  - Help mapped to K_HELP
 *  - Undo mapped to K_UNDO (= K_F246)
 *  - Keypad Left/Right Parenthesis mapped to new K_PPAREN[LR]
 */

typedef enum kb_state_t {
	KEYBOARD, AMOUSE, RMOUSE, JOYSTICK, CLOCK, RESYNC
} KB_STATE_T;

#define	IS_SYNC_CODE(sc)	((sc) >= 0x04 && (sc) <= 0xfb)

typedef struct keyboard_state {
	unsigned char buf[6];
	int len;
	KB_STATE_T state;
} KEYBOARD_STATE;

KEYBOARD_STATE kb_state;

/* ++roman: If a keyboard overrun happened, we can't tell in general how much
 * bytes have been lost and in which state of the packet structure we are now.
 * This usually causes keyboards bytes to be interpreted as mouse movements
 * and vice versa, which is very annoying. It seems better to throw away some
 * bytes (that are usually mouse bytes) than to misinterpret them. Therefore I
 * introduced the RESYNC state for IKBD data. In this state, the bytes up to
 * one that really looks like a key event (0x04..0xf2) or the start of a mouse
 * packet (0xf8..0xfb) are thrown away, but at most 2 bytes. This at least
 * speeds up the resynchronization of the event structure, even if maybe a
 * mouse movement is lost. However, nothing is perfect. For bytes 0x01..0x03,
 * it's really hard to decide whether they're mouse or keyboard bytes. Since
 * overruns usually occur when moving the Atari mouse rapidly, they're seen as
 * mouse bytes here. If this is wrong, only a make code of the keyboard gets
 * lost, which isn't too bad. Losing a break code would be disastrous,
 * because then the keyboard repeat strikes...
 */

static irqreturn_t atari_keyboard_interrupt(int irq, void *dummy)
{
	u_char acia_stat;
	int scancode;
	int break_flag;

repeat:
	if (acia.mid_ctrl & ACIA_IRQ)
		if (atari_MIDI_interrupt_hook)
			atari_MIDI_interrupt_hook();
	acia_stat = acia.key_ctrl;
	/* check out if the interrupt came from this ACIA */
	if (!((acia_stat | acia.mid_ctrl) & ACIA_IRQ))
		return IRQ_HANDLED;

	if (acia_stat & ACIA_OVRN) {
		/* a very fast typist or a slow system, give a warning */
		/* ...happens often if interrupts were disabled for too long */
		pr_debug("Keyboard overrun\n");
		scancode = acia.key_data;
		if (ikbd_self_test)
			/* During self test, don't do resyncing, just process the code */
			goto interpret_scancode;
		else if (IS_SYNC_CODE(scancode)) {
			/* This code seem already to be the start of a new packet or a
			 * single scancode */
			kb_state.state = KEYBOARD;
			goto interpret_scancode;
		} else {
			/* Go to RESYNC state and skip this byte */
			kb_state.state = RESYNC;
			kb_state.len = 1;	/* skip max. 1 another byte */
			goto repeat;
		}
	}

	if (acia_stat & ACIA_RDRF) {
		/* received a character */
		scancode = acia.key_data;	/* get it or reset the ACIA, I'll get it! */
	interpret_scancode:
		switch (kb_state.state) {
		case KEYBOARD:
			switch (scancode) {
			case 0xF7:
				kb_state.state = AMOUSE;
				kb_state.len = 0;
				break;

			case 0xF8:
			case 0xF9:
			case 0xFA:
			case 0xFB:
				kb_state.state = RMOUSE;
				kb_state.len = 1;
				kb_state.buf[0] = scancode;
				break;

			case 0xFC:
				kb_state.state = CLOCK;
				kb_state.len = 0;
				break;

			case 0xFE:
			case 0xFF:
				kb_state.state = JOYSTICK;
				kb_state.len = 1;
				kb_state.buf[0] = scancode;
				break;

			case 0xF1:
				/* during self-test, note that 0xf1 received */
				if (ikbd_self_test) {
					++ikbd_self_test;
					self_test_last_rcv = jiffies;
					break;
				}
				fallthrough;

			default:
				break_flag = scancode & BREAK_MASK;
				scancode &= ~BREAK_MASK;
				if (ikbd_self_test) {
					/* Scancodes sent during the self-test stand for broken
					 * keys (keys being down). The code *should* be a break
					 * code, but nevertheless some AT keyboard interfaces send
					 * make codes instead. Therefore, simply ignore
					 * break_flag...
					 */
					int keyval, keytyp;

					set_bit(scancode, broken_keys);
					self_test_last_rcv = jiffies;
					/* new Linux scancodes; approx. */
					keyval = scancode;
					keytyp = KTYP(keyval) - 0xf0;
					keyval = KVAL(keyval);

					pr_warn("Key with scancode %d ", scancode);
					if (keytyp == KT_LATIN || keytyp == KT_LETTER) {
						if (keyval < ' ')
							pr_cont("('^%c') ", keyval + '@');
						else
							pr_cont("('%c') ", keyval);
					}
					pr_cont("is broken -- will be ignored.\n");
					break;
				} else if (test_bit(scancode, broken_keys))
					break;

				if (atari_input_keyboard_interrupt_hook)
					atari_input_keyboard_interrupt_hook((unsigned char)scancode, !break_flag);
				break;
			}
			break;

		case AMOUSE:
			kb_state.buf[kb_state.len++] = scancode;
			if (kb_state.len == 5) {
				kb_state.state = KEYBOARD;
				/* not yet used */
				/* wake up someone waiting for this */
			}
			break;

		case RMOUSE:
			kb_state.buf[kb_state.len++] = scancode;
			if (kb_state.len == 3) {
				kb_state.state = KEYBOARD;
				if (atari_input_mouse_interrupt_hook)
					atari_input_mouse_interrupt_hook(kb_state.buf);
			}
			break;

		case JOYSTICK:
			kb_state.buf[1] = scancode;
			kb_state.state = KEYBOARD;
#ifdef FIXED_ATARI_JOYSTICK
			atari_joystick_interrupt(kb_state.buf);
#endif
			break;

		case CLOCK:
			kb_state.buf[kb_state.len++] = scancode;
			if (kb_state.len == 6) {
				kb_state.state = KEYBOARD;
				/* wake up someone waiting for this.
				   But will this ever be used, as Linux keeps its own time.
				   Perhaps for synchronization purposes? */
				/* wake_up_interruptible(&clock_wait); */
			}
			break;

		case RESYNC:
			if (kb_state.len <= 0 || IS_SYNC_CODE(scancode)) {
				kb_state.state = KEYBOARD;
				goto interpret_scancode;
			}
			kb_state.len--;
			break;
		}
	}

#if 0
	if (acia_stat & ACIA_CTS)
		/* cannot happen */;
#endif

	if (acia_stat & (ACIA_FE | ACIA_PE)) {
		pr_err("Error in keyboard communication\n");
	}

	/* handle_scancode() can take a lot of time, so check again if
	 * some character arrived
	 */
	goto repeat;
}

/*
 * I write to the keyboard without using interrupts, I poll instead.
 * This takes for the maximum length string allowed (7) at 7812.5 baud
 * 8 data 1 start 1 stop bit: 9.0 ms
 * If this takes too long for normal operation, interrupt driven writing
 * is the solution. (I made a feeble attempt in that direction but I
 * kept it simple for now.)
 */
void ikbd_write(const char *str, int len)
{
	u_char acia_stat;

	if ((len < 1) || (len > 7))
		panic("ikbd: maximum string length exceeded");
	while (len) {
		acia_stat = acia.key_ctrl;
		if (acia_stat & ACIA_TDRE) {
			acia.key_data = *str++;
			len--;
		}
	}
}

/* Reset (without touching the clock) */
void ikbd_reset(void)
{
	static const char cmd[2] = { 0x80, 0x01 };

	ikbd_write(cmd, 2);

	/*
	 * if all's well code 0xF1 is returned, else the break codes of
	 * all keys making contact
	 */
}

/* Set mouse button action */
void ikbd_mouse_button_action(int mode)
{
	char cmd[2] = { 0x07, mode };

	ikbd_write(cmd, 2);
}

/* Set relative mouse position reporting */
void ikbd_mouse_rel_pos(void)
{
	static const char cmd[1] = { 0x08 };

	ikbd_write(cmd, 1);
}
EXPORT_SYMBOL(ikbd_mouse_rel_pos);

/* Set absolute mouse position reporting */
void ikbd_mouse_abs_pos(int xmax, int ymax)
{
	char cmd[5] = { 0x09, xmax>>8, xmax&0xFF, ymax>>8, ymax&0xFF };

	ikbd_write(cmd, 5);
}

/* Set mouse keycode mode */
void ikbd_mouse_kbd_mode(int dx, int dy)
{
	char cmd[3] = { 0x0A, dx, dy };

	ikbd_write(cmd, 3);
}

/* Set mouse threshold */
void ikbd_mouse_thresh(int x, int y)
{
	char cmd[3] = { 0x0B, x, y };

	ikbd_write(cmd, 3);
}
EXPORT_SYMBOL(ikbd_mouse_thresh);

/* Set mouse scale */
void ikbd_mouse_scale(int x, int y)
{
	char cmd[3] = { 0x0C, x, y };

	ikbd_write(cmd, 3);
}

/* Interrogate mouse position */
void ikbd_mouse_pos_get(int *x, int *y)
{
	static const char cmd[1] = { 0x0D };

	ikbd_write(cmd, 1);

	/* wait for returning bytes */
}

/* Load mouse position */
void ikbd_mouse_pos_set(int x, int y)
{
	char cmd[6] = { 0x0E, 0x00, x>>8, x&0xFF, y>>8, y&0xFF };

	ikbd_write(cmd, 6);
}

/* Set Y=0 at bottom */
void ikbd_mouse_y0_bot(void)
{
	static const char cmd[1] = { 0x0F };

	ikbd_write(cmd, 1);
}

/* Set Y=0 at top */
void ikbd_mouse_y0_top(void)
{
	static const char cmd[1] = { 0x10 };

	ikbd_write(cmd, 1);
}
EXPORT_SYMBOL(ikbd_mouse_y0_top);

/* Disable mouse */
void ikbd_mouse_disable(void)
{
	static const char cmd[1] = { 0x12 };

	ikbd_write(cmd, 1);
}
EXPORT_SYMBOL(ikbd_mouse_disable);

/* Set joystick event reporting */
void ikbd_joystick_event_on(void)
{
	static const char cmd[1] = { 0x14 };

	ikbd_write(cmd, 1);
}

/* Set joystick interrogation mode */
void ikbd_joystick_event_off(void)
{
	static const char cmd[1] = { 0x15 };

	ikbd_write(cmd, 1);
}

/* Joystick interrogation */
void ikbd_joystick_get_state(void)
{
	static const char cmd[1] = { 0x16 };

	ikbd_write(cmd, 1);
}

#if 0
/* This disables all other ikbd activities !!!! */
/* Set joystick monitoring */
void ikbd_joystick_monitor(int rate)
{
	static const char cmd[2] = { 0x17, rate };

	ikbd_write(cmd, 2);

	kb_state.state = JOYSTICK_MONITOR;
}
#endif

/* some joystick routines not in yet (0x18-0x19) */

/* Disable joysticks */
void ikbd_joystick_disable(void)
{
	static const char cmd[1] = { 0x1A };

	ikbd_write(cmd, 1);
}

/*
 * The original code sometimes left the interrupt line of
 * the ACIAs low forever. I hope, it is fixed now.
 *
 * Martin Rogge, 20 Aug 1995
 */

static int atari_keyb_done = 0;

int atari_keyb_init(void)
{
	int error;

	if (atari_keyb_done)
		return 0;

	kb_state.state = KEYBOARD;
	kb_state.len = 0;

	error = request_irq(IRQ_MFP_ACIA, atari_keyboard_interrupt, 0,
			    "keyboard,mouse,MIDI", atari_keyboard_interrupt);
	if (error)
		return error;

	atari_turnoff_irq(IRQ_MFP_ACIA);
	do {
		/* reset IKBD ACIA */
		acia.key_ctrl = ACIA_RESET |
				((atari_switches & ATARI_SWITCH_IKBD) ?
				 ACIA_RHTID : 0);
		(void)acia.key_ctrl;
		(void)acia.key_data;

		/* reset MIDI ACIA */
		acia.mid_ctrl = ACIA_RESET |
				((atari_switches & ATARI_SWITCH_MIDI) ?
				 ACIA_RHTID : 0);
		(void)acia.mid_ctrl;
		(void)acia.mid_data;

		/* divide 500kHz by 64 gives 7812.5 baud */
		/* 8 data no parity 1 start 1 stop bit */
		/* receive interrupt enabled */
		/* RTS low (except if switch selected), transmit interrupt disabled */
		acia.key_ctrl = (ACIA_DIV64|ACIA_D8N1S|ACIA_RIE) |
				((atari_switches & ATARI_SWITCH_IKBD) ?
				 ACIA_RHTID : ACIA_RLTID);

		acia.mid_ctrl = ACIA_DIV16 | ACIA_D8N1S |
				((atari_switches & ATARI_SWITCH_MIDI) ?
				 ACIA_RHTID : 0);

	/* make sure the interrupt line is up */
	} while ((st_mfp.par_dt_reg & 0x10) == 0);

	/* enable ACIA Interrupts */
	st_mfp.active_edge &= ~0x10;
	atari_turnon_irq(IRQ_MFP_ACIA);

	ikbd_self_test = 1;
	ikbd_reset();
	/* wait for a period of inactivity (here: 0.25s), then assume the IKBD's
	 * self-test is finished */
	self_test_last_rcv = jiffies;
	while (time_before(jiffies, self_test_last_rcv + HZ/4))
		barrier();
	/* if not incremented: no 0xf1 received */
	if (ikbd_self_test == 1)
		pr_err("Keyboard self test failed!\n");
	ikbd_self_test = 0;

	ikbd_mouse_disable();
	ikbd_joystick_disable();

#ifdef FIXED_ATARI_JOYSTICK
	atari_joystick_init();
#endif

	// flag init done
	atari_keyb_done = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(atari_keyb_init);
