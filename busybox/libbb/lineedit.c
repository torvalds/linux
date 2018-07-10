/* vi: set sw=4 ts=4: */
/*
 * Command line editing.
 *
 * Copyright (c) 1986-2003 may safely be consumed by a BSD or GPL license.
 * Written by:   Vladimir Oleynik <dzo@simtreas.ru>
 *
 * Used ideas:
 *      Adam Rogoyski    <rogoyski@cs.utexas.edu>
 *      Dave Cinege      <dcinege@psychosis.com>
 *      Jakub Jelinek (c) 1995
 *      Erik Andersen    <andersen@codepoet.org> (Majorly adjusted for busybox)
 *
 * This code is 'as is' with no warranty.
 */
/*
 * Usage and known bugs:
 * Terminal key codes are not extensive, more needs to be added.
 * This version was created on Debian GNU/Linux 2.x.
 * Delete, Backspace, Home, End, and the arrow keys were tested
 * to work in an Xterm and console. Ctrl-A also works as Home.
 * Ctrl-E also works as End.
 *
 * The following readline-like commands are not implemented:
 * CTL-t -- Transpose two characters
 *
 * lineedit does not know that the terminal escape sequences do not
 * take up space on the screen. The redisplay code assumes, unless
 * told otherwise, that each character in the prompt is a printable
 * character that takes up one character position on the screen.
 * You need to tell lineedit that some sequences of characters
 * in the prompt take up no screen space. Compatibly with readline,
 * use the \[ escape to begin a sequence of non-printing characters,
 * and the \] escape to signal the end of such a sequence. Example:
 *
 * PS1='\[\033[01;32m\]\u@\h\[\033[01;34m\] \w \$\[\033[00m\] '
 *
 * Unicode in PS1 is not fully supported: prompt length calulation is wrong,
 * resulting in line wrap problems with long (multi-line) input.
 */
#include "busybox.h"
#include "NUM_APPLETS.h"
#include "unicode.h"
#ifndef _POSIX_VDISABLE
# define _POSIX_VDISABLE '\0'
#endif


#ifdef TEST
# define ENABLE_FEATURE_EDITING 0
# define ENABLE_FEATURE_TAB_COMPLETION 0
# define ENABLE_FEATURE_USERNAME_COMPLETION 0
#endif


/* Entire file (except TESTing part) sits inside this #if */
#if ENABLE_FEATURE_EDITING


#define ENABLE_USERNAME_OR_HOMEDIR \
	(ENABLE_FEATURE_USERNAME_COMPLETION || ENABLE_FEATURE_EDITING_FANCY_PROMPT)
#define IF_USERNAME_OR_HOMEDIR(...)
#if ENABLE_USERNAME_OR_HOMEDIR
# undef IF_USERNAME_OR_HOMEDIR
# define IF_USERNAME_OR_HOMEDIR(...) __VA_ARGS__
#endif


#undef CHAR_T
#if ENABLE_UNICODE_SUPPORT
# define BB_NUL ((wchar_t)0)
# define CHAR_T wchar_t
static bool BB_isspace(CHAR_T c) { return ((unsigned)c < 256 && isspace(c)); }
# if ENABLE_FEATURE_EDITING_VI
static bool BB_isalnum_or_underscore(CHAR_T c) {
	return ((unsigned)c < 256 && isalnum(c)) || c == '_';
}
# endif
static bool BB_ispunct(CHAR_T c) { return ((unsigned)c < 256 && ispunct(c)); }
# undef isspace
# undef isalnum
# undef ispunct
# undef isprint
# define isspace isspace_must_not_be_used
# define isalnum isalnum_must_not_be_used
# define ispunct ispunct_must_not_be_used
# define isprint isprint_must_not_be_used
#else
# define BB_NUL '\0'
# define CHAR_T char
# define BB_isspace(c) isspace(c)
# if ENABLE_FEATURE_EDITING_VI
static bool BB_isalnum_or_underscore(CHAR_T c)
{
	return ((unsigned)c < 256 && isalnum(c)) || c == '_';
}
# endif
# define BB_ispunct(c) ispunct(c)
#endif
#if ENABLE_UNICODE_PRESERVE_BROKEN
# define unicode_mark_raw_byte(wc)   ((wc) | 0x20000000)
# define unicode_is_raw_byte(wc)     ((wc) & 0x20000000)
#else
# define unicode_is_raw_byte(wc)     0
#endif


#define ESC "\033"

#define SEQ_CLEAR_TILL_END_OF_SCREEN  ESC"[J"
//#define SEQ_CLEAR_TILL_END_OF_LINE  ESC"[K"


enum {
	MAX_LINELEN = CONFIG_FEATURE_EDITING_MAX_LEN < 0x7ff0
	              ? CONFIG_FEATURE_EDITING_MAX_LEN
	              : 0x7ff0
};

#if ENABLE_USERNAME_OR_HOMEDIR
static const char null_str[] ALIGN1 = "";
#endif

/* We try to minimize both static and stack usage. */
struct lineedit_statics {
	line_input_t *state;

	unsigned cmdedit_termw; /* = 80; */ /* actual terminal width */

	unsigned cmdedit_x;        /* real x (col) terminal position */
	unsigned cmdedit_y;        /* pseudoreal y (row) terminal position */
	unsigned cmdedit_prmt_len; /* on-screen length of last/sole prompt line */

	unsigned cursor;
	int command_len; /* must be signed */
	/* signed maxsize: we want x in "if (x > S.maxsize)"
	 * to _not_ be promoted to unsigned */
	int maxsize;
	CHAR_T *command_ps;

	const char *cmdedit_prompt;
	const char *prompt_last_line;  /* last/sole prompt line */

#if ENABLE_USERNAME_OR_HOMEDIR
	char *user_buf;
	char *home_pwd_buf; /* = (char*)null_str; */
#endif

#if ENABLE_FEATURE_TAB_COMPLETION
	char **matches;
	unsigned num_matches;
#endif

#if ENABLE_FEATURE_EDITING_WINCH
	unsigned SIGWINCH_saved;
	volatile unsigned SIGWINCH_count;
	volatile smallint ok_to_redraw;
#endif

#if ENABLE_FEATURE_EDITING_VI
# define DELBUFSIZ 128
	smallint newdelflag;     /* whether delbuf should be reused yet */
	CHAR_T *delptr;
	CHAR_T delbuf[DELBUFSIZ];  /* a place to store deleted characters */
#endif
#if ENABLE_FEATURE_EDITING_ASK_TERMINAL
	smallint sent_ESC_br6n;
#endif

#if ENABLE_FEATURE_EDITING_WINCH
	/* Largish struct, keeping it last results in smaller code */
	struct sigaction SIGWINCH_handler;
#endif
};

/* See lineedit_ptr_hack.c */
extern struct lineedit_statics *const lineedit_ptr_to_statics;

#define S (*lineedit_ptr_to_statics)
#define state            (S.state           )
#define cmdedit_termw    (S.cmdedit_termw   )
#define cmdedit_x        (S.cmdedit_x       )
#define cmdedit_y        (S.cmdedit_y       )
#define cmdedit_prmt_len (S.cmdedit_prmt_len)
#define cursor           (S.cursor          )
#define command_len      (S.command_len     )
#define command_ps       (S.command_ps      )
#define cmdedit_prompt   (S.cmdedit_prompt  )
#define prompt_last_line (S.prompt_last_line)
#define user_buf         (S.user_buf        )
#define home_pwd_buf     (S.home_pwd_buf    )
#define matches          (S.matches         )
#define num_matches      (S.num_matches     )
#define delptr           (S.delptr          )
#define newdelflag       (S.newdelflag      )
#define delbuf           (S.delbuf          )

#define INIT_S() do { \
	(*(struct lineedit_statics**)&lineedit_ptr_to_statics) = xzalloc(sizeof(S)); \
	barrier(); \
	cmdedit_termw = 80; \
	IF_USERNAME_OR_HOMEDIR(home_pwd_buf = (char*)null_str;) \
	IF_FEATURE_EDITING_VI(delptr = delbuf;) \
} while (0)

static void deinit_S(void)
{
#if ENABLE_FEATURE_EDITING_FANCY_PROMPT
	/* This one is allocated only if FANCY_PROMPT is on
	 * (otherwise it points to verbatim prompt (NOT malloced)) */
	free((char*)cmdedit_prompt);
#endif
#if ENABLE_USERNAME_OR_HOMEDIR
	free(user_buf);
	if (home_pwd_buf != null_str)
		free(home_pwd_buf);
#endif
	free(lineedit_ptr_to_statics);
}
#define DEINIT_S() deinit_S()


#if ENABLE_UNICODE_SUPPORT
static size_t load_string(const char *src)
{
	if (unicode_status == UNICODE_ON) {
		ssize_t len = mbstowcs(command_ps, src, S.maxsize - 1);
		if (len < 0)
			len = 0;
		command_ps[len] = BB_NUL;
		return len;
	} else {
		unsigned i = 0;
		while (src[i] && i < S.maxsize - 1) {
			command_ps[i] = src[i];
			i++;
		}
		command_ps[i] = BB_NUL;
		return i;
	}
}
static unsigned save_string(char *dst, unsigned maxsize)
{
	if (unicode_status == UNICODE_ON) {
# if !ENABLE_UNICODE_PRESERVE_BROKEN
		ssize_t len = wcstombs(dst, command_ps, maxsize - 1);
		if (len < 0)
			len = 0;
		dst[len] = '\0';
		return len;
# else
		unsigned dstpos = 0;
		unsigned srcpos = 0;

		maxsize--;
		while (dstpos < maxsize) {
			wchar_t wc;
			int n = srcpos;

			/* Convert up to 1st invalid byte (or up to end) */
			while ((wc = command_ps[srcpos]) != BB_NUL
			    && !unicode_is_raw_byte(wc)
			) {
				srcpos++;
			}
			command_ps[srcpos] = BB_NUL;
			n = wcstombs(dst + dstpos, command_ps + n, maxsize - dstpos);
			if (n < 0) /* should not happen */
				break;
			dstpos += n;
			if (wc == BB_NUL) /* usually is */
				break;

			/* We do have invalid byte here! */
			command_ps[srcpos] = wc; /* restore it */
			srcpos++;
			if (dstpos == maxsize)
				break;
			dst[dstpos++] = (char) wc;
		}
		dst[dstpos] = '\0';
		return dstpos;
# endif
	} else {
		unsigned i = 0;
		while ((dst[i] = command_ps[i]) != 0)
			i++;
		return i;
	}
}
/* I thought just fputwc(c, stdout) would work. But no... */
static void BB_PUTCHAR(wchar_t c)
{
	if (unicode_status == UNICODE_ON) {
		char buf[MB_CUR_MAX + 1];
		mbstate_t mbst = { 0 };
		ssize_t len = wcrtomb(buf, c, &mbst);
		if (len > 0) {
			buf[len] = '\0';
			fputs(buf, stdout);
		}
	} else {
		/* In this case, c is always one byte */
		putchar(c);
	}
}
# if ENABLE_UNICODE_COMBINING_WCHARS || ENABLE_UNICODE_WIDE_WCHARS
static wchar_t adjust_width_and_validate_wc(unsigned *width_adj, wchar_t wc)
# else
static wchar_t adjust_width_and_validate_wc(wchar_t wc)
#  define adjust_width_and_validate_wc(width_adj, wc) \
	((*(width_adj))++, adjust_width_and_validate_wc(wc))
