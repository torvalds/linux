#include <linux/config.h>
#include <linux/kd.h>
//#include <linux/kbd_ll.h>
#include <linux/kbd_kern.h>

/*
 * Translation of escaped scancodes to keycodes.
 * This is now user-settable.
 * The keycodes 1-88,96-111,119 are fairly standard, and
 * should probably not be changed - changing might confuse X.
 * X also interprets scancode 0x5d (KEY_Begin).
 *
 * For 1-88 keycode equals scancode.
 */

#define E0_KPENTER 96
#define E0_RCTRL   97
#define E0_KPSLASH 98
#define E0_PRSCR   99
#define E0_RALT    100
#define E0_BREAK   101		/* (control-pause) */
#define E0_HOME    102
#define E0_UP      103
#define E0_PGUP    104
#define E0_LEFT    105
#define E0_RIGHT   106
#define E0_END     107
#define E0_DOWN    108
#define E0_PGDN    109
#define E0_INS     110
#define E0_DEL     111

/* for USB 106 keyboard */
#define E0_YEN         124
#define E0_BACKSLASH   89


#define E1_PAUSE   119

/*
 * The keycodes below are randomly located in 89-95,112-118,120-127.
 * They could be thrown away (and all occurrences below replaced by 0),
 * but that would force many users to use the `setkeycodes' utility, where
 * they needed not before. It does not matter that there are duplicates, as
 * long as no duplication occurs for any single keyboard.
 */
#define SC_LIM 89

#define FOCUS_PF1 85		/* actual code! */
#define FOCUS_PF2 89
#define FOCUS_PF3 90
#define FOCUS_PF4 91
#define FOCUS_PF5 92
#define FOCUS_PF6 93
#define FOCUS_PF7 94
#define FOCUS_PF8 95
#define FOCUS_PF9 120
#define FOCUS_PF10 121
#define FOCUS_PF11 122
#define FOCUS_PF12 123

#define JAP_86     124
/* tfj@olivia.ping.dk:
 * The four keys are located over the numeric keypad, and are
 * labelled A1-A4. It's an rc930 keyboard, from
 * Regnecentralen/RC International, Now ICL.
 * Scancodes: 59, 5a, 5b, 5c.
 */
#define RGN1 124
#define RGN2 125
#define RGN3 126
#define RGN4 127

static unsigned char high_keys[128 - SC_LIM] = {
	RGN1, RGN2, RGN3, RGN4, 0, 0, 0,	/* 0x59-0x5f */
	0, 0, 0, 0, 0, 0, 0, 0,	/* 0x60-0x67 */
	0, 0, 0, 0, 0, FOCUS_PF11, 0, FOCUS_PF12,	/* 0x68-0x6f */
	0, 0, 0, FOCUS_PF2, FOCUS_PF9, 0, 0, FOCUS_PF3,	/* 0x70-0x77 */
	FOCUS_PF4, FOCUS_PF5, FOCUS_PF6, FOCUS_PF7,	/* 0x78-0x7b */
	FOCUS_PF8, JAP_86, FOCUS_PF10, 0	/* 0x7c-0x7f */
};

/* BTC */
#define E0_MACRO   112
/* LK450 */
#define E0_F13     113
#define E0_F14     114
#define E0_HELP    115
#define E0_DO      116
#define E0_F17     117
#define E0_KPMINPLUS 118
/*
 * My OmniKey generates e0 4c for  the "OMNI" key and the
 * right alt key does nada. [kkoller@nyx10.cs.du.edu]
 */
#define E0_OK  124
/*
 * New microsoft keyboard is rumoured to have
 * e0 5b (left window button), e0 5c (right window button),
 * e0 5d (menu button). [or: LBANNER, RBANNER, RMENU]
 * [or: Windows_L, Windows_R, TaskMan]
 */
#define E0_MSLW        125
#define E0_MSRW        126
#define E0_MSTM        127

static unsigned char e0_keys[128] = {
	0, 0, 0, 0, 0, 0, 0, 0,	/* 0x00-0x07 */
	0, 0, 0, 0, 0, 0, 0, 0,	/* 0x08-0x0f */
	0, 0, 0, 0, 0, 0, 0, 0,	/* 0x10-0x17 */
	0, 0, 0, 0, E0_KPENTER, E0_RCTRL, 0, 0,	/* 0x18-0x1f */
	0, 0, 0, 0, 0, 0, 0, 0,	/* 0x20-0x27 */
	0, 0, 0, 0, 0, 0, 0, 0,	/* 0x28-0x2f */
	0, 0, 0, 0, 0, E0_KPSLASH, 0, E0_PRSCR,	/* 0x30-0x37 */
	E0_RALT, 0, 0, 0, 0, E0_F13, E0_F14, E0_HELP,	/* 0x38-0x3f */
	E0_DO, E0_F17, 0, 0, 0, 0, E0_BREAK, E0_HOME,	/* 0x40-0x47 */
	E0_UP, E0_PGUP, 0, E0_LEFT, E0_OK, E0_RIGHT, E0_KPMINPLUS, E0_END,	/* 0x48-0x4f */
	E0_DOWN, E0_PGDN, E0_INS, E0_DEL, 0, 0, 0, 0,	/* 0x50-0x57 */
	0, 0, 0, E0_MSLW, E0_MSRW, E0_MSTM, 0, 0,	/* 0x58-0x5f */
	0, 0, 0, 0, 0, 0, 0, 0,	/* 0x60-0x67 */
	0, 0, 0, 0, 0, 0, 0, E0_MACRO,	/* 0x68-0x6f */
	//0, 0, 0, 0, 0, 0, 0, 0,                          /* 0x70-0x77 */
	0, 0, 0, 0, 0, E0_BACKSLASH, 0, 0,	/* 0x70-0x77 */
	0, 0, 0, E0_YEN, 0, 0, 0, 0	/* 0x78-0x7f */
};

