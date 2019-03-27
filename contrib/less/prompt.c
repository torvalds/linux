/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 * Prompting and other messages.
 * There are three flavors of prompts, SHORT, MEDIUM and LONG,
 * selected by the -m/-M options.
 * There is also the "equals message", printed by the = command.
 * A prompt is a message composed of various pieces, such as the 
 * name of the file being viewed, the percentage into the file, etc.
 */

#include "less.h"
#include "position.h"

extern int pr_type;
extern int new_file;
extern int sc_width;
extern int so_s_width, so_e_width;
extern int linenums;
extern int hshift;
extern int sc_height;
extern int jump_sline;
extern int less_is_more;
extern IFILE curr_ifile;
#if EDITOR
extern char *editor;
extern char *editproto;
#endif

/*
 * Prototypes for the three flavors of prompts.
 * These strings are expanded by pr_expand().
 */
static constant char s_proto[] =
  "?n?f%f .?m(%T %i of %m) ..?e(END) ?x- Next\\: %x..%t";
static constant char m_proto[] =
  "?n?f%f .?m(%T %i of %m) ..?e(END) ?x- Next\\: %x.:?pB%pB\\%:byte %bB?s/%s...%t";
static constant char M_proto[] =
  "?f%f .?n?m(%T %i of %m) ..?ltlines %lt-%lb?L/%L. :byte %bB?s/%s. .?e(END) ?x- Next\\: %x.:?pB%pB\\%..%t";
static constant char e_proto[] =
  "?f%f .?m(%T %i of %m) .?ltlines %lt-%lb?L/%L. .byte %bB?s/%s. ?e(END) :?pB%pB\\%..%t";
static constant char h_proto[] =
  "HELP -- ?eEND -- Press g to see it again:Press RETURN for more., or q when done";
static constant char w_proto[] =
  "Waiting for data";
static constant char more_proto[] =
  "--More--(?eEND ?x- Next\\: %x.:?pB%pB\\%:byte %bB?s/%s...%t)";

public char *prproto[3];
public char constant *eqproto = e_proto;
public char constant *hproto = h_proto;
public char constant *wproto = w_proto;

static char message[PROMPT_SIZE];
static char *mp;

/*
 * Initialize the prompt prototype strings.
 */
	public void
init_prompt()
{
	prproto[0] = save(s_proto);
	prproto[1] = save(less_is_more ? more_proto : m_proto);
	prproto[2] = save(M_proto);
	eqproto = save(e_proto);
	hproto = save(h_proto);
	wproto = save(w_proto);
}

/*
 * Append a string to the end of the message.
 */
	static void
ap_str(s)
	char *s;
{
	int len;

	len = (int) strlen(s);
	if (mp + len >= message + PROMPT_SIZE)
		len = (int) (message + PROMPT_SIZE - mp - 1);
	strncpy(mp, s, len);
	mp += len;
	*mp = '\0';
}

/*
 * Append a character to the end of the message.
 */
	static void
ap_char(c)
	char c;
{
	char buf[2];

	buf[0] = c;
	buf[1] = '\0';
	ap_str(buf);
}

/*
 * Append a POSITION (as a decimal integer) to the end of the message.
 */
	static void
ap_pos(pos)
	POSITION pos;
{
	char buf[INT_STRLEN_BOUND(pos) + 2];

	postoa(pos, buf);
	ap_str(buf);
}

/*
 * Append a line number to the end of the message.
 */
 	static void
ap_linenum(linenum)
	LINENUM linenum;
{
	char buf[INT_STRLEN_BOUND(linenum) + 2];

	linenumtoa(linenum, buf);
	ap_str(buf);
}

/*
 * Append an integer to the end of the message.
 */
	static void
ap_int(num)
	int num;
{
	char buf[INT_STRLEN_BOUND(num) + 2];

	inttoa(num, buf);
	ap_str(buf);
}

/*
 * Append a question mark to the end of the message.
 */
	static void
ap_quest()
{
	ap_str("?");
}

/*
 * Return the "current" byte offset in the file.
 */
	static POSITION
curr_byte(where)
	int where;
{
	POSITION pos;

	pos = position(where);
	while (pos == NULL_POSITION && where >= 0 && where < sc_height-1)
		pos = position(++where);
	if (pos == NULL_POSITION)
		pos = ch_length();
	return (pos);
}

/*
 * Return the value of a prototype conditional.
 * A prototype string may include conditionals which consist of a 
 * question mark followed by a single letter.
 * Here we decode that letter and return the appropriate boolean value.
 */
	static int
