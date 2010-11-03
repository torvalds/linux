/* Internationalization implementation.  Includes definitions of English
 * string arrays, and the i18n pointer. */

#include <linux/slab.h>		/* For kmalloc. */
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/string.h>
#include "speakup.h"
#include "spk_priv.h"

static char *speakup_msgs[MSG_LAST_INDEX];
static char *speakup_default_msgs[MSG_LAST_INDEX] = {
	[MSG_BLANK] = "blank",
	[MSG_IAM_ALIVE] = "I'm aLive!",
	[MSG_YOU_KILLED_SPEAKUP] = "You killed speakup!",
	[MSG_HEY_THATS_BETTER] = "hey. That's better!",
	[MSG_YOU_TURNED_ME_OFF] = "You turned me off!",
	[MSG_PARKED] = "parked!",
	[MSG_UNPARKED] = "unparked!",
	[MSG_MARK] = "mark",
	[MSG_CUT] = "cut",
	[MSG_MARK_CLEARED] = "mark, cleared",
	[MSG_PASTE] = "paste",
	[MSG_BRIGHT] = "bright",
	[MSG_ON_BLINKING] = "on blinking",
	[MSG_OFF] = "off",
	[MSG_ON] = "on",
	[MSG_NO_WINDOW] = "no window",
	[MSG_CURSORING_OFF] = "cursoring off",
	[MSG_CURSORING_ON] = "cursoring on",
	[MSG_HIGHLIGHT_TRACKING] = "highlight tracking",
	[MSG_READ_WINDOW] = "read windo",
	[MSG_READ_ALL] = "read all",
	[MSG_EDIT_DONE] = "edit done",
	[MSG_WINDOW_ALREADY_SET] = "window already set, clear then reset",
	[MSG_END_BEFORE_START] = "error end before start",
	[MSG_WINDOW_CLEARED] = "window cleared",
	[MSG_WINDOW_SILENCED] = "window silenced",
	[MSG_WINDOW_SILENCE_DISABLED] = "window silence disabled",
	[MSG_ERROR] = "error",
	[MSG_GOTO_CANCELED] = "goto canceled",
	[MSG_GOTO] = "go to?",
	[MSG_LEAVING_HELP] = "leaving help",
	[MSG_IS_UNASSIGNED] = "is unassigned",
	[MSG_HELP_INFO] =
	"press space to exit, up or down to scroll, or a letter to go to a command",
	[MSG_EDGE_TOP] = "top,",
	[MSG_EDGE_BOTTOM] = "bottom,",
	[MSG_EDGE_LEFT] = "left,",
	[MSG_EDGE_RIGHT] = "right,",
	[MSG_NUMBER] = "number",
	[MSG_SPACE] = "space",
	[MSG_START] = "start",
	[MSG_END] = "end",
	[MSG_CTRL] = "control-",
	[MSG_DISJUNCTION] = "or",

/* Messages with embedded format specifiers. */
	[MSG_POS_INFO] = "line %ld, col %ld, t t y %d",
	[MSG_CHAR_INFO] = "hex %02x, decimal %d",
	[MSG_REPEAT_DESC] = "times %d .",
	[MSG_REPEAT_DESC2] = "repeated %d .",
	[MSG_WINDOW_LINE] = "window is line %d",
	[MSG_WINDOW_BOUNDARY] = "%s at line %d, column %d",
	[MSG_EDIT_PROMPT] = "edit  %s, press space when done",
	[MSG_NO_COMMAND] = "no commands for %c",
	[MSG_KEYDESC] = "is %s",

	/* Control keys. */
	/* Most of these duplicate the entries in state names. */
	[MSG_CTL_SHIFT] = "shift",
	[MSG_CTL_ALTGR] = "altgr",
	[MSG_CTL_CONTROL] = "control",
	[MSG_CTL_ALT] = "ault",
	[MSG_CTL_LSHIFT] = "l shift",
	[MSG_CTL_SPEAKUP] = "speakup",
	[MSG_CTL_LCONTROL] = "l control",
	[MSG_CTL_RCONTROL] = "r control",
	[MSG_CTL_CAPSSHIFT] = "caps shift",

	/* Color names. */
	[MSG_COLOR_BLACK] = "black",
	[MSG_COLOR_BLUE] = "blue",
	[MSG_COLOR_GREEN] = "green",
	[MSG_COLOR_CYAN] = "cyan",
	[MSG_COLOR_RED] = "red",
	[MSG_COLOR_MAGENTA] = "magenta",
	[MSG_COLOR_YELLOW] = "yellow",
	[MSG_COLOR_WHITE] = "white",
	[MSG_COLOR_GREY] = "grey",

	/* Names of key states. */
	[MSG_STATE_DOUBLE] = "double",
	[MSG_STATE_SPEAKUP] = "speakup",
	[MSG_STATE_ALT] = "alt",
	[MSG_STATE_CONTROL] = "ctrl",
	[MSG_STATE_ALTGR] = "altgr",
	[MSG_STATE_SHIFT] = "shift",

	/* Key names. */
	[MSG_KEYNAME_ESC] = "escape",
	[MSG_KEYNAME_1] = "1",
	[MSG_KEYNAME_2] = "2",
	[MSG_KEYNAME_3] = "3",
	[MSG_KEYNAME_4] = "4",
	[MSG_KEYNAME_5] = "5",
	[MSG_KEYNAME_6] = "6",
	[MSG_KEYNAME_7] = "7",
	[MSG_KEYNAME_8] = "8",
	[MSG_KEYNAME_9] = "9",
	[MSG_KEYNAME_0] = "0",
	[MSG_KEYNAME_DASH] = "minus",
	[MSG_KEYNAME_EQUAL] = "equal",
	[MSG_KEYNAME_BS] = "back space",
	[MSG_KEYNAME_TAB] = "tab",
	[MSG_KEYNAME_Q] = "q",
	[MSG_KEYNAME_W] = "w",
	[MSG_KEYNAME_E] = "e",
	[MSG_KEYNAME_R] = "r",
	[MSG_KEYNAME_T] = "t",
	[MSG_KEYNAME_Y] = "y",
	[MSG_KEYNAME_U] = "u",
	[MSG_KEYNAME_I] = "i",
	[MSG_KEYNAME_O] = "o",
	[MSG_KEYNAME_P] = "p",
	[MSG_KEYNAME_LEFTBRACE] = "left brace",
	[MSG_KEYNAME_RIGHTBRACE] = "right brace",
	[MSG_KEYNAME_ENTER] = "enter",
	[MSG_KEYNAME_LEFTCTRL] = "left control",
	[MSG_KEYNAME_A] = "a",
	[MSG_KEYNAME_S] = "s",
	[MSG_KEYNAME_D] = "d",
	[MSG_KEYNAME_F] = "f",
	[MSG_KEYNAME_G] = "g",
	[MSG_KEYNAME_H] = "h",
	[MSG_KEYNAME_J] = "j",
	[MSG_KEYNAME_K] = "k",
	[MSG_KEYNAME_L] = "l",
	[MSG_KEYNAME_SEMICOLON] = "semicolon",
	[MSG_KEYNAME_SINGLEQUOTE] = "apostrophe",
	[MSG_KEYNAME_GRAVE] = "accent",
	[MSG_KEYNAME_LEFTSHFT] = "left shift",
	[MSG_KEYNAME_BACKSLASH] = "back slash",
	[MSG_KEYNAME_Z] = "z",
	[MSG_KEYNAME_X] = "x",
	[MSG_KEYNAME_C] = "c",
	[MSG_KEYNAME_V] = "v",
	[MSG_KEYNAME_B] = "b",
	[MSG_KEYNAME_N] = "n",
	[MSG_KEYNAME_M] = "m",
	[MSG_KEYNAME_COMMA] = "comma",
	[MSG_KEYNAME_DOT] = "dot",
	[MSG_KEYNAME_SLASH] = "slash",
	[MSG_KEYNAME_RIGHTSHFT] = "right shift",
	[MSG_KEYNAME_KPSTAR] = "keypad asterisk",
	[MSG_KEYNAME_LEFTALT] = "left alt",
	[MSG_KEYNAME_SPACE] = "space",
	[MSG_KEYNAME_CAPSLOCK] = "caps lock",
	[MSG_KEYNAME_F1] = "f1",
	[MSG_KEYNAME_F2] = "f2",
	[MSG_KEYNAME_F3] = "f3",
	[MSG_KEYNAME_F4] = "f4",
	[MSG_KEYNAME_F5] = "f5",
	[MSG_KEYNAME_F6] = "f6",
	[MSG_KEYNAME_F7] = "f7",
	[MSG_KEYNAME_F8] = "f8",
	[MSG_KEYNAME_F9] = "f9",
	[MSG_KEYNAME_F10] = "f10",
	[MSG_KEYNAME_NUMLOCK] = "num lock",
	[MSG_KEYNAME_SCROLLLOCK] = "scroll lock",
	[MSG_KEYNAME_KP7] = "keypad 7",
	[MSG_KEYNAME_KP8] = "keypad 8",
	[MSG_KEYNAME_KP9] = "keypad 9",
	[MSG_KEYNAME_KPMINUS] = "keypad minus",
	[MSG_KEYNAME_KP4] = "keypad 4",
	[MSG_KEYNAME_KP5] = "keypad 5",
	[MSG_KEYNAME_KP6] = "keypad 6",
	[MSG_KEYNAME_KPPLUS] = "keypad plus",
	[MSG_KEYNAME_KP1] = "keypad 1",
	[MSG_KEYNAME_KP2] = "keypad 2",
	[MSG_KEYNAME_KP3] = "keypad 3",
	[MSG_KEYNAME_KP0] = "keypad 0",
	[MSG_KEYNAME_KPDOT] = "keypad dot",
	[MSG_KEYNAME_103RD] = "103rd",
	[MSG_KEYNAME_F13] = "f13",
	[MSG_KEYNAME_102ND] = "102nd",
	[MSG_KEYNAME_F11] = "f11",
	[MSG_KEYNAME_F12] = "f12",
	[MSG_KEYNAME_F14] = "f14",
	[MSG_KEYNAME_F15] = "f15",
	[MSG_KEYNAME_F16] = "f16",
	[MSG_KEYNAME_F17] = "f17",
	[MSG_KEYNAME_F18] = "f18",
	[MSG_KEYNAME_F19] = "f19",
	[MSG_KEYNAME_F20] = "f20",
	[MSG_KEYNAME_KPENTER] = "keypad enter",
	[MSG_KEYNAME_RIGHTCTRL] = "right control",
	[MSG_KEYNAME_KPSLASH] = "keypad slash",
	[MSG_KEYNAME_SYSRQ] = "sysrq",
	[MSG_KEYNAME_RIGHTALT] = "right alt",
	[MSG_KEYNAME_LF] = "line feed",
	[MSG_KEYNAME_HOME] = "home",
	[MSG_KEYNAME_UP] = "up",
	[MSG_KEYNAME_PGUP] = "page up",
	[MSG_KEYNAME_LEFT] = "left",
	[MSG_KEYNAME_RIGHT] = "right",
	[MSG_KEYNAME_END] = "end",
	[MSG_KEYNAME_DOWN] = "down",
	[MSG_KEYNAME_PGDN] = "page down",
	[MSG_KEYNAME_INS] = "insert",
	[MSG_KEYNAME_DEL] = "delete",
	[MSG_KEYNAME_MACRO] = "macro",
	[MSG_KEYNAME_MUTE] = "mute",
	[MSG_KEYNAME_VOLDOWN] = "volume down",
	[MSG_KEYNAME_VOLUP] = "volume up",
	[MSG_KEYNAME_POWER] = "power",
	[MSG_KEYNAME_KPEQUAL] = "keypad equal",
	[MSG_KEYNAME_KPPLUSDASH] = "keypad plusminus",
	[MSG_KEYNAME_PAUSE] = "pause",
	[MSG_KEYNAME_F21] = "f21",
	[MSG_KEYNAME_F22] = "f22",
	[MSG_KEYNAME_F23] = "f23",
	[MSG_KEYNAME_F24] = "f24",
	[MSG_KEYNAME_KPCOMMA] = "keypad comma",
	[MSG_KEYNAME_LEFTMETA] = "left meta",
	[MSG_KEYNAME_RIGHTMETA] = "right meta",
	[MSG_KEYNAME_COMPOSE] = "compose",
	[MSG_KEYNAME_STOP] = "stop",
	[MSG_KEYNAME_AGAIN] = "again",
	[MSG_KEYNAME_PROPS] = "props",
	[MSG_KEYNAME_UNDO] = "undo",
	[MSG_KEYNAME_FRONT] = "front",
	[MSG_KEYNAME_COPY] = "copy",
	[MSG_KEYNAME_OPEN] = "open",
	[MSG_KEYNAME_PASTE] = "paste",
	[MSG_KEYNAME_FIND] = "find",
	[MSG_KEYNAME_CUT] = "cut",
	[MSG_KEYNAME_HELP] = "help",
	[MSG_KEYNAME_MENU] = "menu",
	[MSG_KEYNAME_CALC] = "calc",
	[MSG_KEYNAME_SETUP] = "setup",
	[MSG_KEYNAME_SLEEP] = "sleep",
	[MSG_KEYNAME_WAKEUP] = "wakeup",
	[MSG_KEYNAME_FILE] = "file",
	[MSG_KEYNAME_SENDFILE] = "send file",
	[MSG_KEYNAME_DELFILE] = "delete file",
	[MSG_KEYNAME_XFER] = "transfer",
	[MSG_KEYNAME_PROG1] = "prog1",
	[MSG_KEYNAME_PROG2] = "prog2",
	[MSG_KEYNAME_WWW] = "www",
	[MSG_KEYNAME_MSDOS] = "msdos",
	[MSG_KEYNAME_COFFEE] = "coffee",
	[MSG_KEYNAME_DIRECTION] = "direction",
	[MSG_KEYNAME_CYCLEWINDOWS] = "cycle windows",
	[MSG_KEYNAME_MAIL] = "mail",
	[MSG_KEYNAME_BOOKMARKS] = "bookmarks",
	[MSG_KEYNAME_COMPUTER] = "computer",
	[MSG_KEYNAME_BACK] = "back",
	[MSG_KEYNAME_FORWARD] = "forward",
	[MSG_KEYNAME_CLOSECD] = "close cd",
	[MSG_KEYNAME_EJECTCD] = "eject cd",
	[MSG_KEYNAME_EJECTCLOSE] = "eject close cd",
	[MSG_KEYNAME_NEXTSONG] = "next song",
	[MSG_KEYNAME_PLAYPAUSE] = "play pause",
	[MSG_KEYNAME_PREVSONG] = "previous song",
	[MSG_KEYNAME_STOPCD] = "stop cd",
	[MSG_KEYNAME_RECORD] = "record",
	[MSG_KEYNAME_REWIND] = "rewind",
	[MSG_KEYNAME_PHONE] = "phone",
	[MSG_KEYNAME_ISO] = "iso",
	[MSG_KEYNAME_CONFIG] = "config",
	[MSG_KEYNAME_HOMEPG] = "home page",
	[MSG_KEYNAME_REFRESH] = "refresh",
	[MSG_KEYNAME_EXIT] = "exit",
	[MSG_KEYNAME_MOVE] = "move",
	[MSG_KEYNAME_EDIT] = "edit",
	[MSG_KEYNAME_SCROLLUP] = "scroll up",
	[MSG_KEYNAME_SCROLLDN] = "scroll down",
	[MSG_KEYNAME_KPLEFTPAR] = "keypad left paren",
	[MSG_KEYNAME_KPRIGHTPAR] = "keypad right paren",

	/* Function names. */
	[MSG_FUNCNAME_ATTRIB_BLEEP_DEC] = "attribute bleep decrement",
	[MSG_FUNCNAME_ATTRIB_BLEEP_INC] = "attribute bleep increment",
	[MSG_FUNCNAME_BLEEPS_DEC] = "bleeps decrement",
	[MSG_FUNCNAME_BLEEPS_INC] = "bleeps increment",
	[MSG_FUNCNAME_CHAR_FIRST] = "character, first",
	[MSG_FUNCNAME_CHAR_LAST] = "character, last",
	[MSG_FUNCNAME_CHAR_CURRENT] = "character, say current",
	[MSG_FUNCNAME_CHAR_HEX_AND_DEC] = "character, say hex and decimal",
	[MSG_FUNCNAME_CHAR_NEXT] = "character, say next",
	[MSG_FUNCNAME_CHAR_PHONETIC] = "character, say phonetic",
	[MSG_FUNCNAME_CHAR_PREVIOUS] = "character, say previous",
	[MSG_FUNCNAME_CURSOR_PARK] = "cursor park",
	[MSG_FUNCNAME_CUT] = "cut",
	[MSG_FUNCNAME_EDIT_DELIM] = "edit delimiters",
	[MSG_FUNCNAME_EDIT_EXNUM] = "edit exnum",
	[MSG_FUNCNAME_EDIT_MOST] = "edit most",
	[MSG_FUNCNAME_EDIT_REPEATS] = "edit repeats",
	[MSG_FUNCNAME_EDIT_SOME] = "edit some",
	[MSG_FUNCNAME_GOTO] = "go to",
	[MSG_FUNCNAME_GOTO_BOTTOM] = "go to bottom edge",
	[MSG_FUNCNAME_GOTO_LEFT] = "go to left edge",
	[MSG_FUNCNAME_GOTO_RIGHT] = "go to right edge",
	[MSG_FUNCNAME_GOTO_TOP] = "go to top edge",
	[MSG_FUNCNAME_HELP] = "help",
	[MSG_FUNCNAME_LINE_SAY_CURRENT] = "line, say current",
	[MSG_FUNCNAME_LINE_SAY_NEXT] = "line, say next",
	[MSG_FUNCNAME_LINE_SAY_PREVIOUS] = "line, say previous",
	[MSG_FUNCNAME_LINE_SAY_WITH_INDENT] = "line, say with indent",
	[MSG_FUNCNAME_PASTE] = "paste",
	[MSG_FUNCNAME_PITCH_DEC] = "pitch decrement",
	[MSG_FUNCNAME_PITCH_INC] = "pitch increment",
	[MSG_FUNCNAME_PUNC_DEC] = "punctuation decrement",
	[MSG_FUNCNAME_PUNC_INC] = "punctuation increment",
	[MSG_FUNCNAME_PUNC_LEVEL_DEC] = "punc level decrement",
	[MSG_FUNCNAME_PUNC_LEVEL_INC] = "punc level increment",
	[MSG_FUNCNAME_QUIET] = "quiet",
	[MSG_FUNCNAME_RATE_DEC] = "rate decrement",
	[MSG_FUNCNAME_RATE_INC] = "rate increment",
	[MSG_FUNCNAME_READING_PUNC_DEC] = "reading punctuation decrement",
	[MSG_FUNCNAME_READING_PUNC_INC] = "reading punctuation increment",
	[MSG_FUNCNAME_SAY_ATTRIBUTES] = "say attributes",
	[MSG_FUNCNAME_SAY_FROM_LEFT] = "say from left",
	[MSG_FUNCNAME_SAY_FROM_TOP] = "say from top",
	[MSG_FUNCNAME_SAY_POSITION] = "say position",
	[MSG_FUNCNAME_SAY_SCREEN] = "say screen",
	[MSG_FUNCNAME_SAY_TO_BOTTOM] = "say to bottom",
	[MSG_FUNCNAME_SAY_TO_RIGHT] = "say to right",
	[MSG_FUNCNAME_SPEAKUP] = "speakup",
	[MSG_FUNCNAME_SPEAKUP_LOCK] = "speakup lock",
	[MSG_FUNCNAME_SPEAKUP_OFF] = "speakup off",
	[MSG_FUNCNAME_SPEECH_KILL] = "speech kill",
	[MSG_FUNCNAME_SPELL_DELAY_DEC] = "spell delay decrement",
	[MSG_FUNCNAME_SPELL_DELAY_INC] = "spell delay increment",
	[MSG_FUNCNAME_SPELL_WORD] = "spell word",
	[MSG_FUNCNAME_SPELL_WORD_PHONETICALLY] = "spell word phoneticly",
	[MSG_FUNCNAME_TONE_DEC] = "tone decrement",
	[MSG_FUNCNAME_TONE_INC] = "tone increment",
	[MSG_FUNCNAME_VOICE_DEC] = "voice decrement",
	[MSG_FUNCNAME_VOICE_INC] = "voice increment",
	[MSG_FUNCNAME_VOLUME_DEC] = "volume decrement",
	[MSG_FUNCNAME_VOLUME_INC] = "volume increment",
	[MSG_FUNCNAME_WINDOW_CLEAR] = "window, clear",
	[MSG_FUNCNAME_WINDOW_SAY] = "window, say",
	[MSG_FUNCNAME_WINDOW_SET] = "window, set",
	[MSG_FUNCNAME_WINDOW_SILENCE] = "window, silence",
	[MSG_FUNCNAME_WORD_SAY_CURRENT] = "word, say current",
	[MSG_FUNCNAME_WORD_SAY_NEXT] = "word, say next",
	[MSG_FUNCNAME_WORD_SAY_PREVIOUS] = "word, say previous",
};