static int gen_setkeycode(unsigned int scancode, unsigned int keycode)
{
	if (scancode < SC_LIM || scancode > 255 || keycode > 127)
		return -EINVAL;
	if (scancode < 128)
		high_keys[scancode - SC_LIM] = keycode;
	else
		e0_keys[scancode - 128] = keycode;
	return 0;
}

static int gen_getkeycode(unsigned int scancode)
{
	return
	    (scancode < SC_LIM || scancode > 255) ? -EINVAL :
	    (scancode <
	     128) ? high_keys[scancode - SC_LIM] : e0_keys[scancode - 128];
}

static int
gen_translate(unsigned char scancode, unsigned char *keycode, char raw_mode)
{
	static int prev_scancode;

	/* special prefix scancodes.. */
	if (scancode == 0xe0 || scancode == 0xe1) {
		prev_scancode = scancode;
		return 0;
	}

	/* 0xFF is sent by a few keyboards, ignore it. 0x00 is error */
	if (scancode == 0x00 || scancode == 0xff) {
		prev_scancode = 0;
		return 0;
	}

	scancode &= 0x7f;

	if (prev_scancode) {
		/*
		 * usually it will be 0xe0, but a Pause key generates
		 * e1 1d 45 e1 9d c5 when pressed, and nothing when released
		 */
		if (prev_scancode != 0xe0) {
			if (prev_scancode == 0xe1 && scancode == 0x1d) {
				prev_scancode = 0x100;
				return 0;
			}
				else if (prev_scancode == 0x100
					 && scancode == 0x45) {
				*keycode = E1_PAUSE;
				prev_scancode = 0;
			} else {
#ifdef KBD_REPORT_UNKN
				if (!raw_mode)
					printk(KERN_INFO
					       "keyboard: unknown e1 escape sequence\n");
#endif
				prev_scancode = 0;
				return 0;
			}
		} else {
			prev_scancode = 0;
			/*
			 *  The keyboard maintains its own internal caps lock and
			 *  num lock statuses. In caps lock mode E0 AA precedes make
			 *  code and E0 2A follows break code. In num lock mode,
			 *  E0 2A precedes make code and E0 AA follows break code.
			 *  We do our own book-keeping, so we will just ignore these.
			 */
			/*
			 *  For my keyboard there is no caps lock mode, but there are
			 *  both Shift-L and Shift-R modes. The former mode generates
			 *  E0 2A / E0 AA pairs, the latter E0 B6 / E0 36 pairs.
			 *  So, we should also ignore the latter. - aeb@cwi.nl
			 */
			if (scancode == 0x2a || scancode == 0x36)
				return 0;

			if (e0_keys[scancode])
				*keycode = e0_keys[scancode];
			else {
#ifdef KBD_REPORT_UNKN
				if (!raw_mode)
					printk(KERN_INFO
					       "keyboard: unknown scancode e0 %02x\n",
					       scancode);
#endif
				return 0;
			}
		}
	} else if (scancode >= SC_LIM) {
		/* This happens with the FOCUS 9000 keyboard
		   Its keys PF1..PF12 are reported to generate
		   55 73 77 78 79 7a 7b 7c 74 7e 6d 6f
		   Moreover, unless repeated, they do not generate
		   key-down events, so we have to zero up_flag below */
		/* Also, Japanese 86/106 keyboards are reported to
		   generate 0x73 and 0x7d for \ - and \ | respectively. */
		/* Also, some Brazilian keyboard is reported to produce
		   0x73 and 0x7e for \ ? and KP-dot, respectively. */

		*keycode = high_keys[scancode - SC_LIM];

		if (!*keycode) {
			if (!raw_mode) {
#ifdef KBD_REPORT_UNKN
				printk(KERN_INFO
				       "keyboard: unrecognized scancode (%02x)"
				       " - ignored\n", scancode);
#endif
			}
			return 0;
		}
	} else
		*keycode = scancode;
	return 1;
}

static char gen_unexpected_up(unsigned char keycode)
{
	/* unexpected, but this can happen: maybe this was a key release for a
	   FOCUS 9000 PF key; if we want to see it, we have to clear up_flag */
	if (keycode >= SC_LIM || keycode == 85)
		return 0;
	else
		return 0200;
}

/*
 * These are the default mappings
 */
int  (*k_setkeycode)(unsigned int, unsigned int) = gen_setkeycode;
int  (*k_getkeycode)(unsigned int) = gen_getkeycode;
int  (*k_translate)(unsigned char, unsigned char *, char) = gen_translate;
char (*k_unexpected_up)(unsigned char) = gen_unexpected_up;
void (*k_leds)(unsigned char);

/* Simple translation table for the SysRq keys */

#ifdef CONFIG_MAGIC_SYSRQ
static unsigned char gen_sysrq_xlate[128] =
	"\000\0331234567890-=\177\t"	/* 0x00 - 0x0f */
	"qwertyuiop[]\r\000as"	/* 0x10 - 0x1f */
	"dfghjkl;'`\000\\zxcv"	/* 0x20 - 0x2f */
	"bnm,./\000*\000 \000\201\202\203\204\205"	/* 0x30 - 0x3f */
	"\206\207\210\211\212\000\000789-456+1"	/* 0x40 - 0x4f */
	"230\177\000\000\213\214\000\000\000\000\000\000\000\000\000\000"	/* 0x50 - 0x5f */
	"\r\000/";			/* 0x60 - 0x6f */

unsigned char *k_sysrq_xlate = gen_sysrq_xlate;
int k_sysrq_key = 0x54;
#endif
