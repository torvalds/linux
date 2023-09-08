// SPDX-License-Identifier: GPL-2.0
#include <linux/ctype.h>
#include "spk_types.h"
#include "spk_priv.h"
#include "speakup.h"

static struct st_var_header var_headers[] = {
	{ "version", VERSION, VAR_PROC, NULL, NULL },
	{ "synth_name", SYNTH, VAR_PROC, NULL, NULL },
	{ "keymap", KEYMAP, VAR_PROC, NULL, NULL },
	{ "silent", SILENT, VAR_PROC, NULL, NULL },
	{ "punc_some", PUNC_SOME, VAR_PROC, NULL, NULL },
	{ "punc_most", PUNC_MOST, VAR_PROC, NULL, NULL },
	{ "punc_all", PUNC_ALL, VAR_PROC, NULL, NULL },
	{ "delimiters", DELIM, VAR_PROC, NULL, NULL },
	{ "repeats", REPEATS, VAR_PROC, NULL, NULL },
	{ "ex_num", EXNUMBER, VAR_PROC, NULL, NULL },
	{ "characters", CHARS, VAR_PROC, NULL, NULL },
	{ "synth_direct", SYNTH_DIRECT, VAR_PROC, NULL, NULL },
	{ "caps_start", CAPS_START, VAR_STRING, spk_str_caps_start, NULL },
	{ "caps_stop", CAPS_STOP, VAR_STRING, spk_str_caps_stop, NULL },
	{ "delay_time", DELAY, VAR_TIME, NULL, NULL },
	{ "trigger_time", TRIGGER, VAR_TIME, NULL, NULL },
	{ "jiffy_delta", JIFFY, VAR_TIME, NULL, NULL },
	{ "full_time", FULL, VAR_TIME, NULL, NULL },
	{ "flush_time", FLUSH, VAR_TIME, NULL, NULL },
	{ "spell_delay", SPELL_DELAY, VAR_NUM, &spk_spell_delay, NULL },
	{ "bleeps", BLEEPS, VAR_NUM, &spk_bleeps, NULL },
	{ "attrib_bleep", ATTRIB_BLEEP, VAR_NUM, &spk_attrib_bleep, NULL },
	{ "bleep_time", BLEEP_TIME, VAR_TIME, &spk_bleep_time, NULL },
	{ "cursor_time", CURSOR_TIME, VAR_TIME, NULL, NULL },
	{ "punc_level", PUNC_LEVEL, VAR_NUM, &spk_punc_level, NULL },
	{ "reading_punc", READING_PUNC, VAR_NUM, &spk_reading_punc, NULL },
	{ "say_control", SAY_CONTROL, VAR_NUM, &spk_say_ctrl, NULL },
	{ "say_word_ctl", SAY_WORD_CTL, VAR_NUM, &spk_say_word_ctl, NULL },
	{ "no_interrupt", NO_INTERRUPT, VAR_NUM, &spk_no_intr, NULL },
	{ "key_echo", KEY_ECHO, VAR_NUM, &spk_key_echo, NULL },
	{ "bell_pos", BELL_POS, VAR_NUM, &spk_bell_pos, NULL },
	{ "rate", RATE, VAR_NUM, NULL, NULL },
	{ "pitch", PITCH, VAR_NUM, NULL, NULL },
	{ "inflection", INFLECTION, VAR_NUM, NULL, NULL },
	{ "vol", VOL, VAR_NUM, NULL, NULL },
	{ "tone", TONE, VAR_NUM, NULL, NULL },
	{ "punct", PUNCT, VAR_NUM, NULL, NULL   },
	{ "voice", VOICE, VAR_NUM, NULL, NULL },
	{ "freq", FREQUENCY, VAR_NUM, NULL, NULL },
	{ "lang", LANG, VAR_NUM, NULL, NULL },
	{ "chartab", CHARTAB, VAR_PROC, NULL, NULL },
	{ "direct", DIRECT, VAR_NUM, NULL, NULL },
	{ "pause", PAUSE, VAR_STRING, spk_str_pause, NULL },
	{ "cur_phonetic", CUR_PHONETIC, VAR_NUM, &spk_cur_phonetic, NULL },
};