static struct msg_group_t all_groups[] = {
	{
		.name = "ctl_keys",
		.start = MSG_CTL_START,
		.end = MSG_CTL_END,
	},
	{
		.name = "colors",
		.start = MSG_COLORS_START,
		.end = MSG_COLORS_END,
	},
	{
		.name = "formatted",
		.start = MSG_FORMATTED_START,
		.end = MSG_FORMATTED_END,
	},
	{
		.name = "function_names",
		.start = MSG_FUNCNAMES_START,
		.end = MSG_FUNCNAMES_END,
	},
	{
		.name = "key_names",
		.start = MSG_KEYNAMES_START,
		.end = MSG_KEYNAMES_END,
	},
	{
		.name = "announcements",
		.start = MSG_ANNOUNCEMENTS_START,
		.end = MSG_ANNOUNCEMENTS_END,
	},
	{
		.name = "states",
		.start = MSG_STATES_START,
		.end = MSG_STATES_END,
	},
};

static const  int num_groups = sizeof(all_groups) / sizeof(struct msg_group_t);

char *msg_get(enum msg_index_t index)
{
	char *ch;

	ch = speakup_msgs[index];
	return ch;
}

/*
 * Function: next_specifier
 * Finds the start of the next format specifier in the argument string.
 * Return value: pointer to start of format
 * specifier, or NULL if no specifier exists.
*/
static char *next_specifier(char *input)
{
	int found = 0;
	char *next_percent = input;

	while ((next_percent != NULL) && !found) {
		next_percent = strchr(next_percent, '%');
		if (next_percent != NULL) {
			/* skip over doubled percent signs */
			while ((next_percent[0] == '%')
			       && (next_percent[1] == '%'))
				next_percent += 2;
			if (*next_percent == '%')
				found = 1;
			else if (*next_percent == '\0')
				next_percent = NULL;
		}
	}

	return next_percent;
}