cond(c, where)
	char c;
	int where;
{
	POSITION len;

	switch (c)
	{
	case 'a':	/* Anything in the message yet? */
		return (mp > message);
	case 'b':	/* Current byte offset known? */
		return (curr_byte(where) != NULL_POSITION);
	case 'c':
		return (hshift != 0);
	case 'e':	/* At end of file? */
		return (eof_displayed());
	case 'f':	/* Filename known? */
		return (strcmp(get_filename(curr_ifile), "-") != 0);
	case 'l':	/* Line number known? */
	case 'd':	/* Same as l */
		return (linenums);
	case 'L':	/* Final line number known? */
	case 'D':	/* Final page number known? */
		return (linenums && ch_length() != NULL_POSITION);
	case 'm':	/* More than one file? */
#if TAGS
		return (ntags() ? (ntags() > 1) : (nifile() > 1));
#else
		return (nifile() > 1);
#endif
	case 'n':	/* First prompt in a new file? */
#if TAGS
		return (ntags() ? 1 : new_file);
#else
		return (new_file);
#endif
	case 'p':	/* Percent into file (bytes) known? */
		return (curr_byte(where) != NULL_POSITION && 
				ch_length() > 0);
	case 'P':	/* Percent into file (lines) known? */
		return (currline(where) != 0 &&
				(len = ch_length()) > 0 &&
				find_linenum(len) != 0);
	case 's':	/* Size of file known? */
	case 'B':
		return (ch_length() != NULL_POSITION);
	case 'x':	/* Is there a "next" file? */
#if TAGS
		if (ntags())
			return (0);
#endif
		return (next_ifile(curr_ifile) != NULL_IFILE);
	}
	return (0);
}

/*
 * Decode a "percent" prototype character.
 * A prototype string may include various "percent" escapes;
 * that is, a percent sign followed by a single letter.
 * Here we decode that letter and take the appropriate action,
 * usually by appending something to the message being built.
 */
	static void
protochar(c, where, iseditproto)
	int c;
	int where;
	int iseditproto;
{
	POSITION pos;
	POSITION len;
	int n;
	LINENUM linenum;
	LINENUM last_linenum;
	IFILE h;

#undef  PAGE_NUM
#define PAGE_NUM(linenum)  ((((linenum) - 1) / (sc_height - 1)) + 1)

	switch (c)
	{
	case 'b':	/* Current byte offset */
		pos = curr_byte(where);
		if (pos != NULL_POSITION)
			ap_pos(pos);
		else
			ap_quest();
		break;
	case 'c':
		ap_int(hshift);
		break;
	case 'd':	/* Current page number */
		linenum = currline(where);
		if (linenum > 0 && sc_height > 1)
			ap_linenum(PAGE_NUM(linenum));
		else
			ap_quest();
		break;
	case 'D':	/* Final page number */
		/* Find the page number of the last byte in the file (len-1). */
		len = ch_length();
		if (len == NULL_POSITION)
			ap_quest();
		else if (len == 0)
			/* An empty file has no pages. */
			ap_linenum(0);
		else
		{
			linenum = find_linenum(len - 1);
			if (linenum <= 0)
				ap_quest();
			else 
				ap_linenum(PAGE_NUM(linenum));
		}
		break;
#if EDITOR
	case 'E':	/* Editor name */
		ap_str(editor);
		break;
#endif
	case 'f':	/* File name */
		ap_str(get_filename(curr_ifile));
		break;
	case 'F':	/* Last component of file name */
		ap_str(last_component(get_filename(curr_ifile)));
		break;
	case 'i':	/* Index into list of files */
#if TAGS
		if (ntags())
			ap_int(curr_tag());
		else
#endif
			ap_int(get_index(curr_ifile));
		break;
	case 'l':	/* Current line number */
		linenum = currline(where);
		if (linenum != 0)
			ap_linenum(linenum);
		else
			ap_quest();
		break;
	case 'L':	/* Final line number */
		len = ch_length();
		if (len == NULL_POSITION || len == ch_zero() ||
		    (linenum = find_linenum(len)) <= 0)
			ap_quest();
		else
			ap_linenum(linenum-1);
		break;
	case 'm':	/* Number of files */
#if TAGS
		n = ntags();
		if (n)
			ap_int(n);
		else
#endif
			ap_int(nifile());
		break;
	case 'p':	/* Percent into file (bytes) */
		pos = curr_byte(where);
		len = ch_length();
		if (pos != NULL_POSITION && len > 0)
			ap_int(percentage(pos,len));
		else
			ap_quest();
		break;
	case 'P':	/* Percent into file (lines) */
		linenum = currline(where);
		if (linenum == 0 ||
		    (len = ch_length()) == NULL_POSITION || len == ch_zero() ||
		    (last_linenum = find_linenum(len)) <= 0)
			ap_quest();
		else
			ap_int(percentage(linenum, last_linenum));
		break;
	case 's':	/* Size of file */
	case 'B':
		len = ch_length();
		if (len != NULL_POSITION)
			ap_pos(len);
		else
			ap_quest();
		break;
	case 't':	/* Truncate trailing spaces in the message */
		while (mp > message && mp[-1] == ' ')
			mp--;
		*mp = '\0';
		break;
	case 'T':	/* Type of list */
#if TAGS
		if (ntags())
			ap_str("tag");
		else
#endif
			ap_str("file");
		break;
	case 'x':	/* Name of next file */
		h = next_ifile(curr_ifile);
		if (h != NULL_IFILE)
			ap_str(get_filename(h));
		else
			ap_quest();
		break;
	}
}