static struct st_var_header *var_ptrs[MAXVARS] = { NULL, NULL, NULL };

static struct punc_var_t punc_vars[] = {
	{ PUNC_SOME, 1 },
	{ PUNC_MOST, 2 },
	{ PUNC_ALL, 3 },
	{ DELIM, 4 },
	{ REPEATS, 5 },
	{ EXNUMBER, 6 },
	{ -1, -1 },
};

int spk_chartab_get_value(char *keyword)
{
	int value = 0;

	if (!strcmp(keyword, "ALPHA"))
		value = ALPHA;
	else if (!strcmp(keyword, "B_CTL"))
		value = B_CTL;
	else if (!strcmp(keyword, "WDLM"))
		value = WDLM;
	else if (!strcmp(keyword, "A_PUNC"))
		value = A_PUNC;
	else if (!strcmp(keyword, "PUNC"))
		value = PUNC;
	else if (!strcmp(keyword, "NUM"))
		value = NUM;
	else if (!strcmp(keyword, "A_CAP"))
		value = A_CAP;
	else if (!strcmp(keyword, "B_CAPSYM"))
		value = B_CAPSYM;
	else if (!strcmp(keyword, "B_SYM"))
		value = B_SYM;
	return value;
}

void speakup_register_var(struct var_t *var)
{
	static char nothing[2] = "\0";
	int i;
	struct st_var_header *p_header;

	BUG_ON(!var || var->var_id < 0 || var->var_id >= MAXVARS);
	if (!var_ptrs[0]) {
		for (i = 0; i < MAXVARS; i++) {
			p_header = &var_headers[i];
			var_ptrs[p_header->var_id] = p_header;
			p_header->data = NULL;
		}
	}
	p_header = var_ptrs[var->var_id];
	if (p_header->data)
		return;
	p_header->data = var;
	switch (p_header->var_type) {
	case VAR_STRING:
		spk_set_string_var(nothing, p_header, 0);
		break;
	case VAR_NUM:
	case VAR_TIME:
		spk_set_num_var(0, p_header, E_DEFAULT);
		break;
	default:
		break;
	}
}

void speakup_unregister_var(enum var_id_t var_id)
{
	struct st_var_header *p_header;

	BUG_ON(var_id < 0 || var_id >= MAXVARS);
	p_header = var_ptrs[var_id];
	p_header->data = NULL;
}

struct st_var_header *spk_get_var_header(enum var_id_t var_id)
{
	struct st_var_header *p_header;

	if (var_id < 0 || var_id >= MAXVARS)
		return NULL;
	p_header = var_ptrs[var_id];
	if (!p_header->data)
		return NULL;
	return p_header;
}
EXPORT_SYMBOL_GPL(spk_get_var_header);

struct st_var_header *spk_var_header_by_name(const char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i = 0; i < MAXVARS; i++) {
		if (strcmp(name, var_ptrs[i]->name) == 0)
			return var_ptrs[i];
	}
	return NULL;
}

struct var_t *spk_get_var(enum var_id_t var_id)
{
	BUG_ON(var_id < 0 || var_id >= MAXVARS);
	BUG_ON(!var_ptrs[var_id]);
	return var_ptrs[var_id]->data;
}
EXPORT_SYMBOL_GPL(spk_get_var);

struct punc_var_t *spk_get_punc_var(enum var_id_t var_id)
{
	struct punc_var_t *rv = NULL;
	struct punc_var_t *where;

	where = punc_vars;
	while ((where->var_id != -1) && (!rv)) {
		if (where->var_id == var_id)
			rv = where;
		else
			where++;
	}
	return rv;
}