# endif
{
	int w = 1;

	if (unicode_status == UNICODE_ON) {
		if (wc > CONFIG_LAST_SUPPORTED_WCHAR) {
			/* note: also true for unicode_is_raw_byte(wc) */
			goto subst;
		}
		w = wcwidth(wc);
		if ((ENABLE_UNICODE_COMBINING_WCHARS && w < 0)
		 || (!ENABLE_UNICODE_COMBINING_WCHARS && w <= 0)
		 || (!ENABLE_UNICODE_WIDE_WCHARS && w > 1)
		) {
 subst:
			w = 1;
			wc = CONFIG_SUBST_WCHAR;
		}
	}

# if ENABLE_UNICODE_COMBINING_WCHARS || ENABLE_UNICODE_WIDE_WCHARS
	*width_adj += w;
#endif
	return wc;
}
#else /* !UNICODE */
static size_t load_string(const char *src)
{
	safe_strncpy(command_ps, src, S.maxsize);
	return strlen(command_ps);
}
# if ENABLE_FEATURE_TAB_COMPLETION
static void save_string(char *dst, unsigned maxsize)
{
	safe_strncpy(dst, command_ps, maxsize);
}
# endif
# define BB_PUTCHAR(c) bb_putchar(c)
/* Should never be called: */
int adjust_width_and_validate_wc(unsigned *width_adj, int wc);
#endif


/* Put 'command_ps[cursor]', cursor++.
 * Advance cursor on screen. If we reached right margin, scroll text up
 * and remove terminal margin effect by printing 'next_char' */
#define HACK_FOR_WRONG_WIDTH 1
static void put_cur_glyph_and_inc_cursor(void)
{
	CHAR_T c = command_ps[cursor];
	unsigned width = 0;
	int ofs_to_right;

	if (c == BB_NUL) {
		/* erase character after end of input string */
		c = ' ';
	} else {
		/* advance cursor only if we aren't at the end yet */
		cursor++;
		if (unicode_status == UNICODE_ON) {
			IF_UNICODE_WIDE_WCHARS(width = cmdedit_x;)
			c = adjust_width_and_validate_wc(&cmdedit_x, c);
			IF_UNICODE_WIDE_WCHARS(width = cmdedit_x - width;)
		} else {
			cmdedit_x++;
		}
	}

	ofs_to_right = cmdedit_x - cmdedit_termw;
	if (!ENABLE_UNICODE_WIDE_WCHARS || ofs_to_right <= 0) {
		/* c fits on this line */
		BB_PUTCHAR(c);
	}

	if (ofs_to_right >= 0) {
		/* we go to the next line */
#if HACK_FOR_WRONG_WIDTH
		/* This works better if our idea of term width is wrong
		 * and it is actually wider (often happens on serial lines).
		 * Printing CR,LF *forces* cursor to next line.
		 * OTOH if terminal width is correct AND terminal does NOT
		 * have automargin (IOW: it is moving cursor to next line
		 * by itself (which is wrong for VT-10x terminals)),
		 * this will break things: there will be one extra empty line */
		puts("\r"); /* + implicit '\n' */
#else
		/* VT-10x terminals don't wrap cursor to next line when last char
		 * on the line is printed - cursor stays "over" this char.
		 * Need to print _next_ char too (first one to appear on next line)
		 * to make cursor move down to next line.
		 */
		/* Works ok only if cmdedit_termw is correct. */
		c = command_ps[cursor];
		if (c == BB_NUL)
			c = ' ';
		BB_PUTCHAR(c);
		bb_putchar('\b');
#endif
		cmdedit_y++;
		if (!ENABLE_UNICODE_WIDE_WCHARS || ofs_to_right == 0) {
			width = 0;
		} else { /* ofs_to_right > 0 */
			/* wide char c didn't fit on prev line */
			BB_PUTCHAR(c);
		}
		cmdedit_x = width;
	}
}

/* Move to end of line (by printing all chars till the end) */
static void put_till_end_and_adv_cursor(void)
{
	while (cursor < command_len)
		put_cur_glyph_and_inc_cursor();
}

/* Go to the next line */
static void goto_new_line(void)
{
	put_till_end_and_adv_cursor();
	/* "cursor == 0" is only if prompt is "" and user input is empty */
	if (cursor == 0 || cmdedit_x != 0)
		bb_putchar('\n');
}

static void beep(void)
{
	bb_putchar('\007');
}

/* Full or last/sole prompt line, reset edit cursor, calculate terminal cursor.
 * cmdedit_y is always calculated for the last/sole prompt line.
 */
static void put_prompt_custom(bool is_full)
{
	fputs((is_full ? cmdedit_prompt : prompt_last_line), stdout);
	cursor = 0;
	cmdedit_y = cmdedit_prmt_len / cmdedit_termw; /* new quasireal y */
	cmdedit_x = cmdedit_prmt_len % cmdedit_termw;
}

#define put_prompt_last_line() put_prompt_custom(0)
#define put_prompt()           put_prompt_custom(1)

/* Move back one character */
/* (optimized for slow terminals) */
static void input_backward(unsigned num)
{
	if (num > cursor)
		num = cursor;
	if (num == 0)
		return;
	cursor -= num;

	if ((ENABLE_UNICODE_COMBINING_WCHARS || ENABLE_UNICODE_WIDE_WCHARS)
	 && unicode_status == UNICODE_ON
	) {
		/* correct NUM to be equal to _screen_ width */
		int n = num;
		num = 0;
		while (--n >= 0)
			adjust_width_and_validate_wc(&num, command_ps[cursor + n]);
		if (num == 0)
			return;
	}

	if (cmdedit_x >= num) {
		cmdedit_x -= num;
		if (num <= 4) {
			/* This is longer by 5 bytes on x86.
			 * Also gets miscompiled for ARM users
			 * (busybox.net/bugs/view.php?id=2274).
			 * printf(("\b\b\b\b" + 4) - num);
			 * return;
			 */
			do {
				bb_putchar('\b');
			} while (--num);
			return;
		}
		printf(ESC"[%uD", num);
		return;
	}

	/* Need to go one or more lines up */
	if (ENABLE_UNICODE_WIDE_WCHARS) {
		/* With wide chars, it is hard to "backtrack"
		 * and reliably figure out where to put cursor.
		 * Example (<> is a wide char; # is an ordinary char, _ cursor):
		 * |prompt: <><> |
		 * |<><><><><><> |
		 * |_            |
		 * and user presses left arrow. num = 1, cmdedit_x = 0,
		 * We need to go up one line, and then - how do we know that
		 * we need to go *10* positions to the right? Because
		 * |prompt: <>#<>|
		 * |<><><>#<><><>|
		 * |_            |
		 * in this situation we need to go *11* positions to the right.
		 *
		 * A simpler thing to do is to redraw everything from the start
		 * up to new cursor position (which is already known):
		 */
		unsigned sv_cursor;
		/* go to 1st column; go up to first line */
		printf("\r" ESC"[%uA", cmdedit_y);
		cmdedit_y = 0;
		sv_cursor = cursor;
		put_prompt_last_line(); /* sets cursor to 0 */
		while (cursor < sv_cursor)
			put_cur_glyph_and_inc_cursor();
	} else {
		int lines_up;
		/* num = chars to go back from the beginning of current line: */
		num -= cmdedit_x;
		/* num=1...w: one line up, w+1...2w: two, etc: */
		lines_up = 1 + (num - 1) / cmdedit_termw;
		cmdedit_x = (cmdedit_termw * cmdedit_y - num) % cmdedit_termw;
		cmdedit_y -= lines_up;
		/* go to 1st column; go up */
		printf("\r" ESC"[%uA", lines_up);
		/* go to correct column.
		 * xterm, konsole, Linux VT interpret 0 as 1 below! wow.
		 * need to *make sure* we skip it if cmdedit_x == 0 */
		if (cmdedit_x)
			printf(ESC"[%uC", cmdedit_x);
	}
}

/* See redraw and draw_full below */
static void draw_custom(int y, int back_cursor, bool is_full)
{
	if (y > 0) /* up y lines */
		printf(ESC"[%uA", y);
	bb_putchar('\r');
	put_prompt_custom(is_full);
	put_till_end_and_adv_cursor();
	printf(SEQ_CLEAR_TILL_END_OF_SCREEN);
	input_backward(back_cursor);
}

/* Move y lines up, draw last/sole prompt line, editor line[s], and clear tail.
 * goal: redraw the prompt+input+cursor in-place, overwriting the previous */
#define redraw(y, back_cursor) draw_custom((y), (back_cursor), 0)

/* Like above, but without moving up, and while using all the prompt lines.
 * goal: draw a full prompt+input+cursor unrelated to a previous position.
 * note: cmdedit_y always ends up relating to the last/sole prompt line */
#define draw_full(back_cursor) draw_custom(0, (back_cursor), 1)

/* Delete the char in front of the cursor, optionally saving it
 * for later putback */
#if !ENABLE_FEATURE_EDITING_VI
static void input_delete(void)
#define input_delete(save) input_delete()
#else
static void input_delete(int save)
#endif
{
	int j = cursor;

	if (j == (int)command_len)
		return;

#if ENABLE_FEATURE_EDITING_VI
	if (save) {
		if (newdelflag) {
			delptr = delbuf;
			newdelflag = 0;
		}
		if ((delptr - delbuf) < DELBUFSIZ)
			*delptr++ = command_ps[j];
	}
#endif

	memmove(command_ps + j, command_ps + j + 1,
			/* (command_len + 1 [because of NUL]) - (j + 1)
			 * simplified into (command_len - j) */
			(command_len - j) * sizeof(command_ps[0]));
	command_len--;
	put_till_end_and_adv_cursor();
	/* Last char is still visible, erase it (and more) */
	printf(SEQ_CLEAR_TILL_END_OF_SCREEN);
	input_backward(cursor - j);     /* back to old pos cursor */
}

#if ENABLE_FEATURE_EDITING_VI
static void put(void)
{
	int ocursor;
	int j = delptr - delbuf;

	if (j == 0)
		return;
	ocursor = cursor;
	/* open hole and then fill it */
	memmove(command_ps + cursor + j, command_ps + cursor,
			(command_len - cursor + 1) * sizeof(command_ps[0]));
	memcpy(command_ps + cursor, delbuf, j * sizeof(command_ps[0]));
	command_len += j;
	put_till_end_and_adv_cursor();
	input_backward(cursor - ocursor - j + 1); /* at end of new text */
}
#endif

/* Delete the char in back of the cursor */
static void input_backspace(void)
{
	if (cursor > 0) {
		input_backward(1);
		input_delete(0);
	}
}

/* Move forward one character */
static void input_forward(void)
{
	if (cursor < command_len)
		put_cur_glyph_and_inc_cursor();
}

#if ENABLE_FEATURE_TAB_COMPLETION

//FIXME:
//needs to be more clever: currently it thinks that "foo\ b<TAB>
//matches the file named "foo bar", which is untrue.
//Also, perhaps "foo b<TAB> needs to complete to "foo bar" <cursor>,
//not "foo bar <cursor>...

static void free_tab_completion_data(void)
{
	if (matches) {
		while (num_matches)
			free(matches[--num_matches]);
		free(matches);
		matches = NULL;
	}
}

static void add_match(char *matched)
{
	unsigned char *p = (unsigned char*)matched;
	while (*p) {
		/* ESC attack fix: drop any string with control chars */
		if (*p < ' '
		 || (!ENABLE_UNICODE_SUPPORT && *p >= 0x7f)
		 || (ENABLE_UNICODE_SUPPORT && *p == 0x7f)
		) {
			free(matched);
			return;
		}
		p++;
	}
	matches = xrealloc_vector(matches, 4, num_matches);
	matches[num_matches] = matched;
	num_matches++;
}

