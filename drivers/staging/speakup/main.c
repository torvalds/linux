/* speakup.c
 * review functions for the speakup screen review package.
 * originally written by: Kirk Reiser and Andy Berdan.
 *
 * extensively modified by David Borowski.
 *
 ** Copyright (C) 1998  Kirk Reiser.
 *  Copyright (C) 2003  David Borowski.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/vt.h>
#include <linux/tty.h>
#include <linux/mm.h>		/* __get_free_page() and friends */
#include <linux/vt_kern.h>
#include <linux/ctype.h>
#include <linux/selection.h>
#include <linux/unistd.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/keyboard.h>	/* for KT_SHIFT */
#include <linux/kbd_kern.h>	/* for vc_kbd_* and friends */
#include <linux/input.h>
#include <linux/kmod.h>

#include <linux/bootmem.h>	/* for alloc_bootmem */

/* speakup_*_selection */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/consolemap.h>

#include <linux/spinlock.h>
#include <linux/notifier.h>

#include <linux/uaccess.h>	/* copy_from|to|user() and others */

#include "spk_priv.h"
#include "speakup.h"

#define MAX_DELAY msecs_to_jiffies(500)
#define MINECHOCHAR SPACE

MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("Daniel Drake <dsd@gentoo.org>");
MODULE_DESCRIPTION("Speakup console speech");
MODULE_LICENSE("GPL");
MODULE_VERSION(SPEAKUP_VERSION);

char *synth_name;
module_param_named(synth, synth_name, charp, S_IRUGO);
module_param_named(quiet, quiet_boot, bool, S_IRUGO);

MODULE_PARM_DESC(synth, "Synth to start if speakup is built in.");
MODULE_PARM_DESC(quiet, "Do not announce when the synthesizer is found.");

special_func special_handler;