/* Skip over 0 or more flags. */
static char *skip_flags(char *input)
{
	while ((*input != '\0') && strchr(" 0+-#", *input))
		input++;
	return input;
}

/* Skip over width.precision, if it exists. */
static char *skip_width(char *input)
{
	while (isdigit(*input))
		input++;
	if (*input == '.') {
		input++;
		while (isdigit(*input))
			input++;
	}
	return input;
}

/*
 * Skip past the end of the conversion part.
 * Note that this code only accepts a handful of conversion specifiers:
 * c d s x and ld.  Not accidental; these are exactly the ones used in
 * the default group of formatted messages.
*/
static char *skip_conversion(char *input)
{
	if ((input[0] == 'l') && (input[1] == 'd'))
		input += 2;
	else if ((*input != '\0') && strchr("cdsx", *input))
		input++;
	return input;
}

/*
 * Function: find_specifier_end
 * Return a pointer to the end of the format specifier.
*/
static char *find_specifier_end(char *input)
{
	input++;		/* Advance over %. */
	input = skip_flags(input);
	input = skip_width(input);
	input = skip_conversion(input);
	return input;
}

/*
 * Function: compare_specifiers
 * Compare the format specifiers pointed to by *input1 and *input2.
 * Return 1 if they are the same, 0 otherwise.  Advance *input1 and *input2
 * so that they point to the character following the end of the specifier.
*/
static int compare_specifiers(char **input1, char **input2)
{
	int same = 0;
	char *end1 = find_specifier_end(*input1);
	char *end2 = find_specifier_end(*input2);
	size_t length1 = end1 - *input1;
	size_t length2 = end2 - *input2;

	if ((length1 == length2) && !memcmp(*input1, *input2, length1))
		same = 1;

	*input1 = end1;
	*input2 = end2;
	return same;
}