# if ENABLE_FEATURE_USERNAME_COMPLETION
/* Replace "~user/..." with "/homedir/...".
 * The parameter is malloced, free it or return it
 * unchanged if no user is matched.
 */
static char *username_path_completion(char *ud)
{
	struct passwd *entry;
	char *tilde_name = ud;
	char *home = NULL;

	ud++; /* skip ~ */
	if (*ud == '/') {       /* "~/..." */
		home = home_pwd_buf;
	} else {
		/* "~user/..." */
		ud = strchr(ud, '/');
		*ud = '\0';           /* "~user" */
		entry = getpwnam(tilde_name + 1);
		*ud = '/';            /* restore "~user/..." */
		if (entry)
			home = entry->pw_dir;
	}
	if (home) {
		ud = concat_path_file(home, ud);
		free(tilde_name);
		tilde_name = ud;
	}
	return tilde_name;
}

/* ~use<tab> - find all users with this prefix.
 * Return the length of the prefix used for matching.
 */
static NOINLINE unsigned complete_username(const char *ud)
{
	struct passwd *pw;
	unsigned userlen;

	ud++; /* skip ~ */
	userlen = strlen(ud);

	setpwent();
	while ((pw = getpwent()) != NULL) {
		/* Null usernames should result in all users as possible completions. */
		if (/* !ud[0] || */ is_prefixed_with(pw->pw_name, ud)) {
			add_match(xasprintf("~%s/", pw->pw_name));
		}
	}
	endpwent(); /* don't keep password file open */

	return 1 + userlen;
}
# endif  /* FEATURE_USERNAME_COMPLETION */

enum {
	FIND_EXE_ONLY = 0,
	FIND_DIR_ONLY = 1,
	FIND_FILE_ONLY = 2,
};

static int path_parse(char ***p)
{
	int npth;
	const char *pth;
	char *tmp;
	char **res;

	if (state->flags & WITH_PATH_LOOKUP)
		pth = state->path_lookup;
	else
		pth = getenv("PATH");

	/* PATH="" or PATH=":"? */
	if (!pth || !pth[0] || LONE_CHAR(pth, ':'))
		return 1;

	tmp = (char*)pth;
	npth = 1; /* path component count */
	while (1) {
		tmp = strchr(tmp, ':');
		if (!tmp)
			break;
		tmp++;
		if (*tmp == '\0')
			break;  /* :<empty> */
		npth++;
	}

	*p = res = xmalloc(npth * sizeof(res[0]));
	res[0] = tmp = xstrdup(pth);
	npth = 1;
	while (1) {
		tmp = strchr(tmp, ':');
		if (!tmp)
			break;
		*tmp++ = '\0'; /* ':' -> '\0' */
		if (*tmp == '\0')
			break; /* :<empty> */
		res[npth++] = tmp;
	}
	return npth;
}

/* Complete command, directory or file name.
 * Return the length of the prefix used for matching.
 */
static NOINLINE unsigned complete_cmd_dir_file(const char *command, int type)
{
	char *path1[1];
	char **paths = path1;
	int npaths;
	int i;
	unsigned pf_len;
	const char *pfind;
	char *dirbuf = NULL;

	npaths = 1;
	path1[0] = (char*)".";

	pfind = strrchr(command, '/');
	if (!pfind) {
		if (type == FIND_EXE_ONLY)
			npaths = path_parse(&paths);
		pfind = command;
	} else {
		/* point to 'l' in "..../last_component" */
		pfind++;
		/* dirbuf = ".../.../.../" */
		dirbuf = xstrndup(command, pfind - command);
# if ENABLE_FEATURE_USERNAME_COMPLETION
		if (dirbuf[0] == '~')   /* ~/... or ~user/... */
			dirbuf = username_path_completion(dirbuf);
# endif
		path1[0] = dirbuf;
	}
	pf_len = strlen(pfind);

# if ENABLE_FEATURE_SH_STANDALONE && NUM_APPLETS != 1
	if (type == FIND_EXE_ONLY && !dirbuf) {
		const char *p = applet_names;

		while (*p) {
			if (strncmp(pfind, p, pf_len) == 0)
				add_match(xstrdup(p));
			while (*p++ != '\0')
				continue;
		}
	}
# endif

	for (i = 0; i < npaths; i++) {
		DIR *dir;
		struct dirent *next;
		struct stat st;
		char *found;

		dir = opendir(paths[i]);
		if (!dir)
			continue; /* don't print an error */

		while ((next = readdir(dir)) != NULL) {
			unsigned len;
			const char *name_found = next->d_name;

			/* .../<tab>: bash 3.2.0 shows dotfiles, but not . and .. */
			if (!pfind[0] && DOT_OR_DOTDOT(name_found))
				continue;
			/* match? */
			if (!is_prefixed_with(name_found, pfind))
				continue; /* no */

			found = concat_path_file(paths[i], name_found);
			/* NB: stat() first so that we see is it a directory;
			 * but if that fails, use lstat() so that
			 * we still match dangling links */
			if (stat(found, &st) && lstat(found, &st))
				goto cont; /* hmm, remove in progress? */

			/* Save only name */
			len = strlen(name_found);
			found = xrealloc(found, len + 2); /* +2: for slash and NUL */
			strcpy(found, name_found);

			if (S_ISDIR(st.st_mode)) {
				/* name is a directory, add slash */
				found[len] = '/';
				found[len + 1] = '\0';
			} else {
				/* skip files if looking for dirs only (example: cd) */
				if (type == FIND_DIR_ONLY)
					goto cont;
			}
			/* add it to the list */
			add_match(found);
			continue;
 cont:
			free(found);
		}
		closedir(dir);
	} /* for every path */

	if (paths != path1) {
		free(paths[0]); /* allocated memory is only in first member */
		free(paths);
	}
	free(dirbuf);

	return pf_len;
}

/* build_match_prefix:
 * On entry, match_buf contains everything up to cursor at the moment <tab>
 * was pressed. This function looks at it, figures out what part of it
 * constitutes the command/file/directory prefix to use for completion,
 * and rewrites match_buf to contain only that part.
 */
#define dbg_bmp 0
/* Helpers: */
/* QUOT is used on elements of int_buf[], which are bytes,
 * not Unicode chars. Therefore it works correctly even in Unicode mode.
 */
#define QUOT (UCHAR_MAX+1)
static void remove_chunk(int16_t *int_buf, int beg, int end)
{
	/* beg must be <= end */
	if (beg == end)
		return;

	while ((int_buf[beg] = int_buf[end]) != 0)
		beg++, end++;

	if (dbg_bmp) {
		int i;
		for (i = 0; int_buf[i]; i++)
			bb_putchar((unsigned char)int_buf[i]);
		bb_putchar('\n');
	}
}
/* Caller ensures that match_buf points to a malloced buffer
 * big enough to hold strlen(match_buf)*2 + 2
 */
static NOINLINE int build_match_prefix(char *match_buf)
{
	int i, j;
	int command_mode;
	int16_t *int_buf = (int16_t*)match_buf;

	if (dbg_bmp) printf("\n%s\n", match_buf);

	/* Copy in reverse order, since they overlap */
	i = strlen(match_buf);
	do {
		int_buf[i] = (unsigned char)match_buf[i];
		i--;
	} while (i >= 0);

	/* Mark every \c as "quoted c" */
	for (i = 0; int_buf[i]; i++) {
		if (int_buf[i] == '\\') {
			remove_chunk(int_buf, i, i + 1);
			int_buf[i] |= QUOT;
		}
	}
	/* Quote-mark "chars" and 'chars', drop delimiters */
	{
		int in_quote = 0;
		i = 0;
		while (int_buf[i]) {
			int cur = int_buf[i];
			if (!cur)
				break;
			if (cur == '\'' || cur == '"') {
				if (!in_quote || (cur == in_quote)) {
					in_quote ^= cur;
					remove_chunk(int_buf, i, i + 1);
					continue;
				}
			}
			if (in_quote)
				int_buf[i] = cur | QUOT;
			i++;
		}
	}

	/* Remove everything up to command delimiters:
	 * ';' ';;' '&' '|' '&&' '||',
	 * but careful with '>&' '<&' '>|'
	 */
	for (i = 0; int_buf[i]; i++) {
		int cur = int_buf[i];
		if (cur == ';' || cur == '&' || cur == '|') {
			int prev = i ? int_buf[i - 1] : 0;
			if (cur == '&' && (prev == '>' || prev == '<')) {
				continue;
			} else if (cur == '|' && prev == '>') {
				continue;
			}
			remove_chunk(int_buf, 0, i + 1 + (cur == int_buf[i + 1]));
			i = -1;  /* back to square 1 */
		}
	}
	/* Remove all `cmd` */
	for (i = 0; int_buf[i]; i++) {
		if (int_buf[i] == '`') {
			for (j = i + 1; int_buf[j]; j++) {
				if (int_buf[j] == '`') {
					/* `cmd` should count as a word:
					 * `cmd` c<tab> should search for files c*,
					 * not commands c*. Therefore we don't drop
					 * `cmd` entirely, we replace it with single `.
					 */
					remove_chunk(int_buf, i, j);
					goto next;
				}
			}
			/* No closing ` - command mode, remove all up to ` */
			remove_chunk(int_buf, 0, i + 1);
			break;
 next: ;
		}
	}

	/* Remove "cmd (" and "cmd {"
	 * Example: "if { c<tab>"
	 * In this example, c should be matched as command pfx.
	 */
	for (i = 0; int_buf[i]; i++) {
		if (int_buf[i] == '(' || int_buf[i] == '{') {
			remove_chunk(int_buf, 0, i + 1);
			i = -1;  /* back to square 1 */
		}
	}

	/* Remove leading unquoted spaces */
	for (i = 0; int_buf[i]; i++)
		if (int_buf[i] != ' ')
			break;
	remove_chunk(int_buf, 0, i);

	/* Determine completion mode */
	command_mode = FIND_EXE_ONLY;
	for (i = 0; int_buf[i]; i++) {
		if (int_buf[i] == ' ' || int_buf[i] == '<' || int_buf[i] == '>') {
			if (int_buf[i] == ' '
			 && command_mode == FIND_EXE_ONLY
			 && (char)int_buf[0] == 'c'
			 && (char)int_buf[1] == 'd'
			 && i == 2 /* -> int_buf[2] == ' ' */
			) {
				command_mode = FIND_DIR_ONLY;
			} else {
				command_mode = FIND_FILE_ONLY;
				break;
			}
		}
	}
	if (dbg_bmp) printf("command_mode(0:exe/1:dir/2:file):%d\n", command_mode);

	/* Remove everything except last word */
	for (i = 0; int_buf[i]; i++) /* quasi-strlen(int_buf) */
		continue;
	for (--i; i >= 0; i--) {
		int cur = int_buf[i];
		if (cur == ' ' || cur == '<' || cur == '>' || cur == '|' || cur == '&') {
			remove_chunk(int_buf, 0, i + 1);
			break;
		}
	}

	/* Convert back to string of _chars_ */
	i = 0;
	while ((match_buf[i] = int_buf[i]) != '\0')
		i++;

	if (dbg_bmp) printf("final match_buf:'%s'\n", match_buf);

	return command_mode;
}

/*
 * Display by column (original idea from ls applet,
 * very optimized by me [Vladimir] :)
 */