short pitch_shift, synth_flags;
static char buf[256];
int attrib_bleep, bleeps, bleep_time = 10;
int no_intr, spell_delay;
int key_echo, say_word_ctl;
int say_ctrl, bell_pos;
short punc_mask;
int punc_level, reading_punc;
char str_caps_start[MAXVARLEN + 1] = "\0", str_caps_stop[MAXVARLEN + 1] = "\0";
const struct st_bits_data punc_info[] = {
	{"none", "", 0},
	{"some", "/$%&@", SOME},
	{"most", "$%&#()=+*/@^<>|\\", MOST},
	{"all", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", PUNC},
	{"delimiters", "", B_WDLM},
	{"repeats", "()", CH_RPT},
	{"extended numeric", "", B_EXNUM},
	{"symbols", "", B_SYM},
	{0, 0}
};

static char mark_cut_flag;
#define MAX_KEY 160
u_char *our_keys[MAX_KEY], *shift_table;
u_char key_buf[600];
const u_char key_defaults[] = {
#include "speakupmap.h"
};

/* Speakup Cursor Track Variables */
static int cursor_track = 1, prev_cursor_track = 1;

/* cursor track modes, must be ordered same as cursor_msgs */
enum {
	CT_Off = 0,
	CT_On,
	CT_Highlight,
	CT_Window,
	CT_Max
};
#define read_all_mode CT_Max

static struct tty_struct *tty;

static void spkup_write(const char *in_buf, int count);

static char *phonetic[] = {
	"alfa", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
	"india", "juliett", "keelo", "leema", "mike", "november", "oscar",
	    "papa",
	"keh beck", "romeo", "sierra", "tango", "uniform", "victer", "whiskey",
	"x ray", "yankee", "zulu"
};

/* array of 256 char pointers (one for each character description)
 * initialized to default_chars and user selectable via
 * /proc/speakup/characters */
char *characters[256];

char *default_chars[256] = {
/*000*/ "null", "^a", "^b", "^c", "^d", "^e", "^f", "^g",
/*008*/ "^h", "^i", "^j", "^k", "^l", "^m", "^n", "^o",
/*016*/ "^p", "^q", "^r", "^s", "^t", "^u", "^v", "^w",
/*024*/ "^x", "^y", "^z", "control", "control", "control", "control",
	    "control",
/*032*/ "space", "bang!", "quote", "number", "dollar", "percent", "and",
	    "tick",
/*040*/ "left paren", "right paren", "star", "plus", "comma", "dash",
	    "dot",
	"slash",
/*048*/ "zero", "one", "two", "three", "four", "five", "six", "seven",
	"eight", "nine",
/*058*/ "colon", "semmy", "less", "equals", "greater", "question", "at",
/*065*/ "EIGH", "B", "C", "D", "E", "F", "G",
/*072*/ "H", "I", "J", "K", "L", "M", "N", "O",
/*080*/ "P", "Q", "R", "S", "T", "U", "V", "W", "X",
/*089*/ "Y", "ZED", "left bracket", "backslash", "right bracket",
	    "caret",
	"line",
/*096*/ "accent", "a", "b", "c", "d", "e", "f", "g",
/*104*/ "h", "i", "j", "k", "l", "m", "n", "o",
/*112*/ "p", "q", "r", "s", "t", "u", "v", "w",
/*120*/ "x", "y", "zed", "left brace", "bar", "right brace", "tihlduh",
/*127*/ "del", "control", "control", "control", "control", "control",
	    "control", "control", "control", "control", "control",
/*138*/ "control", "control", "control", "control", "control",
	    "control", "control", "control", "control", "control",
	    "control", "control",
/*150*/ "control", "control", "control", "control", "control",
	    "control", "control", "control", "control", "control",
/*160*/ "nbsp", "inverted bang",
/*162*/ "cents", "pounds", "currency", "yen", "broken bar", "section",
/*168*/ "diaeresis", "copyright", "female ordinal", "double left angle",
/*172*/ "not", "soft hyphen", "registered", "macron",
/*176*/ "degrees", "plus or minus", "super two", "super three",
/*180*/ "acute accent", "micro", "pilcrow", "middle dot",
/*184*/ "cedilla", "super one", "male ordinal", "double right angle",
/*188*/ "one quarter", "one half", "three quarters",
	    "inverted question",
/*192*/ "A GRAVE", "A ACUTE", "A CIRCUMFLEX", "A TILDE", "A OOMLAUT",
	    "A RING",
/*198*/ "AE", "C CIDELLA", "E GRAVE", "E ACUTE", "E CIRCUMFLEX",
	    "E OOMLAUT",
/*204*/ "I GRAVE", "I ACUTE", "I CIRCUMFLEX", "I OOMLAUT", "ETH",
	    "N TILDE",
/*210*/ "O GRAVE", "O ACUTE", "O CIRCUMFLEX", "O TILDE", "O OOMLAUT",
/*215*/ "multiplied by", "O STROKE", "U GRAVE", "U ACUTE",
	    "U CIRCUMFLEX",
/*220*/ "U OOMLAUT", "Y ACUTE", "THORN", "sharp s", "a grave",
/*225*/ "a acute", "a circumflex", "a tilde", "a oomlaut", "a ring",
/*230*/ "ae", "c cidella", "e grave", "e acute",
/*234*/ "e circumflex", "e oomlaut", "i grave", "i acute",
	    "i circumflex",
/*239*/ "i oomlaut", "eth", "n tilde", "o grave", "o acute",
	    "o circumflex",
/*245*/ "o tilde", "o oomlaut", "divided by", "o stroke", "u grave",
	    "u acute",
/* 251 */ "u circumflex", "u oomlaut", "y acute", "thorn", "y oomlaut"
};

/* array of 256 u_short (one for each character)
 * initialized to default_chartab and user selectable via
 * /sys/module/speakup/parameters/chartab */
u_short spk_chartab[256];

static u_short default_chartab[256] = {
	B_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL,	/* 0-7 */
	B_CTL, B_CTL, A_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL,	/* 8-15 */
	B_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL,	/*16-23 */
	B_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL, B_CTL,	/* 24-31 */
	WDLM, A_PUNC, PUNC, PUNC, PUNC, PUNC, PUNC, A_PUNC,	/*  !"#$%&' */
	PUNC, PUNC, PUNC, PUNC, A_PUNC, A_PUNC, A_PUNC, PUNC,	/* ()*+, -./ */
	NUM, NUM, NUM, NUM, NUM, NUM, NUM, NUM,	/* 01234567 */
	NUM, NUM, A_PUNC, PUNC, PUNC, PUNC, PUNC, A_PUNC,	/* 89:;<=>? */
	PUNC, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP,	/* @ABCDEFG */
	A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP,	/* HIJKLMNO */
	A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP,	/* PQRSTUVW */
	A_CAP, A_CAP, A_CAP, PUNC, PUNC, PUNC, PUNC, PUNC,	/* XYZ[\]^_ */
	PUNC, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA,	/* `abcdefg */
	ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA,	/* hijklmno */
	ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA,	/* pqrstuvw */
	ALPHA, ALPHA, ALPHA, PUNC, PUNC, PUNC, PUNC, 0,	/* xyz{|}~ */
	B_CAPSYM, B_CAPSYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, /* 128-134 */
	B_SYM,	/* 135 */
	B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, /* 136-142 */
	B_CAPSYM,	/* 143 */
	B_CAPSYM, B_CAPSYM, B_SYM, B_CAPSYM, B_SYM, B_SYM, B_SYM, /* 144-150 */
	B_SYM,	/* 151 */
	B_SYM, B_SYM, B_CAPSYM, B_CAPSYM, B_SYM, B_SYM, B_SYM, /*152-158 */
	B_SYM,	/* 159 */
	WDLM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_CAPSYM, /* 160-166 */
	B_SYM,	/* 167 */
	B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM,	/* 168-175 */
	B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM,	/* 176-183 */
	B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM, B_SYM,	/* 184-191 */
	A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP,	/* 192-199 */
	A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP,	/* 200-207 */
	A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, B_SYM,	/* 208-215 */
	A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, A_CAP, ALPHA,	/* 216-223 */
	ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA,	/* 224-231 */
	ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA,	/* 232-239 */
	ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, B_SYM,	/* 240-247 */
	ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA, ALPHA	/* 248-255 */
};

struct task_struct *speakup_task;
struct bleep unprocessed_sound;
static int spk_keydown;
static u_char spk_lastkey, spk_close_press, keymap_flags;
static u_char last_keycode, this_speakup_key;
static u_long last_spk_jiffy;

struct st_spk_t *speakup_console[MAX_NR_CONSOLES];

DEFINE_MUTEX(spk_mutex);

static int keyboard_notifier_call(struct notifier_block *,
				  unsigned long code, void *param);

struct notifier_block keyboard_notifier_block = {
	.notifier_call = keyboard_notifier_call,
};

static int vt_notifier_call(struct notifier_block *,
			    unsigned long code, void *param);

struct notifier_block vt_notifier_block = {
	.notifier_call = vt_notifier_call,
};

static unsigned char get_attributes(u16 *pos)
{
	return (u_char) (scr_readw(pos) >> 8);
}

static void speakup_date(struct vc_data *vc)
{
	spk_x = spk_cx = vc->vc_x;
	spk_y = spk_cy = vc->vc_y;
	spk_pos = spk_cp = vc->vc_pos;
	spk_old_attr = spk_attr;
	spk_attr = get_attributes((u_short *) spk_pos);
}

static void bleep(u_short val)
{
	static const short vals[] = {
		350, 370, 392, 414, 440, 466, 491, 523, 554, 587, 619, 659
	};
	short freq;
	int time = bleep_time;
	freq = vals[val % 12];
	if (val > 11)
		freq *= (1 << (val / 12));
	unprocessed_sound.freq = freq;
	unprocessed_sound.jiffies = msecs_to_jiffies(time);
	unprocessed_sound.active = 1;
	/* We can only have 1 active sound at a time. */
}

static void speakup_shut_up(struct vc_data *vc)
{
	if (spk_killed)
		return;
	spk_shut_up |= 0x01;
	spk_parked &= 0xfe;
	speakup_date(vc);
	if (synth != NULL)
		do_flush();
}

static void speech_kill(struct vc_data *vc)
{
	char val = synth->is_alive(synth);
	if (val == 0)
		return;

	/* re-enables synth, if disabled */
	if (val == 2 || spk_killed) {
		/* dead */
		spk_shut_up &= ~0x40;
		synth_printf("%s\n", msg_get(MSG_IAM_ALIVE));
	} else {
		synth_printf("%s\n", msg_get(MSG_YOU_KILLED_SPEAKUP));
		spk_shut_up |= 0x40;
	}
}

static void speakup_off(struct vc_data *vc)
{
	if (spk_shut_up & 0x80) {
		spk_shut_up &= 0x7f;
		synth_printf("%s\n", msg_get(MSG_HEY_THATS_BETTER));
	} else {
		spk_shut_up |= 0x80;
		synth_printf("%s\n", msg_get(MSG_YOU_TURNED_ME_OFF));
	}
	speakup_date(vc);
}

static void speakup_parked(struct vc_data *vc)
{
	if (spk_parked & 0x80) {
		spk_parked = 0;
		synth_printf("%s\n", msg_get(MSG_UNPARKED));
	} else {
		spk_parked |= 0x80;
		synth_printf("%s\n", msg_get(MSG_PARKED));
	}
}

static void speakup_cut(struct vc_data *vc)
{
	static const char err_buf[] = "set selection failed";
	int ret;

	if (!mark_cut_flag) {
		mark_cut_flag = 1;
		xs = (u_short) spk_x;
		ys = (u_short) spk_y;
		spk_sel_cons = vc;
		synth_printf("%s\n", msg_get(MSG_MARK));
		return;
	}
	xe = (u_short) spk_x;
	ye = (u_short) spk_y;
	mark_cut_flag = 0;
	synth_printf("%s\n", msg_get(MSG_CUT));

	speakup_clear_selection();
	ret = speakup_set_selection(tty);

	switch (ret) {
	case 0:
		break;		/* no error */
	case -EFAULT:
		pr_warn("%sEFAULT\n", err_buf);
		break;
	case -EINVAL:
		pr_warn("%sEINVAL\n", err_buf);
		break;
	case -ENOMEM:
		pr_warn("%sENOMEM\n", err_buf);
		break;
	}
}

static void speakup_paste(struct vc_data *vc)
{
	if (mark_cut_flag) {
		mark_cut_flag = 0;
		synth_printf("%s\n", msg_get(MSG_MARK_CLEARED));
	} else {
		synth_printf("%s\n", msg_get(MSG_PASTE));
		speakup_paste_selection(tty);
	}
}

static void say_attributes(struct vc_data *vc)
{
	int fg = spk_attr & 0x0f;
	int bg = spk_attr >> 4;
	if (fg > 8) {
		synth_printf("%s ", msg_get(MSG_BRIGHT));
		fg -= 8;
	}
	synth_printf("%s", msg_get(MSG_COLORS_START + fg));
	if (bg > 7) {
		synth_printf(" %s ", msg_get(MSG_ON_BLINKING));
		bg -= 8;
	} else
		synth_printf(" %s ", msg_get(MSG_ON));
	synth_printf("%s\n", msg_get(MSG_COLORS_START + bg));
}

enum {
	edge_top = 1,
	edge_bottom,
	edge_left,
	edge_right,
	edge_quiet
};

static void announce_edge(struct vc_data *vc, int msg_id)
{
	if (bleeps & 1)
		bleep(spk_y);
	if ((bleeps & 2) && (msg_id < edge_quiet))
		synth_printf("%s\n", msg_get(MSG_EDGE_MSGS_START + msg_id - 1));
}

static void speak_char(u_char ch)
{
	char *cp = characters[ch];
	struct var_t *direct = get_var(DIRECT);
	if (direct && direct->u.n.value) {
		if (IS_CHAR(ch, B_CAP)) {
			pitch_shift++;
			synth_printf("%s", str_caps_start);
		}
		synth_printf("%c", ch);
		if (IS_CHAR(ch, B_CAP))
			synth_printf("%s", str_caps_stop);
		return;
	}
	if (cp == NULL) {
		pr_info("speak_char: cp == NULL!\n");
		return;
	}
	synth_buffer_add(SPACE);
	if (IS_CHAR(ch, B_CAP)) {
		pitch_shift++;
		synth_printf("%s", str_caps_start);
		synth_printf("%s", cp);
		synth_printf("%s", str_caps_stop);
	} else {
		if (*cp == '^') {
			synth_printf("%s", msg_get(MSG_CTRL));
			cp++;
		}
		synth_printf("%s", cp);
	}
	synth_buffer_add(SPACE);
}

static u16 get_char(struct vc_data *vc, u16 * pos, u_char * attribs)
{
	u16 ch = ' ';
	if (vc && pos) {
		u16 w = scr_readw(pos);
		u16 c = w & 0xff;

		if (w & vc->vc_hi_font_mask)
			c |= 0x100;

		ch = inverse_translate(vc, c, 0);
		*attribs = (w & 0xff00) >> 8;
	}
	return ch;
}

static void say_char(struct vc_data *vc)
{
	u_short ch;
	spk_old_attr = spk_attr;
	ch = get_char(vc, (u_short *) spk_pos, &spk_attr);
	if (spk_attr != spk_old_attr) {
		if (attrib_bleep & 1)
			bleep(spk_y);
		if (attrib_bleep & 2)
			say_attributes(vc);
	}
	speak_char(ch & 0xff);
}

static void say_phonetic_char(struct vc_data *vc)
{
	u_short ch;
	spk_old_attr = spk_attr;
	ch = get_char(vc, (u_short *) spk_pos, &spk_attr);
	if (isascii(ch) && isalpha(ch)) {
		ch &= 0x1f;
		synth_printf("%s\n", phonetic[--ch]);
	} else {
		if (IS_CHAR(ch, B_NUM))
			synth_printf("%s ", msg_get(MSG_NUMBER));
		speak_char(ch);
	}
}

static void say_prev_char(struct vc_data *vc)
{
	spk_parked |= 0x01;
	if (spk_x == 0) {
		announce_edge(vc, edge_left);
		return;
	}
	spk_x--;
	spk_pos -= 2;
	say_char(vc);
}

static void say_next_char(struct vc_data *vc)
{
	spk_parked |= 0x01;
	if (spk_x == vc->vc_cols - 1) {
		announce_edge(vc, edge_right);
		return;
	}
	spk_x++;
	spk_pos += 2;
	say_char(vc);
}

/* get_word - will first check to see if the character under the
 * reading cursor is a space and if say_word_ctl is true it will
 * return the word space.  If say_word_ctl is not set it will check to
 * see if there is a word starting on the next position to the right
 * and return that word if it exists.  If it does not exist it will
 * move left to the beginning of any previous word on the line or the
 * beginning off the line whichever comes first.. */

static u_long get_word(struct vc_data *vc)
{
	u_long cnt = 0, tmpx = spk_x, tmp_pos = spk_pos;
	char ch;
	u_short attr_ch;
	u_char temp;
	spk_old_attr = spk_attr;
	ch = (char)get_char(vc, (u_short *) tmp_pos, &temp);

/* decided to take out the sayword if on a space (mis-information */
	if (say_word_ctl && ch == SPACE) {
		*buf = '\0';
		synth_printf("%s\n", msg_get(MSG_SPACE));
		return 0;
	} else if ((tmpx < vc->vc_cols - 2)
		   && (ch == SPACE || ch == 0 || IS_WDLM(ch))
		   && ((char)get_char(vc, (u_short *) &tmp_pos + 1, &temp) >
		       SPACE)) {
		tmp_pos += 2;
		tmpx++;
	} else
		while (tmpx > 0) {
			ch = (char)get_char(vc, (u_short *) tmp_pos - 1, &temp);
			if ((ch == SPACE || ch == 0 || IS_WDLM(ch))
			    && ((char)get_char(vc, (u_short *) tmp_pos, &temp) >
				SPACE))
				break;
			tmp_pos -= 2;
			tmpx--;
		}
	attr_ch = get_char(vc, (u_short *) tmp_pos, &spk_attr);
	buf[cnt++] = attr_ch & 0xff;
	while (tmpx < vc->vc_cols - 1) {
		tmp_pos += 2;
		tmpx++;
		ch = (char)get_char(vc, (u_short *) tmp_pos, &temp);
		if ((ch == SPACE) || ch == 0
		    || (IS_WDLM(buf[cnt - 1]) && (ch > SPACE)))
			break;
		buf[cnt++] = ch;
	}
	buf[cnt] = '\0';
	return cnt;
}

static void say_word(struct vc_data *vc)
{
	u_long cnt = get_word(vc);
	u_short saved_punc_mask = punc_mask;
	if (cnt == 0)
		return;
	punc_mask = PUNC;
	buf[cnt++] = SPACE;
	spkup_write(buf, cnt);
	punc_mask = saved_punc_mask;
}

static void say_prev_word(struct vc_data *vc)
{
	u_char temp;
	char ch;
	u_short edge_said = 0, last_state = 0, state = 0;
	spk_parked |= 0x01;

	if (spk_x == 0) {
		if (spk_y == 0) {
			announce_edge(vc, edge_top);
			return;
		}
		spk_y--;
		spk_x = vc->vc_cols;
		edge_said = edge_quiet;
	}
	while (1) {
		if (spk_x == 0) {
			if (spk_y == 0) {
				edge_said = edge_top;
				break;
			}
			if (edge_said != edge_quiet)
				edge_said = edge_left;
			if (state > 0)
				break;
			spk_y--;
			spk_x = vc->vc_cols - 1;
		} else
			spk_x--;
		spk_pos -= 2;
		ch = (char)get_char(vc, (u_short *) spk_pos, &temp);
		if (ch == SPACE || ch == 0)
			state = 0;
		else if (IS_WDLM(ch))
			state = 1;
		else
			state = 2;
		if (state < last_state) {
			spk_pos += 2;
			spk_x++;
			break;
		}
		last_state = state;
	}
	if (spk_x == 0 && edge_said == edge_quiet)
		edge_said = edge_left;
	if (edge_said > 0 && edge_said < edge_quiet)
		announce_edge(vc, edge_said);
	say_word(vc);
}

static void say_next_word(struct vc_data *vc)
{
	u_char temp;
	char ch;
	u_short edge_said = 0, last_state = 2, state = 0;
	spk_parked |= 0x01;

	if (spk_x == vc->vc_cols - 1 && spk_y == vc->vc_rows - 1) {
		announce_edge(vc, edge_bottom);
		return;
	}
	while (1) {
		ch = (char)get_char(vc, (u_short *) spk_pos, &temp);
		if (ch == SPACE || ch == 0)
			state = 0;
		else if (IS_WDLM(ch))
			state = 1;
		else
			state = 2;
		if (state > last_state)
			break;
		if (spk_x >= vc->vc_cols - 1) {
			if (spk_y == vc->vc_rows - 1) {
				edge_said = edge_bottom;
				break;
			}
			state = 0;
			spk_y++;
			spk_x = 0;
			edge_said = edge_right;
		} else
			spk_x++;
		spk_pos += 2;
		last_state = state;
	}
	if (edge_said > 0)
		announce_edge(vc, edge_said);
	say_word(vc);
}

static void spell_word(struct vc_data *vc)
{
	static char *delay_str[] = { "", ",", ".", ". .", ". . ." };
	char *cp = buf, *str_cap = str_caps_stop;
	char *cp1, *last_cap = str_caps_stop;
	u_char ch;
	if (!get_word(vc))
		return;
	while ((ch = (u_char) *cp)) {
		if (cp != buf)
			synth_printf(" %s ", delay_str[spell_delay]);
		if (IS_CHAR(ch, B_CAP)) {
			str_cap = str_caps_start;
			if (*str_caps_stop)
				pitch_shift++;
			else	/* synth has no pitch */
				last_cap = str_caps_stop;
		} else
			str_cap = str_caps_stop;
		if (str_cap != last_cap) {
			synth_printf("%s", str_cap);
			last_cap = str_cap;
		}
		if (this_speakup_key == SPELL_PHONETIC
		    && (isascii(ch) && isalpha(ch))) {
			ch &= 31;
			cp1 = phonetic[--ch];
		} else {
			cp1 = characters[ch];
			if (*cp1 == '^') {
				synth_printf("%s", msg_get(MSG_CTRL));
				cp1++;
			}
		}
		synth_printf("%s", cp1);
		cp++;
	}
	if (str_cap != str_caps_stop)
		synth_printf("%s", str_caps_stop);
}

static int get_line(struct vc_data *vc)
{
	u_long tmp = spk_pos - (spk_x * 2);
	int i = 0;
	u_char tmp2;

	spk_old_attr = spk_attr;
	spk_attr = get_attributes((u_short *) spk_pos);
	for (i = 0; i < vc->vc_cols; i++) {
		buf[i] = (u_char) get_char(vc, (u_short *) tmp, &tmp2);
		tmp += 2;
	}
	for (--i; i >= 0; i--)
		if (buf[i] != SPACE)
			break;
	return ++i;
}

static void say_line(struct vc_data *vc)
{
	int i = get_line(vc);
	char *cp;
	u_short saved_punc_mask = punc_mask;
	if (i == 0) {
		synth_printf("%s\n", msg_get(MSG_BLANK));
		return;
	}
	buf[i++] = '\n';
	if (this_speakup_key == SAY_LINE_INDENT) {
		cp = buf;
		while (*cp == SPACE)
			cp++;
		synth_printf("%d, ", (cp - buf) + 1);
	}
	punc_mask = punc_masks[reading_punc];
	spkup_write(buf, i);
	punc_mask = saved_punc_mask;
}

static void say_prev_line(struct vc_data *vc)
{
	spk_parked |= 0x01;
	if (spk_y == 0) {
		announce_edge(vc, edge_top);
		return;
	}
	spk_y--;
	spk_pos -= vc->vc_size_row;
	say_line(vc);
}

static void say_next_line(struct vc_data *vc)
{
	spk_parked |= 0x01;
	if (spk_y == vc->vc_rows - 1) {
		announce_edge(vc, edge_bottom);
		return;
	}
	spk_y++;
	spk_pos += vc->vc_size_row;
	say_line(vc);
}

static int say_from_to(struct vc_data *vc, u_long from, u_long to,
		       int read_punc)
{
	int i = 0;
	u_char tmp;
	u_short saved_punc_mask = punc_mask;
	spk_old_attr = spk_attr;
	spk_attr = get_attributes((u_short *) from);
	while (from < to) {
		buf[i++] = (char)get_char(vc, (u_short *) from, &tmp);
		from += 2;
		if (i >= vc->vc_size_row)
			break;
	}
	for (--i; i >= 0; i--)
		if (buf[i] != SPACE)
			break;
	buf[++i] = SPACE;
	buf[++i] = '\0';
	if (i < 1)
		return i;
	if (read_punc)
		punc_mask = punc_info[reading_punc].mask;
	spkup_write(buf, i);
	if (read_punc)
		punc_mask = saved_punc_mask;
	return i - 1;
}

static void say_line_from_to(struct vc_data *vc, u_long from, u_long to,
			     int read_punc)
{
	u_long start = vc->vc_origin + (spk_y * vc->vc_size_row);
	u_long end = start + (to * 2);
	start += from * 2;
	if (say_from_to(vc, start, end, read_punc) <= 0)
		if (cursor_track != read_all_mode)
			synth_printf("%s\n", msg_get(MSG_BLANK));
}

/* Sentence Reading Commands */

static int currsentence;
static int numsentences[2];
static char *sentbufend[2];
static char *sentmarks[2][10];
static int currbuf;
static int bn;
static char sentbuf[2][256];

static int say_sentence_num(int num, int prev)
{
	bn = currbuf;
	currsentence = num + 1;
	if (prev && --bn == -1)
		bn = 1;

	if (num > numsentences[bn])
		return 0;

	spkup_write(sentmarks[bn][num], sentbufend[bn] - sentmarks[bn][num]);
	return 1;
}

static int get_sentence_buf(struct vc_data *vc, int read_punc)
{
	u_long start, end;
	int i, bn;
	u_char tmp;

	currbuf++;
	if (currbuf == 2)
		currbuf = 0;
	bn = currbuf;
	start = vc->vc_origin + ((spk_y) * vc->vc_size_row);
	end = vc->vc_origin + ((spk_y) * vc->vc_size_row) + vc->vc_cols * 2;

	numsentences[bn] = 0;
	sentmarks[bn][0] = &sentbuf[bn][0];
	i = 0;
	spk_old_attr = spk_attr;
	spk_attr = get_attributes((u_short *) start);

	while (start < end) {
		sentbuf[bn][i] = (char)get_char(vc, (u_short *) start, &tmp);
		if (i > 0) {
			if (sentbuf[bn][i] == SPACE && sentbuf[bn][i - 1] == '.'
			    && numsentences[bn] < 9) {
				/* Sentence Marker */
				numsentences[bn]++;
				sentmarks[bn][numsentences[bn]] =
				    &sentbuf[bn][i];
			}
		}
		i++;
		start += 2;
		if (i >= vc->vc_size_row)
			break;
	}

	for (--i; i >= 0; i--)
		if (sentbuf[bn][i] != SPACE)
			break;

	if (i < 1)
		return -1;

	sentbuf[bn][++i] = SPACE;
	sentbuf[bn][++i] = '\0';

	sentbufend[bn] = &sentbuf[bn][i];
	return numsentences[bn];
}

static void say_screen_from_to(struct vc_data *vc, u_long from, u_long to)
{
	u_long start = vc->vc_origin, end;
	if (from > 0)
		start += from * vc->vc_size_row;
	if (to > vc->vc_rows)
		to = vc->vc_rows;
	end = vc->vc_origin + (to * vc->vc_size_row);
	for (from = start; from < end; from = to) {
		to = from + vc->vc_size_row;
		say_from_to(vc, from, to, 1);
	}
}

static void say_screen(struct vc_data *vc)
{
	say_screen_from_to(vc, 0, vc->vc_rows);
}

static void speakup_win_say(struct vc_data *vc)
{
	u_long start, end, from, to;
	if (win_start < 2) {
		synth_printf("%s\n", msg_get(MSG_NO_WINDOW));
		return;
	}
	start = vc->vc_origin + (win_top * vc->vc_size_row);
	end = vc->vc_origin + (win_bottom * vc->vc_size_row);
	while (start <= end) {
		from = start + (win_left * 2);
		to = start + (win_right * 2);
		say_from_to(vc, from, to, 1);
		start += vc->vc_size_row;
	}
}

static void top_edge(struct vc_data *vc)
{
	spk_parked |= 0x01;
	spk_pos = vc->vc_origin + 2 * spk_x;
	spk_y = 0;
	say_line(vc);
}

static void bottom_edge(struct vc_data *vc)
{
	spk_parked |= 0x01;
	spk_pos += (vc->vc_rows - spk_y - 1) * vc->vc_size_row;
	spk_y = vc->vc_rows - 1;
	say_line(vc);
}

static void left_edge(struct vc_data *vc)
{
	spk_parked |= 0x01;
	spk_pos -= spk_x * 2;
	spk_x = 0;
	say_char(vc);
}

static void right_edge(struct vc_data *vc)
{
	spk_parked |= 0x01;
	spk_pos += (vc->vc_cols - spk_x - 1) * 2;
	spk_x = vc->vc_cols - 1;
	say_char(vc);
}

static void say_first_char(struct vc_data *vc)
{
	int i, len = get_line(vc);
	u_char ch;
	spk_parked |= 0x01;
	if (len == 0) {
		synth_printf("%s\n", msg_get(MSG_BLANK));
		return;
	}
	for (i = 0; i < len; i++)
		if (buf[i] != SPACE)
			break;
	ch = buf[i];
	spk_pos -= (spk_x - i) * 2;
	spk_x = i;
	synth_printf("%d, ", ++i);
	speak_char(ch);
}

static void say_last_char(struct vc_data *vc)
{
	int len = get_line(vc);
	u_char ch;
	spk_parked |= 0x01;
	if (len == 0) {
		synth_printf("%s\n", msg_get(MSG_BLANK));
		return;
	}
	ch = buf[--len];
	spk_pos -= (spk_x - len) * 2;
	spk_x = len;
	synth_printf("%d, ", ++len);
	speak_char(ch);
}

static void say_position(struct vc_data *vc)
{
	synth_printf(msg_get(MSG_POS_INFO), spk_y + 1, spk_x + 1,
		     vc->vc_num + 1);
	synth_printf("\n");
}

/* Added by brianb */
static void say_char_num(struct vc_data *vc)
{
	u_char tmp;
	u_short ch = get_char(vc, (u_short *) spk_pos, &tmp);
	ch &= 0xff;
	synth_printf(msg_get(MSG_CHAR_INFO), ch, ch);
}

/* these are stub functions to keep keyboard.c happy. */

static void say_from_top(struct vc_data *vc)
{
	say_screen_from_to(vc, 0, spk_y);
}

static void say_to_bottom(struct vc_data *vc)
{
	say_screen_from_to(vc, spk_y, vc->vc_rows);
}

static void say_from_left(struct vc_data *vc)
{
	say_line_from_to(vc, 0, spk_x, 1);
}

static void say_to_right(struct vc_data *vc)
{
	say_line_from_to(vc, spk_x, vc->vc_cols, 1);
}

/* end of stub functions. */

static void spkup_write(const char *in_buf, int count)
{
	static int rep_count;
	static u_char ch = '\0', old_ch = '\0';
	static u_short char_type, last_type;
	int in_count = count;
	spk_keydown = 0;
	while (count--) {
		if (cursor_track == read_all_mode) {
			/* Insert Sentence Index */
			if ((in_buf == sentmarks[bn][currsentence]) &&
			    (currsentence <= numsentences[bn]))
				synth_insert_next_index(currsentence++);
		}
		ch = (u_char) *in_buf++;
		char_type = spk_chartab[ch];
		if (ch == old_ch && !(char_type & B_NUM)) {
			if (++rep_count > 2)
				continue;
		} else {
			if ((last_type & CH_RPT) && rep_count > 2) {
				synth_printf(" ");
				synth_printf(msg_get(MSG_REPEAT_DESC),
					     ++rep_count);
				synth_printf(" ");
			}
			rep_count = 0;
		}
		if (ch == spk_lastkey) {
			rep_count = 0;
			if (key_echo == 1 && ch >= MINECHOCHAR)
				speak_char(ch);
		} else if (char_type & B_ALPHA) {
			if ((synth_flags & SF_DEC) && (last_type & PUNC))
				synth_buffer_add(SPACE);
			synth_printf("%c", ch);
		} else if (char_type & B_NUM) {
			rep_count = 0;
			synth_printf("%c", ch);
		} else if (char_type & punc_mask) {
			speak_char(ch);
			char_type &= ~PUNC;	/* for dec nospell processing */
		} else if (char_type & SYNTH_OK) {
			/* these are usually puncts like . and , which synth
			 * needs for expression.
			 * suppress multiple to get rid of long pauses and
			 * clear repeat count
			 * so if someone has
			 * repeats on you don't get nothing repeated count */
			if (ch != old_ch)
				synth_printf("%c", ch);
			else
				rep_count = 0;
		} else {
/* send space and record position, if next is num overwrite space */
			if (old_ch != ch)
				synth_buffer_add(SPACE);
			else
				rep_count = 0;
		}
		old_ch = ch;
		last_type = char_type;
	}
	spk_lastkey = 0;
	if (in_count > 2 && rep_count > 2) {
		if (last_type & CH_RPT) {
			synth_printf(" ");
			synth_printf(msg_get(MSG_REPEAT_DESC2), ++rep_count);
			synth_printf(" ");
		}
		rep_count = 0;
	}
}

static const int NUM_CTL_LABELS = (MSG_CTL_END - MSG_CTL_START + 1);

static void read_all_doc(struct vc_data *vc);
static void cursor_done(u_long data);
static DEFINE_TIMER(cursor_timer, cursor_done, 0, 0);

static void do_handle_shift(struct vc_data *vc, u_char value, char up_flag)
{
	unsigned long flags;
	if (synth == NULL || up_flag || spk_killed)
		return;
	spk_lock(flags);
	if (cursor_track == read_all_mode) {
		switch (value) {
		case KVAL(K_SHIFT):
			del_timer(&cursor_timer);
			spk_shut_up &= 0xfe;
			do_flush();
			read_all_doc(vc);
			break;
		case KVAL(K_CTRL):
			del_timer(&cursor_timer);
			cursor_track = prev_cursor_track;
			spk_shut_up &= 0xfe;
			do_flush();
			break;
		}
	} else {
		spk_shut_up &= 0xfe;
		do_flush();
	}
	if (say_ctrl && value < NUM_CTL_LABELS)
		synth_printf("%s", msg_get(MSG_CTL_START + value));
	spk_unlock(flags);
}

static void do_handle_latin(struct vc_data *vc, u_char value, char up_flag)
{
	unsigned long flags;
	spk_lock(flags);
	if (up_flag) {
		spk_lastkey = spk_keydown = 0;
		spk_unlock(flags);
		return;
	}
	if (synth == NULL || spk_killed) {
		spk_unlock(flags);
		return;
	}
	spk_shut_up &= 0xfe;
	spk_lastkey = value;
	spk_keydown++;
	spk_parked &= 0xfe;
	if (key_echo == 2 && value >= MINECHOCHAR)
		speak_char(value);
	spk_unlock(flags);
}

int set_key_info(const u_char *key_info, u_char *k_buffer)
{
	int i = 0, states, key_data_len;
	const u_char *cp = key_info;
	u_char *cp1 = k_buffer;
	u_char ch, version, num_keys;
	version = *cp++;
	if (version != KEY_MAP_VER)
		return -1;
	num_keys = *cp;
	states = (int)cp[1];
	key_data_len = (states + 1) * (num_keys + 1);
	if (key_data_len + SHIFT_TBL_SIZE + 4 >= sizeof(key_buf))
		return -2;
	memset(k_buffer, 0, SHIFT_TBL_SIZE);
	memset(our_keys, 0, sizeof(our_keys));
	shift_table = k_buffer;
	our_keys[0] = shift_table;
	cp1 += SHIFT_TBL_SIZE;
	memcpy(cp1, cp, key_data_len + 3);
	/* get num_keys, states and data */
	cp1 += 2;		/* now pointing at shift states */
	for (i = 1; i <= states; i++) {
		ch = *cp1++;
		if (ch >= SHIFT_TBL_SIZE)
			return -3;
		shift_table[ch] = i;
	}
	keymap_flags = *cp1++;
	while ((ch = *cp1)) {
		if (ch >= MAX_KEY)
			return -4;
		our_keys[ch] = cp1;
		cp1 += states + 1;
	}
	return 0;
}

static struct var_t spk_vars[] = {
	/* bell must be first to set high limit */
	{BELL_POS, .u.n = {NULL, 0, 0, 0, 0, 0, NULL} },
	{SPELL_DELAY, .u.n = {NULL, 0, 0, 4, 0, 0, NULL} },
	{ATTRIB_BLEEP, .u.n = {NULL, 1, 0, 3, 0, 0, NULL} },
	{BLEEPS, .u.n = {NULL, 3, 0, 3, 0, 0, NULL} },
	{BLEEP_TIME, .u.n = {NULL, 30, 1, 200, 0, 0, NULL} },
	{PUNC_LEVEL, .u.n = {NULL, 1, 0, 4, 0, 0, NULL} },
	{READING_PUNC, .u.n = {NULL, 1, 0, 4, 0, 0, NULL} },
	{CURSOR_TIME, .u.n = {NULL, 120, 50, 600, 0, 0, NULL} },
	{SAY_CONTROL, TOGGLE_0},
	{SAY_WORD_CTL, TOGGLE_0},
	{NO_INTERRUPT, TOGGLE_0},
	{KEY_ECHO, .u.n = {NULL, 1, 0, 2, 0, 0, NULL} },
	V_LAST_VAR
};

static void toggle_cursoring(struct vc_data *vc)
{
	if (cursor_track == read_all_mode)
		cursor_track = prev_cursor_track;
	if (++cursor_track >= CT_Max)
		cursor_track = 0;
	synth_printf("%s\n", msg_get(MSG_CURSOR_MSGS_START + cursor_track));
}

void reset_default_chars(void)
{
	int i;

	/* First, free any non-default */
	for (i = 0; i < 256; i++) {
		if ((characters[i] != NULL)
		    && (characters[i] != default_chars[i]))
			kfree(characters[i]);
	}

	memcpy(characters, default_chars, sizeof(default_chars));
}

void reset_default_chartab(void)
{
	memcpy(spk_chartab, default_chartab, sizeof(default_chartab));
}

static const struct st_bits_data *pb_edit;

static int edit_bits(struct vc_data *vc, u_char type, u_char ch, u_short key)
{
	short mask = pb_edit->mask, ch_type = spk_chartab[ch];
	if (type != KT_LATIN || (ch_type & B_NUM) || ch < SPACE)
		return -1;
	if (ch == SPACE) {
		synth_printf("%s\n", msg_get(MSG_EDIT_DONE));
		special_handler = NULL;
		return 1;
	}
	if (mask < PUNC && !(ch_type & PUNC))
		return -1;
	spk_chartab[ch] ^= mask;
	speak_char(ch);
	synth_printf(" %s\n",
		     (spk_chartab[ch] & mask) ? msg_get(MSG_ON) :
		     msg_get(MSG_OFF));
	return 1;
}

/* Allocation concurrency is protected by the console semaphore */
int speakup_allocate(struct vc_data *vc)
{
	int vc_num;

	vc_num = vc->vc_num;
	if (speakup_console[vc_num] == NULL) {
		speakup_console[vc_num] = kzalloc(sizeof(*speakup_console[0]),
						  GFP_ATOMIC);
		if (speakup_console[vc_num] == NULL)
			return -ENOMEM;
		speakup_date(vc);
	} else if (!spk_parked)
		speakup_date(vc);

	return 0;
}

void speakup_deallocate(struct vc_data *vc)
{
	int vc_num;

	vc_num = vc->vc_num;
	kfree(speakup_console[vc_num]);
	speakup_console[vc_num] = NULL;
}

static u_char is_cursor;
static u_long old_cursor_pos, old_cursor_x, old_cursor_y;
static int cursor_con;

static void reset_highlight_buffers(struct vc_data *);

static int read_all_key;

static void start_read_all_timer(struct vc_data *vc, int command);

enum {
	RA_NOTHING,
	RA_NEXT_SENT,
	RA_PREV_LINE,
	RA_NEXT_LINE,
	RA_PREV_SENT,
	RA_DOWN_ARROW,
	RA_TIMER,
	RA_FIND_NEXT_SENT,
	RA_FIND_PREV_SENT,
};

static void kbd_fakekey2(struct vc_data *vc, int command)
{
	del_timer(&cursor_timer);
	speakup_fake_down_arrow();
	start_read_all_timer(vc, command);
}

static void read_all_doc(struct vc_data *vc)
{
	if ((vc->vc_num != fg_console) || synth == NULL || spk_shut_up)
		return;
	if (!synth_supports_indexing())
		return;
	if (cursor_track != read_all_mode)
		prev_cursor_track = cursor_track;
	cursor_track = read_all_mode;
	reset_index_count(0);
	if (get_sentence_buf(vc, 0) == -1)
		kbd_fakekey2(vc, RA_DOWN_ARROW);
	else {
		say_sentence_num(0, 0);
		synth_insert_next_index(0);
		start_read_all_timer(vc, RA_TIMER);
	}
}

static void stop_read_all(struct vc_data *vc)
{
	del_timer(&cursor_timer);
	cursor_track = prev_cursor_track;
	spk_shut_up &= 0xfe;
	do_flush();
}

static void start_read_all_timer(struct vc_data *vc, int command)
{
	struct var_t *cursor_timeout;

	cursor_con = vc->vc_num;
	read_all_key = command;
	cursor_timeout = get_var(CURSOR_TIME);
	mod_timer(&cursor_timer,
		  jiffies + msecs_to_jiffies(cursor_timeout->u.n.value));
}

static void handle_cursor_read_all(struct vc_data *vc, int command)
{
	int indcount, sentcount, rv, sn;

	switch (command) {
	case RA_NEXT_SENT:
		/* Get Current Sentence */
		get_index_count(&indcount, &sentcount);
		/*printk("%d %d  ", indcount, sentcount); */
		reset_index_count(sentcount + 1);
		if (indcount == 1) {
			if (!say_sentence_num(sentcount + 1, 0)) {
				kbd_fakekey2(vc, RA_FIND_NEXT_SENT);
				return;
			}
			synth_insert_next_index(0);
		} else {
			sn = 0;
			if (!say_sentence_num(sentcount + 1, 1)) {
				sn = 1;
				reset_index_count(sn);
			} else
				synth_insert_next_index(0);
			if (!say_sentence_num(sn, 0)) {
				kbd_fakekey2(vc, RA_FIND_NEXT_SENT);
				return;
			}
			synth_insert_next_index(0);
		}
		start_read_all_timer(vc, RA_TIMER);
		break;
	case RA_PREV_SENT:
		break;
	case RA_NEXT_LINE:
		read_all_doc(vc);
		break;
	case RA_PREV_LINE:
		break;
	case RA_DOWN_ARROW:
		if (get_sentence_buf(vc, 0) == -1) {
			kbd_fakekey2(vc, RA_DOWN_ARROW);
		} else {
			say_sentence_num(0, 0);
			synth_insert_next_index(0);
			start_read_all_timer(vc, RA_TIMER);
		}
		break;
	case RA_FIND_NEXT_SENT:
		rv = get_sentence_buf(vc, 0);
		if (rv == -1)
			read_all_doc(vc);
		if (rv == 0)
			kbd_fakekey2(vc, RA_FIND_NEXT_SENT);
		else {
			say_sentence_num(1, 0);
			synth_insert_next_index(0);
			start_read_all_timer(vc, RA_TIMER);
		}
		break;
	case RA_FIND_PREV_SENT:
		break;
	case RA_TIMER:
		get_index_count(&indcount, &sentcount);
		if (indcount < 2)
			kbd_fakekey2(vc, RA_DOWN_ARROW);
		else
			start_read_all_timer(vc, RA_TIMER);
		break;
	}
}

static int pre_handle_cursor(struct vc_data *vc, u_char value, char up_flag)
{
	unsigned long flags;
	spk_lock(flags);
	if (cursor_track == read_all_mode) {
		spk_parked &= 0xfe;
		if (synth == NULL || up_flag || spk_shut_up) {
			spk_unlock(flags);
			return NOTIFY_STOP;
		}
		del_timer(&cursor_timer);
		spk_shut_up &= 0xfe;
		do_flush();
		start_read_all_timer(vc, value + 1);
		spk_unlock(flags);
		return NOTIFY_STOP;
	}
	spk_unlock(flags);
	return NOTIFY_OK;
}

static void do_handle_cursor(struct vc_data *vc, u_char value, char up_flag)
{
	unsigned long flags;
	struct var_t *cursor_timeout;

	spk_lock(flags);
	spk_parked &= 0xfe;
	if (synth == NULL || up_flag || spk_shut_up || cursor_track == CT_Off) {
		spk_unlock(flags);
		return;
	}
	spk_shut_up &= 0xfe;
	if (no_intr)
		do_flush();
/* the key press flushes if !no_inter but we want to flush on cursor
 * moves regardless of no_inter state */
	is_cursor = value + 1;
	old_cursor_pos = vc->vc_pos;
	old_cursor_x = vc->vc_x;
	old_cursor_y = vc->vc_y;
	speakup_console[vc->vc_num]->ht.cy = vc->vc_y;
	cursor_con = vc->vc_num;
	if (cursor_track == CT_Highlight)
		reset_highlight_buffers(vc);
	cursor_timeout = get_var(CURSOR_TIME);
	mod_timer(&cursor_timer,
		  jiffies + msecs_to_jiffies(cursor_timeout->u.n.value));
	spk_unlock(flags);
}

static void update_color_buffer(struct vc_data *vc, const char *ic, int len)
{
	int i, bi, hi;
	int vc_num = vc->vc_num;

	bi = ((vc->vc_attr & 0x70) >> 4);
	hi = speakup_console[vc_num]->ht.highsize[bi];

	i = 0;
	if (speakup_console[vc_num]->ht.highsize[bi] == 0) {
		speakup_console[vc_num]->ht.rpos[bi] = vc->vc_pos;
		speakup_console[vc_num]->ht.rx[bi] = vc->vc_x;
		speakup_console[vc_num]->ht.ry[bi] = vc->vc_y;
	}
	while ((hi < COLOR_BUFFER_SIZE) && (i < len)) {
		if ((ic[i] > 32) && (ic[i] < 127)) {
			speakup_console[vc_num]->ht.highbuf[bi][hi] = ic[i];
			hi++;
		} else if ((ic[i] == 32) && (hi != 0)) {
			if (speakup_console[vc_num]->ht.highbuf[bi][hi - 1] !=
			    32) {
				speakup_console[vc_num]->ht.highbuf[bi][hi] =
				    ic[i];
				hi++;
			}
		}
		i++;
	}
	speakup_console[vc_num]->ht.highsize[bi] = hi;
}

static void reset_highlight_buffers(struct vc_data *vc)
{
	int i;
	int vc_num = vc->vc_num;
	for (i = 0; i < 8; i++)
		speakup_console[vc_num]->ht.highsize[i] = 0;
}

static int count_highlight_color(struct vc_data *vc)
{
	int i, bg;
	int cc;
	int vc_num = vc->vc_num;
	u16 ch;
	u16 *start = (u16 *) vc->vc_origin;

	for (i = 0; i < 8; i++)
		speakup_console[vc_num]->ht.bgcount[i] = 0;

	for (i = 0; i < vc->vc_rows; i++) {
		u16 *end = start + vc->vc_cols * 2;
		u16 *ptr;
		for (ptr = start; ptr < end; ptr++) {
			ch = get_attributes(ptr);
			bg = (ch & 0x70) >> 4;
			speakup_console[vc_num]->ht.bgcount[bg]++;
		}
		start += vc->vc_size_row;
	}

	cc = 0;
	for (i = 0; i < 8; i++)
		if (speakup_console[vc_num]->ht.bgcount[i] > 0)
			cc++;
	return cc;
}

static int get_highlight_color(struct vc_data *vc)
{
	int i, j;
	unsigned int cptr[8], tmp;
	int vc_num = vc->vc_num;

	for (i = 0; i < 8; i++)
		cptr[i] = i;

	for (i = 0; i < 7; i++)
		for (j = i + 1; j < 8; j++)
			if (speakup_console[vc_num]->ht.bgcount[cptr[i]] >
			    speakup_console[vc_num]->ht.bgcount[cptr[j]]) {
				tmp = cptr[i];
				cptr[i] = cptr[j];
				cptr[j] = tmp;
			}

	for (i = 0; i < 8; i++)
		if (speakup_console[vc_num]->ht.bgcount[cptr[i]] != 0)
			if (speakup_console[vc_num]->ht.highsize[cptr[i]] > 0)
				return cptr[i];
	return -1;
}

static int speak_highlight(struct vc_data *vc)
{
	int hc, d;
	int vc_num = vc->vc_num;
	if (count_highlight_color(vc) == 1)
		return 0;
	hc = get_highlight_color(vc);
	if (hc != -1) {
		d = vc->vc_y - speakup_console[vc_num]->ht.cy;
		if ((d == 1) || (d == -1))
			if (speakup_console[vc_num]->ht.ry[hc] != vc->vc_y)
				return 0;
		spk_parked |= 0x01;
		do_flush();
		spkup_write(speakup_console[vc_num]->ht.highbuf[hc],
			    speakup_console[vc_num]->ht.highsize[hc]);
		spk_pos = spk_cp = speakup_console[vc_num]->ht.rpos[hc];
		spk_x = spk_cx = speakup_console[vc_num]->ht.rx[hc];
		spk_y = spk_cy = speakup_console[vc_num]->ht.ry[hc];
		return 1;
	}
	return 0;
}

static void cursor_done(u_long data)
{
	struct vc_data *vc = vc_cons[cursor_con].d;
	unsigned long flags;
	del_timer(&cursor_timer);
	spk_lock(flags);
	if (cursor_con != fg_console) {
		is_cursor = 0;
		goto out;
	}
	speakup_date(vc);
	if (win_enabled) {
		if (vc->vc_x >= win_left && vc->vc_x <= win_right &&
		    vc->vc_y >= win_top && vc->vc_y <= win_bottom) {
			spk_keydown = is_cursor = 0;
			goto out;
		}
	}
	if (cursor_track == read_all_mode) {
		handle_cursor_read_all(vc, read_all_key);
		goto out;
	}
	if (cursor_track == CT_Highlight) {
		if (speak_highlight(vc)) {
			spk_keydown = is_cursor = 0;
			goto out;
		}
	}
	if (cursor_track == CT_Window)
		speakup_win_say(vc);
	else if (is_cursor == 1 || is_cursor == 4)
		say_line_from_to(vc, 0, vc->vc_cols, 0);
	else
		say_char(vc);
	spk_keydown = is_cursor = 0;
out:
	spk_unlock(flags);
}

/* called by: vt_notifier_call() */
static void speakup_bs(struct vc_data *vc)
{
	unsigned long flags;
	if (!speakup_console[vc->vc_num])
		return;
	if (!spk_trylock(flags))
		/* Speakup output, discard */
		return;
	if (!spk_parked)
		speakup_date(vc);
	if (spk_shut_up || synth == NULL) {
		spk_unlock(flags);
		return;
	}
	if (vc->vc_num == fg_console && spk_keydown) {
		spk_keydown = 0;
		if (!is_cursor)
			say_char(vc);
	}
	spk_unlock(flags);
}

/* called by: vt_notifier_call() */
static void speakup_con_write(struct vc_data *vc, const char *str, int len)
{
	unsigned long flags;
	if ((vc->vc_num != fg_console) || spk_shut_up || synth == NULL)
		return;
	if (!spk_trylock(flags))
		/* Speakup output, discard */
		return;
	if (bell_pos && spk_keydown && (vc->vc_x == bell_pos - 1))
		bleep(3);
	if ((is_cursor) || (cursor_track == read_all_mode)) {
		if (cursor_track == CT_Highlight)
			update_color_buffer(vc, str, len);
		spk_unlock(flags);
		return;
	}
	if (win_enabled) {
		if (vc->vc_x >= win_left && vc->vc_x <= win_right &&
		    vc->vc_y >= win_top && vc->vc_y <= win_bottom) {
			spk_unlock(flags);
			return;
		}
	}

	spkup_write(str, len);
	spk_unlock(flags);
}

void speakup_con_update(struct vc_data *vc)
{
	unsigned long flags;
	if (speakup_console[vc->vc_num] == NULL || spk_parked)
		return;
	if (!spk_trylock(flags))
		/* Speakup output, discard */
		return;
	speakup_date(vc);
	spk_unlock(flags);
}

static void do_handle_spec(struct vc_data *vc, u_char value, char up_flag)
{
	unsigned long flags;
	int on_off = 2;
	char *label;
	if (synth == NULL || up_flag || spk_killed)
		return;
	spk_lock(flags);
	spk_shut_up &= 0xfe;
	if (no_intr)
		do_flush();
	switch (value) {
	case KVAL(K_CAPS):
		label = msg_get(MSG_KEYNAME_CAPSLOCK);
		on_off = (vc_kbd_led(kbd_table + vc->vc_num, VC_CAPSLOCK));
		break;
	case KVAL(K_NUM):
		label = msg_get(MSG_KEYNAME_NUMLOCK);
		on_off = (vc_kbd_led(kbd_table + vc->vc_num, VC_NUMLOCK));
		break;
	case KVAL(K_HOLD):
		label = msg_get(MSG_KEYNAME_SCROLLLOCK);
		on_off = (vc_kbd_led(kbd_table + vc->vc_num, VC_SCROLLOCK));
		if (speakup_console[vc->vc_num])
			speakup_console[vc->vc_num]->tty_stopped = on_off;
		break;
	default:
		spk_parked &= 0xfe;
		spk_unlock(flags);
		return;
	}
	if (on_off < 2)
		synth_printf("%s %s\n",
			     label, msg_get(MSG_STATUS_START + on_off));
	spk_unlock(flags);
}

static int inc_dec_var(u_char value)
{
	struct st_var_header *p_header;
	struct var_t *var_data;
	char num_buf[32];
	char *cp = num_buf;
	char *pn;
	int var_id = (int)value - VAR_START;
	int how = (var_id & 1) ? E_INC : E_DEC;
	var_id = var_id / 2 + FIRST_SET_VAR;
	p_header = get_var_header(var_id);
	if (p_header == NULL)
		return -1;
	if (p_header->var_type != VAR_NUM)
		return -1;
	var_data = p_header->data;
	if (set_num_var(1, p_header, how) != 0)
		return -1;
	if (!spk_close_press) {
		for (pn = p_header->name; *pn; pn++) {
			if (*pn == '_')
				*cp = SPACE;
			else
				*cp++ = *pn;
		}
	}
	snprintf(cp, sizeof(num_buf) - (cp - num_buf), " %d ",
		 var_data->u.n.value);
	synth_printf("%s", num_buf);
	return 0;
}

static void speakup_win_set(struct vc_data *vc)
{
	char info[40];
	if (win_start > 1) {
		synth_printf("%s\n", msg_get(MSG_WINDOW_ALREADY_SET));
		return;
	}
	if (spk_x < win_left || spk_y < win_top) {
		synth_printf("%s\n", msg_get(MSG_END_BEFORE_START));
		return;
	}
	if (win_start && spk_x == win_left && spk_y == win_top) {
		win_left = 0;
		win_right = vc->vc_cols - 1;
		win_bottom = spk_y;
		snprintf(info, sizeof(info), msg_get(MSG_WINDOW_LINE),
			 (int)win_top + 1);
	} else {
		if (!win_start) {
			win_top = spk_y;
			win_left = spk_x;
		} else {
			win_bottom = spk_y;
			win_right = spk_x;
		}
		snprintf(info, sizeof(info), msg_get(MSG_WINDOW_BOUNDARY),
			 (win_start) ? msg_get(MSG_END) : msg_get(MSG_START),
			 (int)spk_y + 1, (int)spk_x + 1);
	}
	synth_printf("%s\n", info);
	win_start++;
}

static void speakup_win_clear(struct vc_data *vc)
{
	win_top = win_bottom = 0;
	win_left = win_right = 0;
	win_start = 0;
	synth_printf("%s\n", msg_get(MSG_WINDOW_CLEARED));
}

static void speakup_win_enable(struct vc_data *vc)
{
	if (win_start < 2) {
		synth_printf("%s\n", msg_get(MSG_NO_WINDOW));
		return;
	}
	win_enabled ^= 1;
	if (win_enabled)
		synth_printf("%s\n", msg_get(MSG_WINDOW_SILENCED));
	else
		synth_printf("%s\n", msg_get(MSG_WINDOW_SILENCE_DISABLED));
}

static void speakup_bits(struct vc_data *vc)
{
	int val = this_speakup_key - (FIRST_EDIT_BITS - 1);
	if (special_handler != NULL || val < 1 || val > 6) {
		synth_printf("%s\n", msg_get(MSG_ERROR));
		return;
	}
	pb_edit = &punc_info[val];
	synth_printf(msg_get(MSG_EDIT_PROMPT), pb_edit->name);
	special_handler = edit_bits;
}

static int handle_goto(struct vc_data *vc, u_char type, u_char ch, u_short key)
{
	static u_char *goto_buf = "\0\0\0\0\0\0";
	static int num;
	int maxlen, go_pos;
	char *cp;
	if (type == KT_SPKUP && ch == SPEAKUP_GOTO)
		goto do_goto;
	if (type == KT_LATIN && ch == '\n')
		goto do_goto;
	if (type != 0)
		goto oops;
	if (ch == 8) {
		if (num == 0)
			return -1;
		ch = goto_buf[--num];
		goto_buf[num] = '\0';
		spkup_write(&ch, 1);
		return 1;
	}
	if (ch < '+' || ch > 'y')
		goto oops;
	goto_buf[num++] = ch;
	goto_buf[num] = '\0';
	spkup_write(&ch, 1);
	maxlen = (*goto_buf >= '0') ? 3 : 4;
	if ((ch == '+' || ch == '-') && num == 1)
		return 1;
	if (ch >= '0' && ch <= '9' && num < maxlen)
		return 1;
	if (num < maxlen - 1 || num > maxlen)
		goto oops;
	if (ch < 'x' || ch > 'y') {
oops:
		if (!spk_killed)
			synth_printf(" %s\n", msg_get(MSG_GOTO_CANCELED));
		goto_buf[num = 0] = '\0';
		special_handler = NULL;
		return 1;
	}
	cp = speakup_s2i(goto_buf, &go_pos);
	goto_pos = (u_long) go_pos;
	if (*cp == 'x') {
		if (*goto_buf < '0')
			goto_pos += spk_x;
		else
			goto_pos--;
		if (goto_pos < 0)
			goto_pos = 0;
		if (goto_pos >= vc->vc_cols)
			goto_pos = vc->vc_cols - 1;
		goto_x = 1;
	} else {
		if (*goto_buf < '0')
			goto_pos += spk_y;
		else
			goto_pos--;
		if (goto_pos < 0)
			goto_pos = 0;
		if (goto_pos >= vc->vc_rows)
			goto_pos = vc->vc_rows - 1;
		goto_x = 0;
	}
	goto_buf[num = 0] = '\0';
do_goto:
	special_handler = NULL;
	spk_parked |= 0x01;
	if (goto_x) {
		spk_pos -= spk_x * 2;
		spk_x = goto_pos;
		spk_pos += goto_pos * 2;
		say_word(vc);
	} else {
		spk_y = goto_pos;
		spk_pos = vc->vc_origin + (goto_pos * vc->vc_size_row);
		say_line(vc);
	}
	return 1;
}

static void speakup_goto(struct vc_data *vc)
{
	if (special_handler != NULL) {
		synth_printf("%s\n", msg_get(MSG_ERROR));
		return;
	}
	synth_printf("%s\n", msg_get(MSG_GOTO));
	special_handler = handle_goto;
	return;
}

static void speakup_help(struct vc_data *vc)
{
	handle_help(vc, KT_SPKUP, SPEAKUP_HELP, 0);
}

static void do_nothing(struct vc_data *vc)
{
	return;			/* flush done in do_spkup */
}

static u_char key_speakup, spk_key_locked;

static void speakup_lock(struct vc_data *vc)
{
	if (!spk_key_locked)
		spk_key_locked = key_speakup = 16;
	else
		spk_key_locked = key_speakup = 0;
}

typedef void (*spkup_hand) (struct vc_data *);
spkup_hand spkup_handler[] = {
	/* must be ordered same as defines in speakup.h */
	do_nothing, speakup_goto, speech_kill, speakup_shut_up,
	speakup_cut, speakup_paste, say_first_char, say_last_char,
	say_char, say_prev_char, say_next_char,
	say_word, say_prev_word, say_next_word,
	say_line, say_prev_line, say_next_line,
	top_edge, bottom_edge, left_edge, right_edge,
	spell_word, spell_word, say_screen,
	say_position, say_attributes,
	speakup_off, speakup_parked, say_line,	/* this is for indent */
	say_from_top, say_to_bottom,
	say_from_left, say_to_right,
	say_char_num, speakup_bits, speakup_bits, say_phonetic_char,
	speakup_bits, speakup_bits, speakup_bits,
	speakup_win_set, speakup_win_clear, speakup_win_enable, speakup_win_say,
	speakup_lock, speakup_help, toggle_cursoring, read_all_doc, NULL
};

static void do_spkup(struct vc_data *vc, u_char value)
{
	if (spk_killed && value != SPEECH_KILL)
		return;
	spk_keydown = 0;
	spk_lastkey = 0;
	spk_shut_up &= 0xfe;
	this_speakup_key = value;
	if (value < SPKUP_MAX_FUNC && spkup_handler[value]) {
		do_flush();
		(*spkup_handler[value]) (vc);
	} else {
		if (inc_dec_var(value) < 0)
			bleep(9);
	}
}

static const char *pad_chars = "0123456789+-*/\015,.?()";

int
speakup_key(struct vc_data *vc, int shift_state, int keycode, u_short keysym,
	    int up_flag)
{
	unsigned long flags;
	int kh;
	u_char *key_info;
	u_char type = KTYP(keysym), value = KVAL(keysym), new_key = 0;
	u_char shift_info, offset;
	int ret = 0;
	if (synth == NULL)
		return 0;

	spk_lock(flags);
	tty = vc->port.tty;
	if (type >= 0xf0)
		type -= 0xf0;
	if (type == KT_PAD
		&& (vc_kbd_led(kbd_table + fg_console, VC_NUMLOCK))) {
		if (up_flag) {
			spk_keydown = 0;
			goto out;
		}
		value = spk_lastkey = pad_chars[value];
		spk_keydown++;
		spk_parked &= 0xfe;
		goto no_map;
	}
	if (keycode >= MAX_KEY)
		goto no_map;
	key_info = our_keys[keycode];
	if (key_info == 0)
		goto no_map;
	/* Check valid read all mode keys */
	if ((cursor_track == read_all_mode) && (!up_flag)) {
		switch (value) {
		case KVAL(K_DOWN):
		case KVAL(K_UP):
		case KVAL(K_LEFT):
		case KVAL(K_RIGHT):
		case KVAL(K_PGUP):
		case KVAL(K_PGDN):
			break;
		default:
			stop_read_all(vc);
			break;
		}
	}
	shift_info = (shift_state & 0x0f) + key_speakup;
	offset = shift_table[shift_info];
	if (offset) {
		new_key = key_info[offset];
		if (new_key) {
			ret = 1;
			if (new_key == SPK_KEY) {
				if (!spk_key_locked)
					key_speakup = (up_flag) ? 0 : 16;
				if (up_flag || spk_killed)
					goto out;
				spk_shut_up &= 0xfe;
				do_flush();
				goto out;
			}
			if (up_flag)
				goto out;
			if (last_keycode == keycode &&
			    last_spk_jiffy + MAX_DELAY > jiffies) {
				spk_close_press = 1;
				offset = shift_table[shift_info + 32];
				/* double press? */
				if (offset && key_info[offset])
					new_key = key_info[offset];
			}
			last_keycode = keycode;
			last_spk_jiffy = jiffies;
			type = KT_SPKUP;
			value = new_key;
		}
	}
no_map:
	if (type == KT_SPKUP && special_handler == NULL) {
		do_spkup(vc, new_key);
		spk_close_press = 0;
		ret = 1;
		goto out;
	}
	if (up_flag || spk_killed || type == KT_SHIFT)
		goto out;
	spk_shut_up &= 0xfe;
	kh = (value == KVAL(K_DOWN))
	    || (value == KVAL(K_UP))
	    || (value == KVAL(K_LEFT))
	    || (value == KVAL(K_RIGHT));
	if ((cursor_track != read_all_mode) || !kh)
		if (!no_intr)
			do_flush();
	if (special_handler) {
		if (type == KT_SPEC && value == 1) {
			value = '\n';
			type = KT_LATIN;
		} else if (type == KT_LETTER)
			type = KT_LATIN;
		else if (value == 0x7f)
			value = 8;	/* make del = backspace */
		ret = (*special_handler) (vc, type, value, keycode);
		spk_close_press = 0;
		if (ret < 0)
			bleep(9);
		goto out;
	}
	last_keycode = 0;
out:
	spk_unlock(flags);
	return ret;
}

static int keyboard_notifier_call(struct notifier_block *nb,
				  unsigned long code, void *_param)
{
	struct keyboard_notifier_param *param = _param;
	struct vc_data *vc = param->vc;
	int up = !param->down;
	int ret = NOTIFY_OK;
	static int keycode;	/* to hold the current keycode */

	if (vc->vc_mode == KD_GRAPHICS)
		return ret;

	/*
	 * First, determine whether we are handling a fake keypress on
	 * the current processor.  If we are, then return NOTIFY_OK,
	 * to pass the keystroke up the chain.  This prevents us from
	 * trying to take the Speakup lock while it is held by the
	 * processor on which the simulated keystroke was generated.
	 * Also, the simulated keystrokes should be ignored by Speakup.
	 */

	if (speakup_fake_key_pressed())
		return ret;

	switch (code) {
	case KBD_KEYCODE:
		/* speakup requires keycode and keysym currently */
		keycode = param->value;
		break;
	case KBD_UNBOUND_KEYCODE:
		/* not used yet */
		break;
	case KBD_UNICODE:
		/* not used yet */
		break;
	case KBD_KEYSYM:
		if (speakup_key(vc, param->shift, keycode, param->value, up))
			ret = NOTIFY_STOP;
		else if (KTYP(param->value) == KT_CUR)
			ret = pre_handle_cursor(vc, KVAL(param->value), up);
		break;
	case KBD_POST_KEYSYM:{
			unsigned char type = KTYP(param->value) - 0xf0;
			unsigned char val = KVAL(param->value);
			switch (type) {
			case KT_SHIFT:
				do_handle_shift(vc, val, up);
				break;
			case KT_LATIN:
			case KT_LETTER:
				do_handle_latin(vc, val, up);
				break;
			case KT_CUR:
				do_handle_cursor(vc, val, up);
				break;
			case KT_SPEC:
				do_handle_spec(vc, val, up);
				break;
			}
			break;
		}
	}
	return ret;
}

static int vt_notifier_call(struct notifier_block *nb,
			    unsigned long code, void *_param)
{
	struct vt_notifier_param *param = _param;
	struct vc_data *vc = param->vc;
	switch (code) {
	case VT_ALLOCATE:
		if (vc->vc_mode == KD_TEXT)
			speakup_allocate(vc);
		break;
	case VT_DEALLOCATE:
		speakup_deallocate(vc);
		break;
	case VT_WRITE:
		if (param->c == '\b')
			speakup_bs(vc);
		else if (param->c < 0x100) {
			char d = param->c;
			speakup_con_write(vc, &d, 1);
		}
		break;
	case VT_UPDATE:
		speakup_con_update(vc);
		break;
	}
	return NOTIFY_OK;
}

/* called by: module_exit() */
static void __exit speakup_exit(void)
{
	int i;

	unregister_keyboard_notifier(&keyboard_notifier_block);
	unregister_vt_notifier(&vt_notifier_block);
	speakup_unregister_devsynth();
	del_timer(&cursor_timer);
	kthread_stop(speakup_task);
	speakup_task = NULL;
	mutex_lock(&spk_mutex);
	synth_release();
	mutex_unlock(&spk_mutex);

	speakup_kobj_exit();

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		kfree(speakup_console[i]);

	speakup_remove_virtual_keyboard();

	for (i = 0; i < MAXVARS; i++)
		speakup_unregister_var(i);

	for (i = 0; i < 256; i++) {
		if (characters[i] != default_chars[i])
			kfree(characters[i]);
	}

	free_user_msgs();
}

/* call by: module_init() */
static int __init speakup_init(void)
{
	int i;
	long err = 0;
	struct st_spk_t *first_console;
	struct vc_data *vc = vc_cons[fg_console].d;
	struct var_t *var;

	/* These first few initializations cannot fail. */
	initialize_msgs();	/* Initialize arrays for i18n. */
	reset_default_chars();
	reset_default_chartab();
	strlwr(synth_name);
	spk_vars[0].u.n.high = vc->vc_cols;
	for (var = spk_vars; var->var_id != MAXVARS; var++)
		speakup_register_var(var);
	for (var = synth_time_vars;
	     (var->var_id >= 0) && (var->var_id < MAXVARS); var++)
		speakup_register_var(var);
	for (i = 1; punc_info[i].mask != 0; i++)
		set_mask_bits(0, i, 2);

	set_key_info(key_defaults, key_buf);
	if (quiet_boot)
		spk_shut_up |= 0x01;

	/* From here on out, initializations can fail. */
	err = speakup_add_virtual_keyboard();
	if (err)
		goto error_virtkeyboard;

	first_console = kzalloc(sizeof(*first_console), GFP_KERNEL);
	if (!first_console) {
		err = -ENOMEM;
		goto error_alloc;
	}

	speakup_console[vc->vc_num] = first_console;
	speakup_date(vc);

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		if (vc_cons[i].d) {
			err = speakup_allocate(vc_cons[i].d);
			if (err)
				goto error_kobjects;
		}

	err = speakup_kobj_init();
	if (err)
		goto error_kobjects;

	synth_init(synth_name);
	speakup_register_devsynth();
	/*
	 * register_devsynth might fail, but this error is not fatal.
	 * /dev/synth is an extra feature; the rest of Speakup
	 * will work fine without it.
	 */

	err = register_keyboard_notifier(&keyboard_notifier_block);
	if (err)
		goto error_kbdnotifier;
	err = register_vt_notifier(&vt_notifier_block);
	if (err)
		goto error_vtnotifier;

	speakup_task = kthread_create(speakup_thread, NULL, "speakup");

	if (IS_ERR(speakup_task)) {
		err = PTR_ERR(speakup_task);
		goto error_task;
	}

	set_user_nice(speakup_task, 10);
	wake_up_process(speakup_task);

	pr_info("speakup %s: initialized\n", SPEAKUP_VERSION);
	pr_info("synth name on entry is: %s\n", synth_name);
	goto out;

error_task:
	unregister_vt_notifier(&vt_notifier_block);

error_vtnotifier:
	unregister_keyboard_notifier(&keyboard_notifier_block);
	del_timer(&cursor_timer);

error_kbdnotifier:
	speakup_unregister_devsynth();
	mutex_lock(&spk_mutex);
	synth_release();
	mutex_unlock(&spk_mutex);
	speakup_kobj_exit();

error_kobjects:
	for (i = 0; i < MAX_NR_CONSOLES; i++)
		kfree(speakup_console[i]);

error_alloc:
	speakup_remove_virtual_keyboard();

error_virtkeyboard:
	for (i = 0; i < MAXVARS; i++)
		speakup_unregister_var(i);

	for (i = 0; i < 256; i++) {
		if (characters[i] != default_chars[i])
			kfree(characters[i]);
	}

	free_user_msgs();

out:
	return err;
}

module_init(speakup_init);
module_exit(speakup_exit);