/* handlers for setting vars */
int spk_set_num_var(int input, struct st_var_header *var, int how)
{
	int val;
	int *p_val = var->p_val;
	char buf[32];
	char *cp;
	struct var_t *var_data = var->data;

	if (!var_data)
		return -ENODATA;

	val = var_data->u.n.value;
	switch (how) {
	case E_NEW_DEFAULT:
		if (input < var_data->u.n.low || input > var_data->u.n.high)
			return -ERANGE;
		var_data->u.n.default_val = input;
		return 0;
	case E_DEFAULT:
		val = var_data->u.n.default_val;
		break;
	case E_SET:
		val = input;
		break;
	case E_INC:
		val += input;
		break;
	case E_DEC:
		val -= input;
		break;
	}

	if (val < var_data->u.n.low || val > var_data->u.n.high)
		return -ERANGE;

	var_data->u.n.value = val;
	if (var->var_type == VAR_TIME && p_val) {
		*p_val = msecs_to_jiffies(val);
		return 0;
	}
	if (p_val)
		*p_val = val;
	if (var->var_id == PUNC_LEVEL) {
		spk_punc_mask = spk_punc_masks[val];
	}
	if (var_data->u.n.multiplier != 0)
		val *= var_data->u.n.multiplier;
	val += var_data->u.n.offset;

	if (!synth)
		return 0;
	if (synth->synth_adjust && synth->synth_adjust(synth, var))
		return 0;
	if (var->var_id < FIRST_SYNTH_VAR)
		return 0;

	if (!var_data->u.n.synth_fmt)
		return 0;
	if (var->var_id == PITCH)
		cp = spk_pitch_buff;
	else
		cp = buf;
	if (!var_data->u.n.out_str)
		sprintf(cp, var_data->u.n.synth_fmt, (int)val);
	else
		sprintf(cp, var_data->u.n.synth_fmt,
			var_data->u.n.out_str[val]);
	synth_printf("%s", cp);
	return 0;
}
EXPORT_SYMBOL_GPL(spk_set_num_var);

int spk_set_string_var(const char *page, struct st_var_header *var, int len)
{
	struct var_t *var_data = var->data;

	if (!var_data)
		return -ENODATA;
	if (len > MAXVARLEN)
		return -E2BIG;
	if (!len) {
		if (!var_data->u.s.default_val)
			return 0;
		if (!var->p_val)
			var->p_val = var_data->u.s.default_val;
		if (var->p_val != var_data->u.s.default_val)
			strcpy((char *)var->p_val, var_data->u.s.default_val);
		return -ERESTART;
	} else if (var->p_val) {
		strcpy((char *)var->p_val, page);
	} else {
		return -E2BIG;
	}
	return 0;
}

/*
 * spk_set_mask_bits sets or clears the punc/delim/repeat bits,
 * if input is null uses the defaults.
 * values for how: 0 clears bits of chars supplied,
 * 1 clears allk, 2 sets bits for chars
 */
int spk_set_mask_bits(const char *input, const int which, const int how)
{
	u_char *cp;
	short mask = spk_punc_info[which].mask;

	if (how & 1) {
		for (cp = (u_char *)spk_punc_info[3].value; *cp; cp++)
			spk_chartab[*cp] &= ~mask;
	}
	cp = (u_char *)input;
	if (!cp) {
		cp = spk_punc_info[which].value;
	} else {
		for (; *cp; cp++) {
			if (*cp < SPACE)
				break;
			if (mask < PUNC) {
				if (!(spk_chartab[*cp] & PUNC))
					break;
			} else if (spk_chartab[*cp] & B_NUM) {
				break;
			}
		}
		if (*cp)
			return -EINVAL;
		cp = (u_char *)input;
	}
	if (how & 2) {
		for (; *cp; cp++)
			if (*cp > SPACE)
				spk_chartab[*cp] |= mask;
	} else {
		for (; *cp; cp++)
			if (*cp > SPACE)
				spk_chartab[*cp] &= ~mask;
	}
	return 0;
}

char *spk_strlwr(char *s)
{
	char *p;

	if (!s)
		return NULL;

	for (p = s; *p; p++)
		*p = tolower(*p);
	return s;
}

char *spk_s2uchar(char *start, char *dest)
{
	int val;

	/* Do not replace with kstrtoul: here we need start to be updated */
	val = simple_strtoul(skip_spaces(start), &start, 10);
	if (*start == ',')
		start++;
	*dest = (u_char)val;
	return start;
}
