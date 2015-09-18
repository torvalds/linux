/* speakup_keyhelp.c
 * help module for speakup
 *
 *written by David Borowski.
 *
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

#include <linux/keyboard.h>
#include "spk_priv.h"
#include "speakup.h"

#define MAXFUNCS 130
#define MAXKEYS 256
static const int num_key_names = MSG_KEYNAMES_END - MSG_KEYNAMES_START + 1;
static u_short key_offsets[MAXFUNCS], key_data[MAXKEYS];
static u_short masks[] = { 32, 16, 8, 4, 2, 1 };

static short letter_offsets[26] = {
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1 };

static u_char funcvals[] = {
	ATTRIB_BLEEP_DEC, ATTRIB_BLEEP_INC, BLEEPS_DEC, BLEEPS_INC,
	SAY_FIRST_CHAR, SAY_LAST_CHAR, SAY_CHAR, SAY_CHAR_NUM,
	SAY_NEXT_CHAR, SAY_PHONETIC_CHAR, SAY_PREV_CHAR, SPEAKUP_PARKED,
	SPEAKUP_CUT, EDIT_DELIM, EDIT_EXNUM, EDIT_MOST,
	EDIT_REPEAT, EDIT_SOME, SPEAKUP_GOTO, BOTTOM_EDGE,
	LEFT_EDGE, RIGHT_EDGE, TOP_EDGE, SPEAKUP_HELP,
	SAY_LINE, SAY_NEXT_LINE, SAY_PREV_LINE, SAY_LINE_INDENT,
	SPEAKUP_PASTE, PITCH_DEC, PITCH_INC, PUNCT_DEC,
	PUNCT_INC, PUNC_LEVEL_DEC, PUNC_LEVEL_INC, SPEAKUP_QUIET,
	RATE_DEC, RATE_INC, READING_PUNC_DEC, READING_PUNC_INC,
	SAY_ATTRIBUTES, SAY_FROM_LEFT, SAY_FROM_TOP, SAY_POSITION,
	SAY_SCREEN, SAY_TO_BOTTOM, SAY_TO_RIGHT, SPK_KEY,
	SPK_LOCK, SPEAKUP_OFF, SPEECH_KILL, SPELL_DELAY_DEC,
	SPELL_DELAY_INC, SPELL_WORD, SPELL_PHONETIC, TONE_DEC,
	TONE_INC, VOICE_DEC, VOICE_INC, VOL_DEC,
	VOL_INC, CLEAR_WIN, SAY_WIN, SET_WIN,
	ENABLE_WIN, SAY_WORD, SAY_NEXT_WORD, SAY_PREV_WORD, 0
};

static u_char *state_tbl;
static int cur_item, nstates;

static void build_key_data(void)
{
	u_char *kp, counters[MAXFUNCS], ch, ch1;
	u_short *p_key = key_data, key;
	int i, offset = 1;

	nstates = (int)(state_tbl[-1]);
	memset(counters, 0, sizeof(counters));
	memset(key_offsets, 0, sizeof(key_offsets));
	kp = state_tbl + nstates + 1;
	while (*kp++) {
		/* count occurrences of each function */
		for (i = 0; i < nstates; i++, kp++) {
			if (!*kp)
				continue;
			if ((state_tbl[i]&16) != 0 && *kp == SPK_KEY)
				continue;
			counters[*kp]++;
		}
	}
	for (i = 0; i < MAXFUNCS; i++) {
		if (counters[i] == 0)
			continue;
		key_offsets[i] = offset;
		offset += (counters[i]+1);
		if (offset >= MAXKEYS)
			break;
	}
/* leave counters set so high keycodes come first.
 * this is done so num pad and other extended keys maps are spoken before
 * the alpha with speakup type mapping.
 */
	kp = state_tbl + nstates + 1;
	while ((ch = *kp++)) {
		for (i = 0; i < nstates; i++) {
			ch1 = *kp++;
			if (!ch1)
				continue;
			if ((state_tbl[i]&16) != 0 && ch1 == SPK_KEY)
				continue;
			key = (state_tbl[i] << 8) + ch;
			counters[ch1]--;
			offset = key_offsets[ch1];
			if (!offset)
				continue;
			p_key = key_data + offset + counters[ch1];
			*p_key = key;
		}
	}
}