static void showfiles(void)
{
	int ncols, row;
	int column_width = 0;
	int nfiles = num_matches;
	int nrows = nfiles;
	int l;

	/* find the longest file name - use that as the column width */
	for (row = 0; row < nrows; row++) {
		l = unicode_strwidth(matches[row]);
		if (column_width < l)
			column_width = l;
	}
	column_width += 2;              /* min space for columns */
	ncols = cmdedit_termw / column_width;

	if (ncols > 1) {
		nrows /= ncols;
		if (nfiles % ncols)
			nrows++;        /* round up fractionals */
	} else {
		ncols = 1;
	}
	for (row = 0; row < nrows; row++) {
		int n = row;
		int nc;

		for (nc = 1; nc < ncols && n+nrows < nfiles; n += nrows, nc++) {
			printf("%s%-*s", matches[n],
				(int)(column_width - unicode_strwidth(matches[n])), ""
			);
		}
		if (ENABLE_UNICODE_SUPPORT)
			puts(printable_string(NULL, matches[n]));
		else
			puts(matches[n]);
	}
}

static const char *is_special_char(char c)
{
	return strchr(" `\"#$%^&*()=+{}[]:;'|\\<>", c);
}

static char *quote_special_chars(char *found)
{
	int l = 0;
	char *s = xzalloc((strlen(found) + 1) * 2);

	while (*found) {
		if (is_special_char(*found))
			s[l++] = '\\';
		s[l++] = *found++;
	}
	/* s[l] = '\0'; - already is */
	return s;
}

/* Do TAB completion */
static NOINLINE void input_tab(smallint *lastWasTab)
{
	char *chosen_match;
	char *match_buf;
	size_t len_found;
	/* Length of string used for matching */
	unsigned match_pfx_len = match_pfx_len;
	int find_type;
# if ENABLE_UNICODE_SUPPORT
	/* cursor pos in command converted to multibyte form */
	int cursor_mb;
# endif
	if (!(state->flags & TAB_COMPLETION))
		return;

	if (*lastWasTab) {
		/* The last char was a TAB too.
		 * Print a list of all the available choices.
		 */
		if (num_matches > 0) {
			/* cursor will be changed by goto_new_line() */
			int sav_cursor = cursor;
			goto_new_line();
			showfiles();
			draw_full(command_len - sav_cursor);
		}
		return;
	}

	*lastWasTab = 1;
	chosen_match = NULL;

	/* Make a local copy of the string up to the position of the cursor.
	 * build_match_prefix will expand it into int16_t's, need to allocate
	 * twice as much as the string_len+1.
	 * (we then also (ab)use this extra space later - see (**))
	 */
	match_buf = xmalloc(MAX_LINELEN * sizeof(int16_t));
# if !ENABLE_UNICODE_SUPPORT
	save_string(match_buf, cursor + 1); /* +1 for NUL */
# else
	{
		CHAR_T wc = command_ps[cursor];
		command_ps[cursor] = BB_NUL;
		save_string(match_buf, MAX_LINELEN);
		command_ps[cursor] = wc;
		cursor_mb = strlen(match_buf);
	}
# endif
	find_type = build_match_prefix(match_buf);

	/* Free up any memory already allocated */
	free_tab_completion_data();

# if ENABLE_FEATURE_USERNAME_COMPLETION
	/* If the word starts with ~ and there is no slash in the word,
	 * then try completing this word as a username. */
	if (state->flags & USERNAME_COMPLETION)
		if (match_buf[0] == '~' && strchr(match_buf, '/') == NULL)
			match_pfx_len = complete_username(match_buf);
# endif
	/* If complete_username() did not match,
	 * try to match a command in $PATH, or a directory, or a file */
	if (!matches)
		match_pfx_len = complete_cmd_dir_file(match_buf, find_type);

	/* Account for backslashes which will be inserted
	 * by quote_special_chars() later */
	{
		const char *e = match_buf + strlen(match_buf);
		const char *s = e - match_pfx_len;
		while (s < e)
			if (is_special_char(*s++))
				match_pfx_len++;
	}

	/* Remove duplicates */
	if (matches) {
		unsigned i, n = 0;
		qsort_string_vector(matches, num_matches);
		for (i = 0; i < num_matches - 1; ++i) {
			//if (matches[i] && matches[i+1]) { /* paranoia */
				if (strcmp(matches[i], matches[i+1]) == 0) {
					free(matches[i]);
					//matches[i] = NULL; /* paranoia */
				} else {
					matches[n++] = matches[i];
				}
			//}
		}
		matches[n++] = matches[i];
		num_matches = n;
	}

	/* Did we find exactly one match? */
	if (num_matches != 1) { /* no */
		char *cp;
		beep();
		if (!matches)
			goto ret; /* no matches at all */
		/* Find common prefix */
		chosen_match = xstrdup(matches[0]);
		for (cp = chosen_match; *cp; cp++) {
			unsigned n;
			for (n = 1; n < num_matches; n++) {
				if (matches[n][cp - chosen_match] != *cp) {
					goto stop;
				}
			}
		}
 stop:
		if (cp == chosen_match) { /* have unique prefix? */
			goto ret; /* no */
		}
		*cp = '\0';
		cp = quote_special_chars(chosen_match);
		free(chosen_match);
		chosen_match = cp;
		len_found = strlen(chosen_match);
	} else {                        /* exactly one match */
		/* Next <tab> is not a double-tab */
		*lastWasTab = 0;

		chosen_match = quote_special_chars(matches[0]);
		len_found = strlen(chosen_match);
		if (chosen_match[len_found-1] != '/') {
			chosen_match[len_found] = ' ';
			chosen_match[++len_found] = '\0';
		}
	}

# if !ENABLE_UNICODE_SUPPORT
	/* Have space to place the match? */
	/* The result consists of three parts with these lengths: */
	/* cursor + (len_found - match_pfx_len) + (command_len - cursor) */
	/* it simplifies into: */
	if ((int)(len_found - match_pfx_len + command_len) < S.maxsize) {
		int pos;
		/* save tail */
		strcpy(match_buf, &command_ps[cursor]);
		/* add match and tail */
		sprintf(&command_ps[cursor], "%s%s", chosen_match + match_pfx_len, match_buf);
		command_len = strlen(command_ps);
		/* new pos */
		pos = cursor + len_found - match_pfx_len;
		/* write out the matched command */
		redraw(cmdedit_y, command_len - pos);
	}
# else
	{
		/* Use 2nd half of match_buf as scratch space - see (**) */
		char *command = match_buf + MAX_LINELEN;
		int len = save_string(command, MAX_LINELEN);
		/* Have space to place the match? */
		/* cursor_mb + (len_found - match_pfx_len) + (len - cursor_mb) */
		if ((int)(len_found - match_pfx_len + len) < MAX_LINELEN) {
			int pos;
			/* save tail */
			strcpy(match_buf, &command[cursor_mb]);
			/* where do we want to have cursor after all? */
			strcpy(&command[cursor_mb], chosen_match + match_pfx_len);
			len = load_string(command);
			/* add match and tail */
			sprintf(&command[cursor_mb], "%s%s", chosen_match + match_pfx_len, match_buf);
			command_len = load_string(command);
			/* write out the matched command */
			/* paranoia: load_string can return 0 on conv error,
			 * prevent passing pos = (0 - 12) to redraw */
			pos = command_len - len;
			redraw(cmdedit_y, pos >= 0 ? pos : 0);
		}
	}
# endif
 ret:
	free(chosen_match);
	free(match_buf);
}

#endif  /* FEATURE_TAB_COMPLETION */


line_input_t* FAST_FUNC new_line_input_t(int flags)
{
	line_input_t *n = xzalloc(sizeof(*n));
	n->flags = flags;
	n->timeout = -1;
#if MAX_HISTORY > 0
	n->max_history = MAX_HISTORY;
#endif
	return n;
}


#if MAX_HISTORY > 0

unsigned FAST_FUNC size_from_HISTFILESIZE(const char *hp)
{
	int size = MAX_HISTORY;
	if (hp) {
		size = atoi(hp);
		if (size <= 0)
			return 1;
		if (size > MAX_HISTORY)
			return MAX_HISTORY;
	}
	return size;
}

static void save_command_ps_at_cur_history(void)
{
	if (command_ps[0] != BB_NUL) {
		int cur = state->cur_history;
		free(state->history[cur]);

# if ENABLE_UNICODE_SUPPORT
		{
			char tbuf[MAX_LINELEN];
			save_string(tbuf, sizeof(tbuf));
			state->history[cur] = xstrdup(tbuf);
		}
# else
		state->history[cur] = xstrdup(command_ps);
# endif
	}
}

/* state->flags is already checked to be nonzero */
static int get_previous_history(void)
{
	if ((state->flags & DO_HISTORY) && state->cur_history) {
		save_command_ps_at_cur_history();
		state->cur_history--;
		return 1;
	}
	beep();
	return 0;
}

static int get_next_history(void)
{
	if (state->flags & DO_HISTORY) {
		if (state->cur_history < state->cnt_history) {
			save_command_ps_at_cur_history(); /* save the current history line */
			return ++state->cur_history;
		}
	}
	beep();
	return 0;
}

/* Lists command history. Used by shell 'history' builtins */
void FAST_FUNC show_history(const line_input_t *st)
{
	int i;

	if (!st)
		return;
	for (i = 0; i < st->cnt_history; i++)
		printf("%4d %s\n", i, st->history[i]);
}

# if ENABLE_FEATURE_EDITING_SAVEHISTORY
/* We try to ensure that concurrent additions to the history
 * do not overwrite each other.
 * Otherwise shell users get unhappy.
 *
 * History file is trimmed lazily, when it grows several times longer
 * than configured MAX_HISTORY lines.
 */

static void free_line_input_t(line_input_t *n)
{
	int i = n->cnt_history;
	while (i > 0)
		free(n->history[--i]);
	free(n);
}

/* state->flags is already checked to be nonzero */
static void load_history(line_input_t *st_parm)
{
	char *temp_h[MAX_HISTORY];
	char *line;
	FILE *fp;
	unsigned idx, i, line_len;

	/* NB: do not trash old history if file can't be opened */

	fp = fopen_for_read(st_parm->hist_file);
	if (fp) {
		/* clean up old history */
		for (idx = st_parm->cnt_history; idx > 0;) {
			idx--;
			free(st_parm->history[idx]);
			st_parm->history[idx] = NULL;
		}

		/* fill temp_h[], retaining only last MAX_HISTORY lines */
		memset(temp_h, 0, sizeof(temp_h));
		idx = 0;
		st_parm->cnt_history_in_file = 0;
		while ((line = xmalloc_fgetline(fp)) != NULL) {
			if (line[0] == '\0') {
				free(line);
				continue;
			}
			free(temp_h[idx]);
			temp_h[idx] = line;
			st_parm->cnt_history_in_file++;
			idx++;
			if (idx == st_parm->max_history)
				idx = 0;
		}
		fclose(fp);

		/* find first non-NULL temp_h[], if any */
		if (st_parm->cnt_history_in_file) {
			while (temp_h[idx] == NULL) {
				idx++;
				if (idx == st_parm->max_history)
					idx = 0;
			}
		}

		/* copy temp_h[] to st_parm->history[] */
		for (i = 0; i < st_parm->max_history;) {
			line = temp_h[idx];
			if (!line)
				break;
			idx++;
			if (idx == st_parm->max_history)
				idx = 0;
			line_len = strlen(line);
			if (line_len >= MAX_LINELEN)
				line[MAX_LINELEN-1] = '\0';
			st_parm->history[i++] = line;
		}
		st_parm->cnt_history = i;
		if (ENABLE_FEATURE_EDITING_SAVE_ON_EXIT)
			st_parm->cnt_history_in_file = i;
	}
}