/*
 * Function: fmt_validate
 * Check that two format strings contain the same number of format specifiers,
 * and that the order of specifiers is the same in both strings.
 * Return 1 if the condition holds, 0 if it doesn't.
*/
static int fmt_validate(char *template, char *user)
{
	int valid = 1;
	int still_comparing = 1;
	char *template_ptr = template;
	char *user_ptr = user;

	while (still_comparing && valid) {
		template_ptr = next_specifier(template_ptr);
		user_ptr = next_specifier(user_ptr);
		if (template_ptr && user_ptr) {
			/* Both have at least one more specifier. */
			valid = compare_specifiers(&template_ptr, &user_ptr);
		} else {
			/* No more format specifiers in one or both strings. */
			still_comparing = 0;
			/* See if one has more specifiers than the other. */
			if (template_ptr || user_ptr)
				valid = 0;
		}
	}
	return valid;
}

/*
 * Function: msg_set
 * Description: Add a user-supplied message to the user_messages array.
 * The message text is copied to a memory area allocated with kmalloc.
 * If the function fails, then user_messages is untouched.
 * Arguments:
 * - index: a message number, as found in i18n.h.
 * - text:  text of message.  Not NUL-terminated.
 * - length: number of bytes in text.
 * Failure conditions:
 * -EINVAL -  Invalid format specifiers in formatted message or illegal index.
 * -ENOMEM -  Unable to allocate memory.
*/
ssize_t msg_set(enum msg_index_t index, char *text, size_t length)
{
	int rc = 0;
	char *newstr = NULL;
	unsigned long flags;

	if ((index >= MSG_FIRST_INDEX) && (index < MSG_LAST_INDEX)) {
		newstr = kmalloc(length + 1, GFP_KERNEL);
		if (newstr) {
			memcpy(newstr, text, length);
			newstr[length] = '\0';
			if ((index >= MSG_FORMATTED_START
			&& index <= MSG_FORMATTED_END)
				&& !fmt_validate(speakup_default_msgs[index],
				newstr)) {
				return -EINVAL;
			}
			spk_lock(flags);
			if (speakup_msgs[index] != speakup_default_msgs[index])
				kfree(speakup_msgs[index]);
			speakup_msgs[index] = newstr;
			spk_unlock(flags);
		} else {
			rc = -ENOMEM;
		}
	} else {
		rc = -EINVAL;
	}
	return rc;
}