/*
 * Skip a false conditional.
 * When a false condition is found (either a false IF or the ELSE part 
 * of a true IF), this routine scans the prototype string to decide
 * where to resume parsing the string.
 * We must keep track of nested IFs and skip them properly.
 */
	static constant char *
skipcond(p)
	constant char *p;
{
	int iflevel;

	/*
	 * We came in here after processing a ? or :,
	 * so we start nested one level deep.
	 */
	iflevel = 1;

	for (;;) switch (*++p)
	{
	case '?':
		/*
		 * Start of a nested IF.
		 */
		iflevel++;
		break;
	case ':':
		/*
		 * Else.
		 * If this matches the IF we came in here with,
		 * then we're done.
		 */
		if (iflevel == 1)
			return (p);
		break;
	case '.':
		/*
		 * Endif.
		 * If this matches the IF we came in here with,
		 * then we're done.
		 */
		if (--iflevel == 0)
			return (p);
		break;
	case '\\':
		/*
		 * Backslash escapes the next character.
		 */
		++p;
		break;
	case '\0':
		/*
		 * Whoops.  Hit end of string.
		 * This is a malformed conditional, but just treat it
		 * as if all active conditionals ends here.
		 */
		return (p-1);
	}
	/*NOTREACHED*/
}

/*
 * Decode a char that represents a position on the screen.
 */
	static constant char *
wherechar(p, wp)
	char constant *p;
	int *wp;
{
	switch (*p)
	{
	case 'b': case 'd': case 'l': case 'p': case 'P':
		switch (*++p)
		{
		case 't':   *wp = TOP;			break;
		case 'm':   *wp = MIDDLE;		break;
		case 'b':   *wp = BOTTOM;		break;
		case 'B':   *wp = BOTTOM_PLUS_ONE;	break;
		case 'j':   *wp = sindex_from_sline(jump_sline); break;
		default:    *wp = TOP;  p--;		break;
		}
	}
	return (p);
}

/*
 * Construct a message based on a prototype string.
 */
	public char *
pr_expand(proto, maxwidth)
	constant char *proto;
	int maxwidth;
{
	constant char *p;
	int c;
	int where;

	mp = message;

	if (*proto == '\0')
		return ("");

	for (p = proto;  *p != '\0';  p++)
	{
		switch (*p)
		{
		default:	/* Just put the character in the message */
			ap_char(*p);
			break;
		case '\\':	/* Backslash escapes the next character */
			p++;
			ap_char(*p);
			break;
		case '?':	/* Conditional (IF) */
			if ((c = *++p) == '\0')
				--p;
			else
			{
				where = 0;
				p = wherechar(p, &where);
				if (!cond(c, where))
					p = skipcond(p);
			}
			break;
		case ':':	/* ELSE */
			p = skipcond(p);
			break;
		case '.':	/* ENDIF */
			break;
		case '%':	/* Percent escape */
			if ((c = *++p) == '\0')
				--p;
			else
			{
				where = 0;
				p = wherechar(p, &where);
				protochar(c, where,
#if EDITOR
					(proto == editproto));
#else
					0);
#endif

			}
			break;
		}
	}

	if (mp == message)
		return ("");
	if (maxwidth > 0 && mp >= message + maxwidth)
	{
		/*
		 * Message is too long.
		 * Return just the final portion of it.
		 */
		return (mp - maxwidth);
	}
	return (message);
}

/*
 * Return a message suitable for printing by the "=" command.
 */
	public char *
eq_message()
{
	return (pr_expand(eqproto, 0));
}

/*
 * Return a prompt.
 * This depends on the prompt type (SHORT, MEDIUM, LONG), etc.
 * If we can't come up with an appropriate prompt, return NULL
 * and the caller will prompt with a colon.
 */
	public char *
pr_string()
{
	char *prompt;
	int type;

	type = (!less_is_more) ? pr_type : pr_type ? 0 : 1;
	prompt = pr_expand((ch_getflags() & CH_HELPFILE) ?
				hproto : prproto[type],
			sc_width-so_s_width-so_e_width-2);
	new_file = 0;
	return (prompt);
}

/*
 * Return a message suitable for printing while waiting in the F command.
 */
	public char *
wait_message()
{
	return (pr_expand(wproto, sc_width-so_s_width-so_e_width-2));
}