#  if ENABLE_FEATURE_EDITING_SAVE_ON_EXIT
void save_history(line_input_t *st)
{
	FILE *fp;

	if (!st->hist_file)
		return;
	if (st->cnt_history <= st->cnt_history_in_file)
		return;

	fp = fopen(st->hist_file, "a");
	if (fp) {
		int i, fd;
		char *new_name;
		line_input_t *st_temp;

		for (i = st->cnt_history_in_file; i < st->cnt_history; i++)
			fprintf(fp, "%s\n", st->history[i]);
		fclose(fp);

		/* we may have concurrently written entries from others.
		 * load them */
		st_temp = new_line_input_t(st->flags);
		st_temp->hist_file = st->hist_file;
		st_temp->max_history = st->max_history;
		load_history(st_temp);

		/* write out temp file and replace hist_file atomically */
		new_name = xasprintf("%s.%u.new", st->hist_file, (int) getpid());
		fd = open(new_name, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd >= 0) {
			fp = xfdopen_for_write(fd);
			for (i = 0; i < st_temp->cnt_history; i++)
				fprintf(fp, "%s\n", st_temp->history[i]);
			fclose(fp);
			if (rename(new_name, st->hist_file) == 0)
				st->cnt_history_in_file = st_temp->cnt_history;
		}
		free(new_name);
		free_line_input_t(st_temp);
	}
}
#  else
static void save_history(char *str)
{
	int fd;
	int len, len2;

	if (!state->hist_file)
		return;

	fd = open(state->hist_file, O_WRONLY | O_CREAT | O_APPEND, 0600);
	if (fd < 0)
		return;
	xlseek(fd, 0, SEEK_END); /* paranoia */
	len = strlen(str);
	str[len] = '\n'; /* we (try to) do atomic write */
	len2 = full_write(fd, str, len + 1);
	str[len] = '\0';
	close(fd);
	if (len2 != len + 1)
		return; /* "wtf?" */

	/* did we write so much that history file needs trimming? */
	state->cnt_history_in_file++;
	if (state->cnt_history_in_file > state->max_history * 4) {
		char *new_name;
		line_input_t *st_temp;

		/* we may have concurrently written entries from others.
		 * load them */
		st_temp = new_line_input_t(state->flags);
		st_temp->hist_file = state->hist_file;
		st_temp->max_history = state->max_history;
		load_history(st_temp);

		/* write out temp file and replace hist_file atomically */
		new_name = xasprintf("%s.%u.new", state->hist_file, (int) getpid());
		fd = open(new_name, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd >= 0) {
			FILE *fp;
			int i;

			fp = xfdopen_for_write(fd);
			for (i = 0; i < st_temp->cnt_history; i++)
				fprintf(fp, "%s\n", st_temp->history[i]);
			fclose(fp);
			if (rename(new_name, state->hist_file) == 0)
				state->cnt_history_in_file = st_temp->cnt_history;
		}
		free(new_name);
		free_line_input_t(st_temp);
	}
}
#  endif
# else
#  define load_history(a) ((void)0)
#  define save_history(a) ((void)0)
# endif /* FEATURE_COMMAND_SAVEHISTORY */

static void remember_in_history(char *str)
{
	int i;

	if (!(state->flags & DO_HISTORY))
		return;
	if (str[0] == '\0')
		return;
	i = state->cnt_history;
	/* Don't save dupes */
	if (i && strcmp(state->history[i-1], str) == 0)
		return;

	free(state->history[state->max_history]); /* redundant, paranoia */
	state->history[state->max_history] = NULL; /* redundant, paranoia */

	/* If history[] is full, remove the oldest command */
	/* we need to keep history[state->max_history] empty, hence >=, not > */
	if (i >= state->max_history) {
		free(state->history[0]);
		for (i = 0; i < state->max_history-1; i++)
			state->history[i] = state->history[i+1];
		/* i == state->max_history-1 */
# if ENABLE_FEATURE_EDITING_SAVE_ON_EXIT
		if (state->cnt_history_in_file)
			state->cnt_history_in_file--;
# endif
	}
	/* i <= state->max_history-1 */
	state->history[i++] = xstrdup(str);
	/* i <= state->max_history */
	state->cur_history = i;
	state->cnt_history = i;
# if ENABLE_FEATURE_EDITING_SAVEHISTORY && !ENABLE_FEATURE_EDITING_SAVE_ON_EXIT
	save_history(str);
# endif
}

#else /* MAX_HISTORY == 0 */
# define remember_in_history(a) ((void)0)
#endif /* MAX_HISTORY */


#if ENABLE_FEATURE_EDITING_VI
/*
 * vi mode implemented 2005 by Paul Fox <pgf@foxharp.boston.ma.us>
 */
static void
vi_Word_motion(int eat)
{
	CHAR_T *command = command_ps;

	while (cursor < command_len && !BB_isspace(command[cursor]))
		input_forward();
	if (eat) while (cursor < command_len && BB_isspace(command[cursor]))
		input_forward();
}

static void
vi_word_motion(int eat)
{
	CHAR_T *command = command_ps;

	if (BB_isalnum_or_underscore(command[cursor])) {
		while (cursor < command_len
		 && (BB_isalnum_or_underscore(command[cursor+1]))
		) {
			input_forward();
		}
	} else if (BB_ispunct(command[cursor])) {
		while (cursor < command_len && BB_ispunct(command[cursor+1]))
			input_forward();
	}

	if (cursor < command_len)
		input_forward();

	if (eat) {
		while (cursor < command_len && BB_isspace(command[cursor]))
			input_forward();
	}
}

static void
vi_End_motion(void)
{
	CHAR_T *command = command_ps;

	input_forward();
	while (cursor < command_len && BB_isspace(command[cursor]))
		input_forward();
	while (cursor < command_len-1 && !BB_isspace(command[cursor+1]))
		input_forward();
}

static void
vi_end_motion(void)
{
	CHAR_T *command = command_ps;

	if (cursor >= command_len-1)
		return;
	input_forward();
	while (cursor < command_len-1 && BB_isspace(command[cursor]))
		input_forward();
	if (cursor >= command_len-1)
		return;
	if (BB_isalnum_or_underscore(command[cursor])) {
		while (cursor < command_len-1
		 && (BB_isalnum_or_underscore(command[cursor+1]))
		) {
			input_forward();
		}
	} else if (BB_ispunct(command[cursor])) {
		while (cursor < command_len-1 && BB_ispunct(command[cursor+1]))
			input_forward();
	}
}

static void
vi_Back_motion(void)
{
	CHAR_T *command = command_ps;

	while (cursor > 0 && BB_isspace(command[cursor-1]))
		input_backward(1);
	while (cursor > 0 && !BB_isspace(command[cursor-1]))
		input_backward(1);
}

static void
vi_back_motion(void)
{
	CHAR_T *command = command_ps;

	if (cursor <= 0)
		return;
	input_backward(1);
	while (cursor > 0 && BB_isspace(command[cursor]))
		input_backward(1);
	if (cursor <= 0)
		return;
	if (BB_isalnum_or_underscore(command[cursor])) {
		while (cursor > 0
		 && (BB_isalnum_or_underscore(command[cursor-1]))
		) {
			input_backward(1);
		}
	} else if (BB_ispunct(command[cursor])) {
		while (cursor > 0 && BB_ispunct(command[cursor-1]))
			input_backward(1);
	}
}
#endif

/* Modelled after bash 4.0 behavior of Ctrl-<arrow> */
static void ctrl_left(void)
{
	CHAR_T *command = command_ps;

	while (1) {
		CHAR_T c;

		input_backward(1);
		if (cursor == 0)
			break;
		c = command[cursor];
		if (c != ' ' && !BB_ispunct(c)) {
			/* we reached a "word" delimited by spaces/punct.
			 * go to its beginning */
			while (1) {
				c = command[cursor - 1];
				if (c == ' ' || BB_ispunct(c))
					break;
				input_backward(1);
				if (cursor == 0)
					break;
			}
			break;
		}
	}
}
static void ctrl_right(void)
{
	CHAR_T *command = command_ps;

	while (1) {
		CHAR_T c;

		c = command[cursor];
		if (c == BB_NUL)
			break;
		if (c != ' ' && !BB_ispunct(c)) {
			/* we reached a "word" delimited by spaces/punct.
			 * go to its end + 1 */
			while (1) {
				input_forward();
				c = command[cursor];
				if (c == BB_NUL || c == ' ' || BB_ispunct(c))
					break;
			}
			break;
		}
		input_forward();
	}
}


/*
 * read_line_input and its helpers
 */

#if ENABLE_FEATURE_EDITING_ASK_TERMINAL
static void ask_terminal(void)
{
	/* Ask terminal where is the cursor now.
	 * lineedit_read_key handles response and corrects
	 * our idea of current cursor position.
	 * Testcase: run "echo -n long_line_long_line_long_line",
	 * then type in a long, wrapping command and try to
	 * delete it using backspace key.
	 * Note: we print it _after_ prompt, because
	 * prompt may contain CR. Example: PS1='\[\r\n\]\w '
	 */
	/* Problem: if there is buffered input on stdin,
	 * the response will be delivered later,
	 * possibly to an unsuspecting application.
	 * Testcase: "sleep 1; busybox ash" + press and hold [Enter].
	 * Result:
	 * ~/srcdevel/bbox/fix/busybox.t4 #
	 * ~/srcdevel/bbox/fix/busybox.t4 #
	 * ^[[59;34~/srcdevel/bbox/fix/busybox.t4 #  <-- garbage
	 * ~/srcdevel/bbox/fix/busybox.t4 #
	 *
	 * Checking for input with poll only makes the race narrower,
	 * I still can trigger it. Strace:
	 *
	 * write(1, "~/srcdevel/bbox/fix/busybox.t4 # ", 33) = 33
	 * poll([{fd=0, events=POLLIN}], 1, 0) = 0 (Timeout)  <-- no input exists
	 * write(1, "\33[6n", 4) = 4  <-- send the ESC sequence, quick!
	 * poll([{fd=0, events=POLLIN}], 1, -1) = 1 ([{fd=0, revents=POLLIN}])
	 * read(0, "\n", 1)      = 1  <-- oh crap, user's input got in first
	 */
	struct pollfd pfd;

	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;
	if (safe_poll(&pfd, 1, 0) == 0) {
		S.sent_ESC_br6n = 1;
		fputs(ESC"[6n", stdout);
		fflush_all(); /* make terminal see it ASAP! */
	}
}
#else
#define ask_terminal() ((void)0)
#endif

/* Note about multi-line PS1 (e.g. "\n\w \u@\h\n> ") and prompt redrawing:
 *
 * If the prompt has any newlines, after we print it once we use only its last
 * line to redraw in-place, which makes it simpler to calculate how many lines
 * we should move the cursor up to align the redraw (cmdedit_y). The earlier
 * prompt lines just stay on screen and we redraw below them.
 *
 * Use cases for all prompt lines beyond the initial draw:
 * - After clear-screen (^L) or after displaying tab-completion choices, we
 *   print the full prompt, as it isn't redrawn in-place.
 * - During terminal resize we could try to redraw all lines, but we don't,
 *   because it requires delicate alignment, it's good enough with only the
 *   last line, and doing it wrong is arguably worse than not doing it at all.
 *
 * Terminology wise, if it doesn't mention "full", then it means the last/sole
 * prompt line. We use the prompt (last/sole line) while redrawing in-place,
 * and the full where we need a fresh one unrelated to an earlier position.
 *
 * If PS1 is not multiline, the last/sole line and the full are the same string.
 */