/*
 * Find a message group, given its name.  Return a pointer to the structure
 * if found, or NULL otherwise.
*/
struct msg_group_t *find_msg_group(const char *group_name)
{
	struct msg_group_t *group = NULL;
	int i;

	for (i = 0; i < num_groups; i++) {
		if (!strcmp(all_groups[i].name, group_name)) {
			group = &all_groups[i];
			break;
		}
	}
	return group;
}

void reset_msg_group(struct msg_group_t *group)
{
	unsigned long flags;
	enum msg_index_t i;

	spk_lock(flags);

	for (i = group->start; i <= group->end; i++) {
		if (speakup_msgs[i] != speakup_default_msgs[i])
			kfree(speakup_msgs[i]);
		speakup_msgs[i] = speakup_default_msgs[i];
	}
	spk_unlock(flags);
}

/* Called at initialization time, to establish default messages. */
void initialize_msgs(void)
{
	memcpy(speakup_msgs, speakup_default_msgs,
		sizeof(speakup_default_msgs));
}

/* Free user-supplied strings when module is unloaded: */
void free_user_msgs(void)
{
	enum msg_index_t index;
	unsigned long flags;

	spk_lock(flags);
	for (index = MSG_FIRST_INDEX; index < MSG_LAST_INDEX; index++) {
		if (speakup_msgs[index] != speakup_default_msgs[index]) {
			kfree(speakup_msgs[index]);
			speakup_msgs[index] = speakup_default_msgs[index];
		}
	}
	spk_unlock(flags);
}