static void say_key(int key)
{
	int i, state = key >> 8;

	key &= 0xff;
	for (i = 0; i < 6; i++) {
		if (state & masks[i])
			synth_printf(" %s", spk_msg_get(MSG_STATES_START + i));
	}
	if ((key > 0) && (key <= num_key_names))
		synth_printf(" %s\n",
				spk_msg_get(MSG_KEYNAMES_START + (key - 1)));
}

static int help_init(void)
{
	char start = SPACE;
	int i;
	int num_funcs = MSG_FUNCNAMES_END - MSG_FUNCNAMES_START + 1;

	state_tbl = spk_our_keys[0]+SHIFT_TBL_SIZE+2;
	for (i = 0; i < num_funcs; i++) {
		char *cur_funcname = spk_msg_get(MSG_FUNCNAMES_START + i);

		if (start == *cur_funcname)
			continue;
		start = *cur_funcname;
		letter_offsets[(start&31)-1] = i;
	}
	return 0;
}

int spk_handle_help(struct vc_data *vc, u_char type, u_char ch, u_short key)
{
	int i, n;
	char *name;
	u_char func, *kp;
	u_short *p_keys, val;

	if (letter_offsets[0] == -1)
		help_init();
	if (type == KT_LATIN) {
		if (ch == SPACE) {
			spk_special_handler = NULL;
			synth_printf("%s\n", spk_msg_get(MSG_LEAVING_HELP));
			return 1;
		}
		ch |= 32; /* lower case */
		if (ch < 'a' || ch > 'z')
			return -1;
		if (letter_offsets[ch-'a'] == -1) {
			synth_printf(spk_msg_get(MSG_NO_COMMAND), ch);
			synth_printf("\n");
			return 1;
		}
		cur_item = letter_offsets[ch-'a'];
	} else if (type == KT_CUR) {
		if (ch == 0
		    && (MSG_FUNCNAMES_START + cur_item + 1) <=
		    MSG_FUNCNAMES_END)
			cur_item++;
		else if (ch == 3 && cur_item > 0)
			cur_item--;
		else
			return -1;
	} else if (type == KT_SPKUP
			&& ch == SPEAKUP_HELP
			&& !spk_special_handler) {
		spk_special_handler = spk_handle_help;
		synth_printf("%s\n", spk_msg_get(MSG_HELP_INFO));
		build_key_data(); /* rebuild each time in case new mapping */
		return 1;
	} else {
		name = NULL;
		if ((type != KT_SPKUP) && (key > 0) && (key <= num_key_names)) {
			synth_printf("%s\n",
				spk_msg_get(MSG_KEYNAMES_START + key-1));
			return 1;
		}
		for (i = 0; funcvals[i] != 0 && !name; i++) {
			if (ch == funcvals[i])
				name = spk_msg_get(MSG_FUNCNAMES_START + i);
		}
		if (!name)
			return -1;
		kp = spk_our_keys[key]+1;
		for (i = 0; i < nstates; i++) {
			if (ch == kp[i])
				break;
		}
		key += (state_tbl[i] << 8);
		say_key(key);
		synth_printf(spk_msg_get(MSG_KEYDESC), name);
		synth_printf("\n");
		return 1;
	}
	name = spk_msg_get(MSG_FUNCNAMES_START + cur_item);
	func = funcvals[cur_item];
	synth_printf("%s", name);
	if (key_offsets[func] == 0) {
		synth_printf(" %s\n", spk_msg_get(MSG_IS_UNASSIGNED));
		return 1;
	}
	p_keys = key_data + key_offsets[func];
	for (n = 0; p_keys[n]; n++) {
		val = p_keys[n];
		if (n > 0)
			synth_printf("%s ", spk_msg_get(MSG_DISJUNCTION));
		say_key(val);
	}
	return 1;
}