/* Called just once at read_line_input() init time */
#if !ENABLE_FEATURE_EDITING_FANCY_PROMPT
static void parse_and_put_prompt(const char *prmt_ptr)
{
	const char *p;
	cmdedit_prompt = prompt_last_line = prmt_ptr;
	p = strrchr(prmt_ptr, '\n');
	if (p)
		prompt_last_line = p + 1;
	cmdedit_prmt_len = unicode_strwidth(prompt_last_line);
	put_prompt();
}
#else
static void parse_and_put_prompt(const char *prmt_ptr)
{
	int prmt_size = 0;
	char *prmt_mem_ptr = xzalloc(1);
# if ENABLE_USERNAME_OR_HOMEDIR
	char *cwd_buf = NULL;
# endif
	char flg_not_length = '[';
	char cbuf[2];

	/*cmdedit_prmt_len = 0; - already is */

	cbuf[1] = '\0'; /* never changes */

	while (*prmt_ptr) {
		char timebuf[sizeof("HH:MM:SS")];
		char *free_me = NULL;
		char *pbuf;
		char c;

		pbuf = cbuf;
		c = *prmt_ptr++;
		if (c == '\\') {
			const char *cp;
			int l;
/*
 * Supported via bb_process_escape_sequence:
 * \a	ASCII bell character (07)
 * \e	ASCII escape character (033)
 * \n	newline
 * \r	carriage return
 * \\	backslash
 * \nnn	char with octal code nnn
 * Supported:
 * \$	if the effective UID is 0, a #, otherwise a $
 * \w	current working directory, with $HOME abbreviated with a tilde
 *	Note: we do not support $PROMPT_DIRTRIM=n feature
 * \W	basename of the current working directory, with $HOME abbreviated with a tilde
 * \h	hostname up to the first '.'
 * \H	hostname
 * \u	username
 * \[	begin a sequence of non-printing characters
 * \]	end a sequence of non-printing characters
 * \T	current time in 12-hour HH:MM:SS format
 * \@	current time in 12-hour am/pm format
 * \A	current time in 24-hour HH:MM format
 * \t	current time in 24-hour HH:MM:SS format
 *	(all of the above work as \A)
 * Not supported:
 * \!	history number of this command
 * \#	command number of this command
 * \j	number of jobs currently managed by the shell
 * \l	basename of the shell's terminal device name
 * \s	name of the shell, the basename of $0 (the portion following the final slash)
 * \V	release of bash, version + patch level (e.g., 2.00.0)
 * \d	date in "Weekday Month Date" format (e.g., "Tue May 26")
 * \D{format}
 *	format is passed to strftime(3).
 *	An empty format results in a locale-specific time representation.
 *	The braces are required.
 * Mishandled by bb_process_escape_sequence:
 * \v	version of bash (e.g., 2.00)
 */
			cp = prmt_ptr;
			c = *cp;
			if (c != 't') /* don't treat \t as tab */
				c = bb_process_escape_sequence(&prmt_ptr);
			if (prmt_ptr == cp) {
				if (*cp == '\0')
					break;
				c = *prmt_ptr++;

				switch (c) {
# if ENABLE_USERNAME_OR_HOMEDIR
				case 'u':
					pbuf = user_buf ? user_buf : (char*)"";
					break;
# endif
				case 'H':
				case 'h':
					pbuf = free_me = safe_gethostname();
					if (c == 'h')
						strchrnul(pbuf, '.')[0] = '\0';
					break;
				case '$':
					c = (geteuid() == 0 ? '#' : '$');
					break;
				case 'T': /* 12-hour HH:MM:SS format */
				case '@': /* 12-hour am/pm format */
				case 'A': /* 24-hour HH:MM format */
				case 't': /* 24-hour HH:MM:SS format */
					/* We show all of them as 24-hour HH:MM */
					strftime_HHMMSS(timebuf, sizeof(timebuf), NULL)[-3] = '\0';
					pbuf = timebuf;
					break;
# if ENABLE_USERNAME_OR_HOMEDIR
				case 'w': /* current dir */
				case 'W': /* basename of cur dir */
					if (!cwd_buf) {
						cwd_buf = xrealloc_getcwd_or_warn(NULL);
						if (!cwd_buf)
							cwd_buf = (char *)bb_msg_unknown;
						else if (home_pwd_buf[0]) {
							char *after_home_user;

							/* /home/user[/something] -> ~[/something] */
							after_home_user = is_prefixed_with(cwd_buf, home_pwd_buf);
							if (after_home_user
							 && (*after_home_user == '/' || *after_home_user == '\0')
							) {
								cwd_buf[0] = '~';
								overlapping_strcpy(cwd_buf + 1, after_home_user);
							}
						}
					}
					pbuf = cwd_buf;
					if (c == 'w')
						break;
					cp = strrchr(pbuf, '/');
					if (cp)
						pbuf = (char*)cp + 1;
					break;
# endif
// bb_process_escape_sequence does this now:
//				case 'e': case 'E':     /* \e \E = \033 */
//					c = '\033';
//					break;
				case 'x': case 'X': {
					char buf2[4];
					for (l = 0; l < 3;) {
						unsigned h;
						buf2[l++] = *prmt_ptr;
						buf2[l] = '\0';
						h = strtoul(buf2, &pbuf, 16);
						if (h > UCHAR_MAX || (pbuf - buf2) < l) {
							buf2[--l] = '\0';
							break;
						}
						prmt_ptr++;
					}
					c = (char)strtoul(buf2, NULL, 16);
					if (c == 0)
						c = '?';
					pbuf = cbuf;
					break;
				}
				case '[': case ']':
					if (c == flg_not_length) {
						/* Toggle '['/']' hex 5b/5d */
						flg_not_length ^= 6;
						continue;
					}
					break;
				} /* switch */
			} /* if */
		} /* if */
		cbuf[0] = c;
		{
			int n = strlen(pbuf);
			prmt_size += n;
			if (c == '\n')
				cmdedit_prmt_len = 0;
			else if (flg_not_length != ']') {
#if 0 /*ENABLE_UNICODE_SUPPORT*/
/* Won't work, pbuf is one BYTE string here instead of an one Unicode char string. */
/* FIXME */
				cmdedit_prmt_len += unicode_strwidth(pbuf);
#else
				cmdedit_prmt_len += n;
#endif
			}
		}
		prmt_mem_ptr = strcat(xrealloc(prmt_mem_ptr, prmt_size+1), pbuf);
		free(free_me);
	} /* while */

# if ENABLE_USERNAME_OR_HOMEDIR
	if (cwd_buf != (char *)bb_msg_unknown)
		free(cwd_buf);
# endif
	/* see comment (above this function) about multiline prompt redrawing */
	cmdedit_prompt = prompt_last_line = prmt_mem_ptr;
	prmt_ptr = strrchr(cmdedit_prompt, '\n');
	if (prmt_ptr)
		prompt_last_line = prmt_ptr + 1;
	put_prompt();
}
#endif

#if ENABLE_FEATURE_EDITING_WINCH
static void cmdedit_setwidth(void)
{
	int new_y;

	cmdedit_termw = get_terminal_width(STDIN_FILENO);
	/* new y for current cursor */
	new_y = (cursor + cmdedit_prmt_len) / cmdedit_termw;
	/* redraw */
	redraw((new_y >= cmdedit_y ? new_y : cmdedit_y), command_len - cursor);
}

static void win_changed(int nsig UNUSED_PARAM)
{
	if (S.ok_to_redraw) {
		/* We are in read_key(), safe to redraw immediately */
		int sv_errno = errno;
		cmdedit_setwidth();
		fflush_all();
		errno = sv_errno;
	} else {
		/* Signal main loop that redraw is necessary */
		S.SIGWINCH_count++;
	}
}
#endif

static int lineedit_read_key(char *read_key_buffer, int timeout)
{
	int64_t ic;
#if ENABLE_UNICODE_SUPPORT
	char unicode_buf[MB_CUR_MAX + 1];
	int unicode_idx = 0;
#endif

	fflush_all();
	while (1) {
		/* Wait for input. TIMEOUT = -1 makes read_key wait even
		 * on nonblocking stdin, TIMEOUT = 50 makes sure we won't
		 * insist on full MB_CUR_MAX buffer to declare input like
		 * "\xff\n",pause,"ls\n" invalid and thus won't lose "ls".
		 *
		 * Note: read_key sets errno to 0 on success.
		 */
		IF_FEATURE_EDITING_WINCH(S.ok_to_redraw = 1;)
		ic = read_key(STDIN_FILENO, read_key_buffer, timeout);
		IF_FEATURE_EDITING_WINCH(S.ok_to_redraw = 0;)
		if (errno) {
#if ENABLE_UNICODE_SUPPORT
			if (errno == EAGAIN && unicode_idx != 0)
				goto pushback;
#endif
			break;
		}

#if ENABLE_FEATURE_EDITING_ASK_TERMINAL
		if ((int32_t)ic == KEYCODE_CURSOR_POS
		 && S.sent_ESC_br6n
		) {
			S.sent_ESC_br6n = 0;
			if (cursor == 0) { /* otherwise it may be bogus */
				int col = ((ic >> 32) & 0x7fff) - 1;
				/*
				 * Is col > cmdedit_prmt_len?
				 * If yes (terminal says cursor is farther to the right
				 * of where we think it should be),
				 * the prompt wasn't printed starting at col 1,
				 * there was additional text before it.
				 */
				if ((int)(col - cmdedit_prmt_len) > 0) {
					/* Fix our understanding of current x position */
					cmdedit_x += (col - cmdedit_prmt_len);
					while (cmdedit_x >= cmdedit_termw) {
						cmdedit_x -= cmdedit_termw;
						cmdedit_y++;
					}
				}
			}
			continue;
		}
#endif

#if ENABLE_UNICODE_SUPPORT
		if (unicode_status == UNICODE_ON) {
			wchar_t wc;

			if ((int32_t)ic < 0) /* KEYCODE_xxx */
				break;
			// TODO: imagine sequence like: 0xff,<left-arrow>: we are currently losing 0xff...

			unicode_buf[unicode_idx++] = ic;
			unicode_buf[unicode_idx] = '\0';
			if (mbstowcs(&wc, unicode_buf, 1) != 1) {
				/* Not (yet?) a valid unicode char */
				if (unicode_idx < MB_CUR_MAX) {
					timeout = 50;
					continue;
				}
 pushback:
				/* Invalid sequence. Save all "bad bytes" except first */
				read_key_ungets(read_key_buffer, unicode_buf + 1, unicode_idx - 1);
# if !ENABLE_UNICODE_PRESERVE_BROKEN
				ic = CONFIG_SUBST_WCHAR;
# else
				ic = unicode_mark_raw_byte(unicode_buf[0]);
# endif
			} else {
				/* Valid unicode char, return its code */
				ic = wc;
			}
		}
#endif
		break;
	}

	return ic;
}

#if ENABLE_UNICODE_BIDI_SUPPORT
static int isrtl_str(void)
{
	int idx = cursor;

	while (idx < command_len && unicode_bidi_is_neutral_wchar(command_ps[idx]))
		idx++;
	return unicode_bidi_isrtl(command_ps[idx]);
}
#else
# define isrtl_str() 0
#endif

/* leave out the "vi-mode"-only case labels if vi editing isn't
 * configured. */
#define vi_case(caselabel) IF_FEATURE_EDITING_VI(case caselabel)

/* convert uppercase ascii to equivalent control char, for readability */
#undef CTRL
#define CTRL(a) ((a) & ~0x40)

enum {
	VI_CMDMODE_BIT = 0x40000000,
	/* 0x80000000 bit flags KEYCODE_xxx */
};

#if ENABLE_FEATURE_REVERSE_SEARCH
/* Mimic readline Ctrl-R reverse history search.
 * When invoked, it shows the following prompt:
 * (reverse-i-search)'': user_input [cursor pos unchanged by Ctrl-R]
 * and typing results in search being performed:
 * (reverse-i-search)'tmp': cd /tmp [cursor under t in /tmp]
 * Search is performed by looking at progressively older lines in history.
 * Ctrl-R again searches for the next match in history.
 * Backspace deletes last matched char.
 * Control keys exit search and return to normal editing (at current history line).
 */
static int32_t reverse_i_search(int timeout)
{
	char match_buf[128]; /* for user input */
	char read_key_buffer[KEYCODE_BUFFER_SIZE];
	const char *matched_history_line;
	const char *saved_prompt;
	unsigned saved_prmt_len;
	int32_t ic;

	matched_history_line = NULL;
	read_key_buffer[0] = 0;
	match_buf[0] = '\0';

	/* Save and replace the prompt */
	saved_prompt = prompt_last_line;
	saved_prmt_len = cmdedit_prmt_len;
	goto set_prompt;

	while (1) {
		int h;
		unsigned match_buf_len = strlen(match_buf);

//FIXME: correct timeout? (i.e. count it down?)
		ic = lineedit_read_key(read_key_buffer, timeout);

		switch (ic) {
		case CTRL('R'): /* searching for the next match */
			break;

		case '\b':
		case '\x7f':
			/* Backspace */
			if (unicode_status == UNICODE_ON) {
				while (match_buf_len != 0) {
					uint8_t c = match_buf[--match_buf_len];
					if ((c & 0xc0) != 0x80) /* start of UTF-8 char? */
						break; /* yes */
				}
			} else {
				if (match_buf_len != 0)
					match_buf_len--;
			}
			match_buf[match_buf_len] = '\0';
			break;

		default:
			if (ic < ' '
			 || (!ENABLE_UNICODE_SUPPORT && ic >= 256)
			 || (ENABLE_UNICODE_SUPPORT && ic >= VI_CMDMODE_BIT)
			) {
				goto ret;
			}

			/* Append this char */
#if ENABLE_UNICODE_SUPPORT
			if (unicode_status == UNICODE_ON) {
				mbstate_t mbstate = { 0 };
				char buf[MB_CUR_MAX + 1];
				int len = wcrtomb(buf, ic, &mbstate);
				if (len > 0) {
					buf[len] = '\0';
					if (match_buf_len + len < sizeof(match_buf))
						strcpy(match_buf + match_buf_len, buf);
				}
			} else
#endif
			if (match_buf_len < sizeof(match_buf) - 1) {
				match_buf[match_buf_len] = ic;
				match_buf[match_buf_len + 1] = '\0';
			}
			break;
		} /* switch (ic) */

		/* Search in history for match_buf */
		h = state->cur_history;
		if (ic == CTRL('R'))
			h--;
		while (h >= 0) {
			if (state->history[h]) {
				char *match = strstr(state->history[h], match_buf);
				if (match) {
					state->cur_history = h;
					matched_history_line = state->history[h];
					command_len = load_string(matched_history_line);
					cursor = match - matched_history_line;
//FIXME: cursor position for Unicode case

					free((char*)prompt_last_line);
 set_prompt:
					prompt_last_line = xasprintf("(reverse-i-search)'%s': ", match_buf);
					cmdedit_prmt_len = unicode_strwidth(prompt_last_line);
					goto do_redraw;
				}
			}
			h--;
		}

		/* Not found */
		match_buf[match_buf_len] = '\0';
		beep();
		continue;

 do_redraw:
		redraw(cmdedit_y, command_len - cursor);
	} /* while (1) */

 ret:
	if (matched_history_line)
		command_len = load_string(matched_history_line);

	free((char*)prompt_last_line);
	prompt_last_line = saved_prompt;
	cmdedit_prmt_len = saved_prmt_len;
	redraw(cmdedit_y, command_len - cursor);

	return ic;
}
#endif

/* maxsize must be >= 2.
 * Returns:
 * -1 on read errors or EOF, or on bare Ctrl-D,
 * 0  on ctrl-C (the line entered is still returned in 'command'),
 * (in both cases the cursor remains on the input line, '\n' is not printed)
 * >0 length of input string, including terminating '\n'
 */
int FAST_FUNC read_line_input(line_input_t *st, const char *prompt, char *command, int maxsize)
{
	int len, n;
	int timeout;
#if ENABLE_FEATURE_TAB_COMPLETION
	smallint lastWasTab = 0;
#endif
	smallint break_out = 0;
#if ENABLE_FEATURE_EDITING_VI
	smallint vi_cmdmode = 0;
#endif
	struct termios initial_settings;
	struct termios new_settings;
	char read_key_buffer[KEYCODE_BUFFER_SIZE];

	INIT_S();

	n = get_termios_and_make_raw(STDIN_FILENO, &new_settings, &initial_settings, 0
		| TERMIOS_CLEAR_ISIG /* turn off INTR (ctrl-C), QUIT, SUSP */
	);
	if (n != 0 || (initial_settings.c_lflag & (ECHO|ICANON)) == ICANON) {
		/* Happens when e.g. stty -echo was run before.
		 * But if ICANON is not set, we don't come here.
		 * (example: interactive python ^Z-backgrounded,
		 * tty is still in "raw mode").
		 */
		parse_and_put_prompt(prompt);
		fflush_all();
		if (fgets(command, maxsize, stdin) == NULL)
			len = -1; /* EOF or error */
		else
			len = strlen(command);
		DEINIT_S();
		return len;
	}

	init_unicode();

// FIXME: audit & improve this
	if (maxsize > MAX_LINELEN)
		maxsize = MAX_LINELEN;
	S.maxsize = maxsize;

	timeout = -1;
	/* Make state->flags == 0 if st is NULL.
	 * With zeroed flags, no other fields are ever referenced.
	 */
	state = (line_input_t*) &const_int_0;
	if (st) {
		state = st;
		timeout = st->timeout;
	}
#if MAX_HISTORY > 0
# if ENABLE_FEATURE_EDITING_SAVEHISTORY
	if (state->hist_file)
		if (state->cnt_history == 0)
			load_history(state);
# endif
	if (state->flags & DO_HISTORY)
		state->cur_history = state->cnt_history;
#endif

	/* prepare before init handlers */
	cmdedit_y = 0;  /* quasireal y, not true if line > xt*yt */
	command_len = 0;
#if ENABLE_UNICODE_SUPPORT
	command_ps = xzalloc(maxsize * sizeof(command_ps[0]));
#else
	command_ps = command;
	command[0] = '\0';
#endif
#define command command_must_not_be_used

	tcsetattr_stdin_TCSANOW(&new_settings);

#if ENABLE_USERNAME_OR_HOMEDIR
	{
		struct passwd *entry;

		entry = getpwuid(geteuid());
		if (entry) {
			user_buf = xstrdup(entry->pw_name);
			home_pwd_buf = xstrdup(entry->pw_dir);
		}
	}
#endif

#if 0
	for (i = 0; i <= state->max_history; i++)
		bb_error_msg("history[%d]:'%s'", i, state->history[i]);
	bb_error_msg("cur_history:%d cnt_history:%d", state->cur_history, state->cnt_history);
#endif

	/* Get width (before printing prompt) */
	cmdedit_termw = get_terminal_width(STDIN_FILENO);
	/* Print out the command prompt, optionally ask where cursor is */
	parse_and_put_prompt(prompt);
	ask_terminal();

#if ENABLE_FEATURE_EDITING_WINCH
	/* Install window resize handler (NB: after *all* init is complete) */
	S.SIGWINCH_handler.sa_handler = win_changed;
	S.SIGWINCH_handler.sa_flags = SA_RESTART;
	sigaction(SIGWINCH, &S.SIGWINCH_handler, &S.SIGWINCH_handler);
#endif
	read_key_buffer[0] = 0;
	while (1) {
		/*
		 * The emacs and vi modes share much of the code in the big
		 * command loop.  Commands entered when in vi's command mode
		 * (aka "escape mode") get an extra bit added to distinguish
		 * them - this keeps them from being self-inserted. This
		 * clutters the big switch a bit, but keeps all the code
		 * in one place.
		 */
		int32_t ic, ic_raw;
#if ENABLE_FEATURE_EDITING_WINCH
		unsigned count;

		count = S.SIGWINCH_count;
		if (S.SIGWINCH_saved != count) {
			S.SIGWINCH_saved = count;
			cmdedit_setwidth();
		}
#endif
		ic = ic_raw = lineedit_read_key(read_key_buffer, timeout);

#if ENABLE_FEATURE_REVERSE_SEARCH
 again:
#endif
#if ENABLE_FEATURE_EDITING_VI
		newdelflag = 1;
		if (vi_cmdmode) {
			/* btw, since KEYCODE_xxx are all < 0, this doesn't
			 * change ic if it contains one of them: */
			ic |= VI_CMDMODE_BIT;
		}
#endif

		switch (ic) {
		case '\n':
		case '\r':
		vi_case('\n'|VI_CMDMODE_BIT:)
		vi_case('\r'|VI_CMDMODE_BIT:)
			/* Enter */
			goto_new_line();
			break_out = 1;
			break;
		case CTRL('A'):
		vi_case('0'|VI_CMDMODE_BIT:)
			/* Control-a -- Beginning of line */
			input_backward(cursor);
			break;
		case CTRL('B'):
		vi_case('h'|VI_CMDMODE_BIT:)
		vi_case('\b'|VI_CMDMODE_BIT:) /* ^H */
		vi_case('\x7f'|VI_CMDMODE_BIT:) /* DEL */
			input_backward(1); /* Move back one character */
			break;
		case CTRL('E'):
		vi_case('$'|VI_CMDMODE_BIT:)
			/* Control-e -- End of line */
			put_till_end_and_adv_cursor();
			break;
		case CTRL('F'):
		vi_case('l'|VI_CMDMODE_BIT:)
		vi_case(' '|VI_CMDMODE_BIT:)
			input_forward(); /* Move forward one character */
			break;
		case '\b':   /* ^H */
		case '\x7f': /* DEL */
			if (!isrtl_str())
				input_backspace();
			else
				input_delete(0);
			break;
		case KEYCODE_DELETE:
			if (!isrtl_str())
				input_delete(0);
			else
				input_backspace();
			break;
#if ENABLE_FEATURE_TAB_COMPLETION
		case '\t':
			input_tab(&lastWasTab);
			break;
#endif
		case CTRL('K'):
			/* Control-k -- clear to end of line */
			command_ps[cursor] = BB_NUL;
			command_len = cursor;
			printf(SEQ_CLEAR_TILL_END_OF_SCREEN);
			break;
		case CTRL('L'):
		vi_case(CTRL('L')|VI_CMDMODE_BIT:)
			/* Control-l -- clear screen */
			/* cursor to top,left; clear to the end of screen */
			printf(ESC"[H" ESC"[J");
			draw_full(command_len - cursor);
			break;
#if MAX_HISTORY > 0
		case CTRL('N'):
		vi_case(CTRL('N')|VI_CMDMODE_BIT:)
		vi_case('j'|VI_CMDMODE_BIT:)
			/* Control-n -- Get next command in history */
			if (get_next_history())
				goto rewrite_line;
			break;
		case CTRL('P'):
		vi_case(CTRL('P')|VI_CMDMODE_BIT:)
		vi_case('k'|VI_CMDMODE_BIT:)
			/* Control-p -- Get previous command from history */
			if (get_previous_history())
				goto rewrite_line;
			break;
#endif
		case CTRL('U'):
		vi_case(CTRL('U')|VI_CMDMODE_BIT:)
			/* Control-U -- Clear line before cursor */
			if (cursor) {
				command_len -= cursor;
				memmove(command_ps, command_ps + cursor,
					(command_len + 1) * sizeof(command_ps[0]));
				redraw(cmdedit_y, command_len);
			}
			break;
		case CTRL('W'):
		vi_case(CTRL('W')|VI_CMDMODE_BIT:)
			/* Control-W -- Remove the last word */
			while (cursor > 0 && BB_isspace(command_ps[cursor-1]))
				input_backspace();
			while (cursor > 0 && !BB_isspace(command_ps[cursor-1]))
				input_backspace();
			break;
		case KEYCODE_ALT_D: {
			/* Delete word forward */
			int nc, sc = cursor;
			ctrl_right();
			nc = cursor - sc;
			input_backward(nc);
			while (--nc >= 0)
				input_delete(1);
			break;
		}
		case KEYCODE_ALT_BACKSPACE: {
			/* Delete word backward */
			int sc = cursor;
			ctrl_left();
			while (sc-- > cursor)
				input_delete(1);
			break;
		}
#if ENABLE_FEATURE_REVERSE_SEARCH
		case CTRL('R'):
			ic = ic_raw = reverse_i_search(timeout);
			goto again;
#endif

#if ENABLE_FEATURE_EDITING_VI
		case 'i'|VI_CMDMODE_BIT:
			vi_cmdmode = 0;
			break;
		case 'I'|VI_CMDMODE_BIT:
			input_backward(cursor);
			vi_cmdmode = 0;
			break;
		case 'a'|VI_CMDMODE_BIT:
			input_forward();
			vi_cmdmode = 0;
			break;
		case 'A'|VI_CMDMODE_BIT:
			put_till_end_and_adv_cursor();
			vi_cmdmode = 0;
			break;
		case 'x'|VI_CMDMODE_BIT:
			input_delete(1);
			break;
		case 'X'|VI_CMDMODE_BIT:
			if (cursor > 0) {
				input_backward(1);
				input_delete(1);
			}
			break;
		case 'W'|VI_CMDMODE_BIT:
			vi_Word_motion(1);
			break;
		case 'w'|VI_CMDMODE_BIT:
			vi_word_motion(1);
			break;
		case 'E'|VI_CMDMODE_BIT:
			vi_End_motion();
			break;
		case 'e'|VI_CMDMODE_BIT:
			vi_end_motion();
			break;
		case 'B'|VI_CMDMODE_BIT:
			vi_Back_motion();
			break;
		case 'b'|VI_CMDMODE_BIT:
			vi_back_motion();
			break;
		case 'C'|VI_CMDMODE_BIT:
			vi_cmdmode = 0;
			/* fall through */
		case 'D'|VI_CMDMODE_BIT:
			goto clear_to_eol;

		case 'c'|VI_CMDMODE_BIT:
			vi_cmdmode = 0;
			/* fall through */
		case 'd'|VI_CMDMODE_BIT: {
			int nc, sc;

			ic = lineedit_read_key(read_key_buffer, timeout);
			if (errno) /* error */
				goto return_error_indicator;
			if (ic == ic_raw) { /* "cc", "dd" */
				input_backward(cursor);
				goto clear_to_eol;
				break;
			}

			sc = cursor;
			switch (ic) {
			case 'w':
			case 'W':
			case 'e':
			case 'E':
				switch (ic) {
				case 'w':   /* "dw", "cw" */
					vi_word_motion(vi_cmdmode);
					break;
				case 'W':   /* 'dW', 'cW' */
					vi_Word_motion(vi_cmdmode);
					break;
				case 'e':   /* 'de', 'ce' */
					vi_end_motion();
					input_forward();
					break;
				case 'E':   /* 'dE', 'cE' */
					vi_End_motion();
					input_forward();
					break;
				}
				nc = cursor;
				input_backward(cursor - sc);
				while (nc-- > cursor)
					input_delete(1);
				break;
			case 'b':  /* "db", "cb" */
			case 'B':  /* implemented as B */
				if (ic == 'b')
					vi_back_motion();
				else
					vi_Back_motion();
				while (sc-- > cursor)
					input_delete(1);
				break;
			case ' ':  /* "d ", "c " */
				input_delete(1);
				break;
			case '$':  /* "d$", "c$" */
 clear_to_eol:
				while (cursor < command_len)
					input_delete(1);
				break;
			}
			break;
		}
		case 'p'|VI_CMDMODE_BIT:
			input_forward();
			/* fallthrough */
		case 'P'|VI_CMDMODE_BIT:
			put();
			break;
		case 'r'|VI_CMDMODE_BIT:
//FIXME: unicode case?
			ic = lineedit_read_key(read_key_buffer, timeout);
			if (errno) /* error */
				goto return_error_indicator;
			if (ic < ' ' || ic > 255) {
				beep();
			} else {
				command_ps[cursor] = ic;
				bb_putchar(ic);
				bb_putchar('\b');
			}
			break;
		case '\x1b': /* ESC */
			if (state->flags & VI_MODE) {
				/* insert mode --> command mode */
				vi_cmdmode = 1;
				input_backward(1);
			}
			break;
#endif /* FEATURE_COMMAND_EDITING_VI */

#if MAX_HISTORY > 0
		case KEYCODE_UP:
			if (get_previous_history())
				goto rewrite_line;
			beep();
			break;
		case KEYCODE_DOWN:
			if (!get_next_history())
				break;
 rewrite_line:
			/* Rewrite the line with the selected history item */
			/* change command */
			command_len = load_string(state->history[state->cur_history] ?
					state->history[state->cur_history] : "");
			/* redraw and go to eol (bol, in vi) */
			redraw(cmdedit_y, (state->flags & VI_MODE) ? 9999 : 0);
			break;
#endif
		case KEYCODE_RIGHT:
			input_forward();
			break;
		case KEYCODE_LEFT:
			input_backward(1);
			break;
		case KEYCODE_CTRL_LEFT:
		case KEYCODE_ALT_LEFT: /* bash doesn't do it */
			ctrl_left();
			break;
		case KEYCODE_CTRL_RIGHT:
		case KEYCODE_ALT_RIGHT: /* bash doesn't do it */
			ctrl_right();
			break;
		case KEYCODE_HOME:
			input_backward(cursor);
			break;
		case KEYCODE_END:
			put_till_end_and_adv_cursor();
			break;

		default:
			if (initial_settings.c_cc[VINTR] != 0
			 && ic_raw == initial_settings.c_cc[VINTR]
			) {
				/* Ctrl-C (usually) - stop gathering input */
				command_len = 0;
				break_out = -1; /* "do not append '\n'" */
				break;
			}
			if (initial_settings.c_cc[VEOF] != 0
			 && ic_raw == initial_settings.c_cc[VEOF]
			) {
				/* Ctrl-D (usually) - delete one character,
				 * or exit if len=0 and no chars to delete */
				if (command_len == 0) {
					errno = 0;

		case -1: /* error (e.g. EIO when tty is destroyed) */
 IF_FEATURE_EDITING_VI(return_error_indicator:)
					break_out = command_len = -1;
					break;
				}
				input_delete(0);
				break;
			}
//			/* Control-V -- force insert of next char */
//			if (c == CTRL('V')) {
//				if (safe_read(STDIN_FILENO, &c, 1) < 1)
//					goto return_error_indicator;
//				if (c == 0) {
//					beep();
//					break;
//				}
//			}
			if (ic < ' '
			 || (!ENABLE_UNICODE_SUPPORT && ic >= 256)
			 || (ENABLE_UNICODE_SUPPORT && ic >= VI_CMDMODE_BIT)
			) {
				/* If VI_CMDMODE_BIT is set, ic is >= 256
				 * and vi mode ignores unexpected chars.
				 * Otherwise, we are here if ic is a
				 * control char or an unhandled ESC sequence,
				 * which is also ignored.
				 */
				break;
			}
			if ((int)command_len >= (maxsize - 2)) {
				/* Not enough space for the char and EOL */
				break;
			}

			command_len++;
			if (cursor == (command_len - 1)) {
				/* We are at the end, append */
				command_ps[cursor] = ic;
				command_ps[cursor + 1] = BB_NUL;
				put_cur_glyph_and_inc_cursor();
				if (unicode_bidi_isrtl(ic))
					input_backward(1);
			} else {
				/* In the middle, insert */
				int sc = cursor;

				memmove(command_ps + sc + 1, command_ps + sc,
					(command_len - sc) * sizeof(command_ps[0]));
				command_ps[sc] = ic;
				/* is right-to-left char, or neutral one (e.g. comma) was just added to rtl text? */
				if (!isrtl_str())
					sc++; /* no */
				put_till_end_and_adv_cursor();
				/* to prev x pos + 1 */
				input_backward(cursor - sc);
			}
			break;
		} /* switch (ic) */

		if (break_out)
			break;

#if ENABLE_FEATURE_TAB_COMPLETION
		if (ic_raw != '\t')
			lastWasTab = 0;
#endif
	} /* while (1) */

#if ENABLE_FEATURE_EDITING_ASK_TERMINAL
	if (S.sent_ESC_br6n) {
		/* "sleep 1; busybox ash" + hold [Enter] to trigger.
		 * We sent "ESC [ 6 n", but got '\n' first, and
		 * KEYCODE_CURSOR_POS response is now buffered from terminal.
		 * It's bad already and not much can be done with it
		 * (it _will_ be visible for the next process to read stdin),
		 * but without this delay it even shows up on the screen
		 * as garbage because we restore echo settings with tcsetattr
		 * before it comes in. UGLY!
		 */
		usleep(20*1000);
	}
#endif

/* End of bug-catching "command_must_not_be_used" trick */
#undef command

#if ENABLE_UNICODE_SUPPORT
	command[0] = '\0';
	if (command_len > 0)
		command_len = save_string(command, maxsize - 1);
	free(command_ps);
#endif

	if (command_len > 0) {
		remember_in_history(command);
	}

	if (break_out > 0) {
		command[command_len++] = '\n';
		command[command_len] = '\0';
	}

#if ENABLE_FEATURE_TAB_COMPLETION
	free_tab_completion_data();
#endif

	/* restore initial_settings */
	tcsetattr_stdin_TCSANOW(&initial_settings);
#if ENABLE_FEATURE_EDITING_WINCH
	/* restore SIGWINCH handler */
	sigaction_set(SIGWINCH, &S.SIGWINCH_handler);
#endif
	fflush_all();

	len = command_len;
	DEINIT_S();

	return len; /* can't return command_len, DEINIT_S() destroys it */
}

#else  /* !FEATURE_EDITING */

#undef read_line_input
int FAST_FUNC read_line_input(const char* prompt, char* command, int maxsize)
{
	fputs(prompt, stdout);
	fflush_all();
	if (!fgets(command, maxsize, stdin))
		return -1;
	return strlen(command);
}

#endif  /* !FEATURE_EDITING */


/*
 * Testing
 */

#ifdef TEST

#include <locale.h>

const char *applet_name = "debug stuff usage";

int main(int argc, char **argv)
{
	char buff[MAX_LINELEN];
	char *prompt =
#if ENABLE_FEATURE_EDITING_FANCY_PROMPT
		"\\[\\033[32;1m\\]\\u@\\[\\x1b[33;1m\\]\\h:"
		"\\[\\033[34;1m\\]\\w\\[\\033[35;1m\\] "
		"\\!\\[\\e[36;1m\\]\\$ \\[\\E[m\\]";
#else
		"% ";
#endif

	while (1) {
		int l;
		l = read_line_input(prompt, buff);
		if (l <= 0 || buff[l-1] != '\n')
			break;
		buff[l-1] = '\0';
		printf("*** read_line_input() returned line =%s=\n", buff);
	}
	printf("*** read_line_input() detect ^D\n");
	return 0;
}

#endif  /* TEST */
