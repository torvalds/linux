/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 * Functions to define the character set
 * and do things specific to the character set.
 */

#include "less.h"
#if HAVE_LOCALE
#include <locale.h>
#include <ctype.h>
#include <langinfo.h>
#endif

#include "charset.h"

#if MSDOS_COMPILER==WIN32C
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

extern int bs_mode;

public int utf_mode = 0;

/*
 * Predefined character sets,
 * selected by the LESSCHARSET environment variable.
 */
struct charset {
	char *name;
	int *p_flag;
	char *desc;
} charsets[] = {
	{ "ascii",		NULL,       "8bcccbcc18b95.b" },
	{ "utf-8",		&utf_mode,  "8bcccbcc18b95.b126.bb" },
	{ "iso8859",		NULL,       "8bcccbcc18b95.33b." },
	{ "latin3",		NULL,       "8bcccbcc18b95.33b5.b8.b15.b4.b12.b18.b12.b." },
	{ "arabic",		NULL,       "8bcccbcc18b95.33b.3b.7b2.13b.3b.b26.5b19.b" },
	{ "greek",		NULL,       "8bcccbcc18b95.33b4.2b4.b3.b35.b44.b" },
	{ "greek2005",		NULL,       "8bcccbcc18b95.33b14.b35.b44.b" },
	{ "hebrew",		NULL,       "8bcccbcc18b95.33b.b29.32b28.2b2.b" },
	{ "koi8-r",		NULL,       "8bcccbcc18b95.b." },
	{ "KOI8-T",		NULL,       "8bcccbcc18b95.b8.b6.b8.b.b.5b7.3b4.b4.b3.b.b.3b." },
	{ "georgianps",		NULL,       "8bcccbcc18b95.3b11.4b12.2b." },
	{ "tcvn",		NULL,       "b..b...bcccbccbbb7.8b95.b48.5b." },
	{ "TIS-620",		NULL,       "8bcccbcc18b95.b.4b.11b7.8b." },
	{ "next",		NULL,       "8bcccbcc18b95.bb125.bb" },
	{ "dos",		NULL,       "8bcccbcc12bc5b95.b." },
	{ "windows-1251",	NULL,       "8bcccbcc12bc5b95.b24.b." },
	{ "windows-1252",	NULL,       "8bcccbcc12bc5b95.b.b11.b.2b12.b." },
	{ "windows-1255",	NULL,       "8bcccbcc12bc5b95.b.b8.b.5b9.b.4b." },
	{ "ebcdic",		NULL,       "5bc6bcc7bcc41b.9b7.9b5.b..8b6.10b6.b9.7b9.8b8.17b3.3b9.7b9.8b8.6b10.b.b.b." },
	{ "IBM-1047",		NULL,       "4cbcbc3b9cbccbccbb4c6bcc5b3cbbc4bc4bccbc191.b" },
	{ NULL, NULL, NULL }
};

/*
 * Support "locale charmap"/nl_langinfo(CODESET) values, as well as others.
 */
struct cs_alias {
	char *name;
	char *oname;
} cs_aliases[] = {
	{ "UTF-8",		"utf-8" },
	{ "utf8",		"utf-8" },
	{ "UTF8",		"utf-8" },
	{ "ANSI_X3.4-1968",	"ascii" },
	{ "US-ASCII",		"ascii" },
	{ "latin1",		"iso8859" },
	{ "ISO-8859-1",		"iso8859" },
	{ "latin9",		"iso8859" },
	{ "ISO-8859-15",	"iso8859" },
	{ "latin2",		"iso8859" },
	{ "ISO-8859-2",		"iso8859" },
	{ "ISO-8859-3",		"latin3" },
	{ "latin4",		"iso8859" },
	{ "ISO-8859-4",		"iso8859" },
	{ "cyrillic",		"iso8859" },
	{ "ISO-8859-5",		"iso8859" },
	{ "ISO-8859-6",		"arabic" },
	{ "ISO-8859-7",		"greek" },
	{ "IBM9005",		"greek2005" },
	{ "ISO-8859-8",		"hebrew" },
	{ "latin5",		"iso8859" },
	{ "ISO-8859-9",		"iso8859" },
	{ "latin6",		"iso8859" },
	{ "ISO-8859-10",	"iso8859" },
	{ "latin7",		"iso8859" },
	{ "ISO-8859-13",	"iso8859" },
	{ "latin8",		"iso8859" },
	{ "ISO-8859-14",	"iso8859" },
	{ "latin10",		"iso8859" },
	{ "ISO-8859-16",	"iso8859" },
	{ "IBM437",		"dos" },
	{ "EBCDIC-US",		"ebcdic" },
	{ "IBM1047",		"IBM-1047" },
	{ "KOI8-R",		"koi8-r" },
	{ "KOI8-U",		"koi8-r" },
	{ "GEORGIAN-PS",	"georgianps" },
	{ "TCVN5712-1", 	"tcvn" },
	{ "NEXTSTEP",		"next" },
	{ "windows",		"windows-1252" }, /* backward compatibility */
	{ "CP1251",		"windows-1251" },
	{ "CP1252",		"windows-1252" },
	{ "CP1255",		"windows-1255" },
	{ NULL, NULL }
};

#define	IS_BINARY_CHAR	01
#define	IS_CONTROL_CHAR	02

static char chardef[256];
static char *binfmt = NULL;
static char *utfbinfmt = NULL;
public int binattr = AT_STANDOUT;


/*
 * Define a charset, given a description string.
 * The string consists of 256 letters,
 * one for each character in the charset.
 * If the string is shorter than 256 letters, missing letters
 * are taken to be identical to the last one.
 * A decimal number followed by a letter is taken to be a 
 * repetition of the letter.
 *
 * Each letter is one of:
 *	. normal character
 *	b binary character
 *	c control character
 */
	static void
ichardef(s)
	char *s;
{
	char *cp;
	int n;
	char v;

	n = 0;
	v = 0;
	cp = chardef;
	while (*s != '\0')
	{
		switch (*s++)
		{
		case '.':
			v = 0;
			break;
		case 'c':
			v = IS_CONTROL_CHAR;
			break;
		case 'b':
			v = IS_BINARY_CHAR|IS_CONTROL_CHAR;
			break;

		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = (10 * n) + (s[-1] - '0');
			continue;

		default:
			error("invalid chardef", NULL_PARG);
			quit(QUIT_ERROR);
			/*NOTREACHED*/
		}

		do
		{
			if (cp >= chardef + sizeof(chardef))
			{
				error("chardef longer than 256", NULL_PARG);
				quit(QUIT_ERROR);
				/*NOTREACHED*/
			}
			*cp++ = v;
		} while (--n > 0);
		n = 0;
	}

	while (cp < chardef + sizeof(chardef))
		*cp++ = v;
}

/*
 * Define a charset, given a charset name.
 * The valid charset names are listed in the "charsets" array.
 */
	static int
icharset(name, no_error)
	char *name;
	int no_error;
{
	struct charset *p;
	struct cs_alias *a;

	if (name == NULL || *name == '\0')
		return (0);

	/* First see if the name is an alias. */
	for (a = cs_aliases;  a->name != NULL;  a++)
	{
		if (strcmp(name, a->name) == 0)
		{
			name = a->oname;
			break;
		}
	}

	for (p = charsets;  p->name != NULL;  p++)
	{
		if (strcmp(name, p->name) == 0)
		{
			ichardef(p->desc);
			if (p->p_flag != NULL)
			{
#if MSDOS_COMPILER==WIN32C
				*(p->p_flag) = 1 + (GetConsoleOutputCP() != CP_UTF8);
#else
				*(p->p_flag) = 1;
#endif
			}
			return (1);
		}
	}

	if (!no_error) {
		error("invalid charset name", NULL_PARG);
		quit(QUIT_ERROR);
	}
	return (0);
}

#if HAVE_LOCALE
/*
 * Define a charset, given a locale name.
 */
	static void
ilocale()
{
	int c;

	for (c = 0;  c < (int) sizeof(chardef);  c++)
	{
		if (isprint(c))
			chardef[c] = 0;
		else if (iscntrl(c))
			chardef[c] = IS_CONTROL_CHAR;
		else
			chardef[c] = IS_BINARY_CHAR|IS_CONTROL_CHAR;
	}
}
#endif

/*
 * Define the printing format for control (or binary utf) chars.
 */
	public void
setfmt(s, fmtvarptr, attrptr, default_fmt)
	char *s;
	char **fmtvarptr;
	int *attrptr;
	char *default_fmt;
{
	if (s && utf_mode)
	{
		/* It would be too hard to account for width otherwise.  */
		char constant *t = s;
		while (*t)
		{
			if (*t < ' ' || *t > '~')
			{
				s = default_fmt;
				goto attr;
			}
			t++;
		}
	}

	/* %n is evil */
	if (s == NULL || *s == '\0' ||
	    (*s == '*' && (s[1] == '\0' || s[2] == '\0' || strchr(s + 2, 'n'))) ||
	    (*s != '*' && strchr(s, 'n')))
		s = default_fmt;

	/*
	 * Select the attributes if it starts with "*".
	 */
 attr:
	if (*s == '*' && s[1] != '\0')
	{
		switch (s[1])
		{
		case 'd':  *attrptr = AT_BOLD;      break;
		case 'k':  *attrptr = AT_BLINK;     break;
		case 's':  *attrptr = AT_STANDOUT;  break;
		case 'u':  *attrptr = AT_UNDERLINE; break;
		default:   *attrptr = AT_NORMAL;    break;
		}
		s += 2;
	}
	*fmtvarptr = s;
}

/*
 *
 */
	static void
set_charset()
{
	char *s;

#if MSDOS_COMPILER==WIN32C
	/*
	 * If the Windows console is using UTF-8, we'll use it too.
	 */
	if (GetConsoleOutputCP() == CP_UTF8)
		if (icharset("utf-8", 1))
			return;
#endif
	/*
	 * See if environment variable LESSCHARSET is defined.
	 */
	s = lgetenv("LESSCHARSET");
	if (icharset(s, 0))
		return;

	/*
	 * LESSCHARSET is not defined: try LESSCHARDEF.
	 */
	s = lgetenv("LESSCHARDEF");
	if (s != NULL && *s != '\0')
	{
		ichardef(s);
		return;
	}

#if HAVE_LOCALE
#ifdef CODESET
	/*
	 * Try using the codeset name as the charset name.
	 */
	s = nl_langinfo(CODESET);
	if (icharset(s, 1))
		return;
#endif
#endif

#if HAVE_STRSTR
	/*
	 * Check whether LC_ALL, LC_CTYPE or LANG look like UTF-8 is used.
	 */
	if ((s = lgetenv("LC_ALL")) != NULL ||
	    (s = lgetenv("LC_CTYPE")) != NULL ||
	    (s = lgetenv("LANG")) != NULL)
	{
		if (   strstr(s, "UTF-8") != NULL || strstr(s, "utf-8") != NULL
		    || strstr(s, "UTF8")  != NULL || strstr(s, "utf8")  != NULL)
			if (icharset("utf-8", 1))
				return;
	}
#endif

#if HAVE_LOCALE
	/*
	 * Get character definitions from locale functions,
	 * rather than from predefined charset entry.
	 */
	ilocale();
#else
#if MSDOS_COMPILER
	/*
	 * Default to "dos".
	 */
	(void) icharset("dos", 1);
#else
	/*
	 * Default to "latin1".
	 */
	(void) icharset("latin1", 1);
#endif
#endif
}

/*
 * Initialize charset data structures.
 */
	public void
init_charset()
{
	char *s;

#if HAVE_LOCALE
	setlocale(LC_ALL, "");
#endif

	set_charset();

	s = lgetenv("LESSBINFMT");
	setfmt(s, &binfmt, &binattr, "*s<%02X>");
	
	s = lgetenv("LESSUTFBINFMT");
	setfmt(s, &utfbinfmt, &binattr, "<U+%04lX>");
}

/*
 * Is a given character a "binary" character?
 */
	public int
binary_char(c)
	LWCHAR c;
{
	if (utf_mode)
		return (is_ubin_char(c));
	c &= 0377;
	return (chardef[c] & IS_BINARY_CHAR);
}

/*
 * Is a given character a "control" character?
 */
	public int
control_char(c)
	LWCHAR c;
{
	c &= 0377;
	return (chardef[c] & IS_CONTROL_CHAR);
}

/*
 * Return the printable form of a character.
 * For example, in the "ascii" charset '\3' is printed as "^C".
 */
	public char *
prchar(c)
	LWCHAR c;
{
	/* {{ This buffer can be overrun if LESSBINFMT is a long string. }} */
	static char buf[32];

	c &= 0377;
	if ((c < 128 || !utf_mode) && !control_char(c))
		SNPRINTF1(buf, sizeof(buf), "%c", (int) c);
	else if (c == ESC)
		strcpy(buf, "ESC");
#if IS_EBCDIC_HOST
	else if (!binary_char(c) && c < 64)
		SNPRINTF1(buf, sizeof(buf), "^%c",
		/*
		 * This array roughly inverts CONTROL() #defined in less.h,
	 	 * and should be kept in sync with CONTROL() and IBM-1047.
 	 	 */
		"@ABC.I.?...KLMNO"
		"PQRS.JH.XY.."
		"\\]^_"
		"......W[.....EFG"
		"..V....D....TU.Z"[c]);
#else
  	else if (c < 128 && !control_char(c ^ 0100))
  		SNPRINTF1(buf, sizeof(buf), "^%c", (int) (c ^ 0100));
#endif
	else
		SNPRINTF1(buf, sizeof(buf), binfmt, c);
	return (buf);
}

/*
 * Return the printable form of a UTF-8 character.
 */
	public char *
prutfchar(ch)
	LWCHAR ch;
{
	static char buf[32];

	if (ch == ESC)
		strcpy(buf, "ESC");
  	else if (ch < 128 && control_char(ch))
	{
		if (!control_char(ch ^ 0100))
			SNPRINTF1(buf, sizeof(buf), "^%c", ((char) ch) ^ 0100);
		else
			SNPRINTF1(buf, sizeof(buf), binfmt, (char) ch);
	} else if (is_ubin_char(ch))
	{
		SNPRINTF1(buf, sizeof(buf), utfbinfmt, ch);
	} else
	{
		char *p = buf;
		if (ch >= 0x80000000)
			ch = 0xFFFD; /* REPLACEMENT CHARACTER */
		put_wchar(&p, ch);
		*p = '\0';
	}
	return (buf);
}

/*
 * Get the length of a UTF-8 character in bytes.
 */
	public int
utf_len(ch)
	unsigned char ch;
{
	if ((ch & 0x80) == 0)
		return 1;
	if ((ch & 0xE0) == 0xC0)
		return 2;
	if ((ch & 0xF0) == 0xE0)
		return 3;
	if ((ch & 0xF8) == 0xF0)
		return 4;
	if ((ch & 0xFC) == 0xF8)
		return 5;
	if ((ch & 0xFE) == 0xFC)
		return 6;
	/* Invalid UTF-8 encoding. */
	return 1;
}

/*
 * Does the parameter point to the lead byte of a well-formed UTF-8 character?
 */
	public int
is_utf8_well_formed(ss, slen)
	char *ss;
	int slen;
{
	int i;
	int len;
	unsigned char *s = (unsigned char *) ss;

	if (IS_UTF8_INVALID(s[0]))
		return (0);

	len = utf_len(s[0]);
	if (len > slen)
		return (0);
	if (len == 1)
		return (1);
	if (len == 2)
	{
		if (s[0] < 0xC2)
		    return (0);
	} else
	{
		unsigned char mask;
		mask = (~((1 << (8-len)) - 1)) & 0xFF;
		if (s[0] == mask && (s[1] & mask) == 0x80)
			return (0);
	}

	for (i = 1;  i < len;  i++)
		if (!IS_UTF8_TRAIL(s[i]))
			return (0);
	return (1);
}

/*
 * Skip bytes until a UTF-8 lead byte (11xxxxxx) or ASCII byte (0xxxxxxx) is found.
 */
	public void
utf_skip_to_lead(pp, limit)
	char **pp;
	char *limit;
{
	do {
		++(*pp);
	} while (*pp < limit && !IS_UTF8_LEAD((*pp)[0] & 0377) && !IS_ASCII_OCTET((*pp)[0]));
}


/*
 * Get the value of a UTF-8 character.
 */
	public LWCHAR
get_wchar(p)
	constant char *p;
{
	switch (utf_len(p[0]))
	{
	case 1:
	default:
		/* 0xxxxxxx */
		return (LWCHAR)
			(p[0] & 0xFF);
	case 2:
		/* 110xxxxx 10xxxxxx */
		return (LWCHAR) (
			((p[0] & 0x1F) << 6) |
			(p[1] & 0x3F));
	case 3:
		/* 1110xxxx 10xxxxxx 10xxxxxx */
		return (LWCHAR) (
			((p[0] & 0x0F) << 12) |
			((p[1] & 0x3F) << 6) |
			(p[2] & 0x3F));
	case 4:
		/* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
		return (LWCHAR) (
			((p[0] & 0x07) << 18) |
			((p[1] & 0x3F) << 12) | 
			((p[2] & 0x3F) << 6) | 
			(p[3] & 0x3F));
	case 5:
		/* 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
		return (LWCHAR) (
			((p[0] & 0x03) << 24) |
			((p[1] & 0x3F) << 18) | 
			((p[2] & 0x3F) << 12) | 
			((p[3] & 0x3F) << 6) | 
			(p[4] & 0x3F));
	case 6:
		/* 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
		return (LWCHAR) (
			((p[0] & 0x01) << 30) |
			((p[1] & 0x3F) << 24) | 
			((p[2] & 0x3F) << 18) | 
			((p[3] & 0x3F) << 12) | 
			((p[4] & 0x3F) << 6) | 
			(p[5] & 0x3F));
	}
}

/*
 * Store a character into a UTF-8 string.
 */
	public void
put_wchar(pp, ch)
	char **pp;
	LWCHAR ch;
{
	if (!utf_mode || ch < 0x80) 
	{
		/* 0xxxxxxx */
		*(*pp)++ = (char) ch;
	} else if (ch < 0x800)
	{
		/* 110xxxxx 10xxxxxx */
		*(*pp)++ = (char) (0xC0 | ((ch >> 6) & 0x1F));
		*(*pp)++ = (char) (0x80 | (ch & 0x3F));
	} else if (ch < 0x10000)
	{
		/* 1110xxxx 10xxxxxx 10xxxxxx */
		*(*pp)++ = (char) (0xE0 | ((ch >> 12) & 0x0F));
		*(*pp)++ = (char) (0x80 | ((ch >> 6) & 0x3F));
		*(*pp)++ = (char) (0x80 | (ch & 0x3F));
	} else if (ch < 0x200000)
	{
		/* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
		*(*pp)++ = (char) (0xF0 | ((ch >> 18) & 0x07));
		*(*pp)++ = (char) (0x80 | ((ch >> 12) & 0x3F));
		*(*pp)++ = (char) (0x80 | ((ch >> 6) & 0x3F));
		*(*pp)++ = (char) (0x80 | (ch & 0x3F));
	} else if (ch < 0x4000000)
	{
		/* 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
		*(*pp)++ = (char) (0xF0 | ((ch >> 24) & 0x03));
		*(*pp)++ = (char) (0x80 | ((ch >> 18) & 0x3F));
		*(*pp)++ = (char) (0x80 | ((ch >> 12) & 0x3F));
		*(*pp)++ = (char) (0x80 | ((ch >> 6) & 0x3F));
		*(*pp)++ = (char) (0x80 | (ch & 0x3F));
	} else 
	{
		/* 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
		*(*pp)++ = (char) (0xF0 | ((ch >> 30) & 0x01));
		*(*pp)++ = (char) (0x80 | ((ch >> 24) & 0x3F));
		*(*pp)++ = (char) (0x80 | ((ch >> 18) & 0x3F));
		*(*pp)++ = (char) (0x80 | ((ch >> 12) & 0x3F));
		*(*pp)++ = (char) (0x80 | ((ch >> 6) & 0x3F));
		*(*pp)++ = (char) (0x80 | (ch & 0x3F));
	}
}

/*
 * Step forward or backward one character in a string.
 */
	public LWCHAR
step_char(pp, dir, limit)
	char **pp;
	signed int dir;
	constant char *limit;
{
	LWCHAR ch;
	int len;
	char *p = *pp;

	if (!utf_mode)
	{
		/* It's easy if chars are one byte. */
		if (dir > 0)
			ch = (LWCHAR) (unsigned char) ((p < limit) ? *p++ : 0);
		else
			ch = (LWCHAR) (unsigned char) ((p > limit) ? *--p : 0);
	} else if (dir > 0)
	{
		len = utf_len(*p);
		if (p + len > limit)
		{
			ch = 0;
			p = (char *) limit;
		} else
		{
			ch = get_wchar(p);
			p += len;
		}
	} else
	{
		while (p > limit && IS_UTF8_TRAIL(p[-1]))
			p--;
		if (p > limit)
			ch = get_wchar(--p);
		else
			ch = 0;
	}
	*pp = p;
	return ch;
}

/*
 * Unicode characters data
 * Actual data is in the generated *.uni files.
 */

#define DECLARE_RANGE_TABLE_START(name) \
    static struct wchar_range name##_array[] = {
#define DECLARE_RANGE_TABLE_END(name) \
    }; struct wchar_range_table name##_table = { name##_array, sizeof(name##_array)/sizeof(*name##_array) };

DECLARE_RANGE_TABLE_START(compose)
#include "compose.uni"
DECLARE_RANGE_TABLE_END(compose)

DECLARE_RANGE_TABLE_START(ubin)
#include "ubin.uni"
DECLARE_RANGE_TABLE_END(ubin)

DECLARE_RANGE_TABLE_START(wide)
#include "wide.uni"
DECLARE_RANGE_TABLE_END(wide)

DECLARE_RANGE_TABLE_START(fmt)
#include "fmt.uni"
DECLARE_RANGE_TABLE_END(fmt)

/* comb_table is special pairs, not ranges. */
static struct wchar_range comb_table[] = {
	{0x0644,0x0622}, {0x0644,0x0623}, {0x0644,0x0625}, {0x0644,0x0627},
};


	static int
is_in_table(ch, table)
	LWCHAR ch;
	struct wchar_range_table *table;
{
	int hi;
	int lo;

	/* Binary search in the table. */
	if (ch < table->table[0].first)
		return 0;
	lo = 0;
	hi = table->count - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (ch > table->table[mid].last)
			lo = mid + 1;
		else if (ch < table->table[mid].first)
			hi = mid - 1;
		else
			return 1;
	}
	return 0;
}

/*
 * Is a character a UTF-8 composing character?
 * If a composing character follows any char, the two combine into one glyph.
 */
	public int
is_composing_char(ch)
	LWCHAR ch;
{
	return is_in_table(ch, &compose_table) ||
	       (bs_mode != BS_CONTROL && is_in_table(ch, &fmt_table));
}

/*
 * Should this UTF-8 character be treated as binary?
 */
	public int
is_ubin_char(ch)
	LWCHAR ch;
{
	int ubin = is_in_table(ch, &ubin_table) ||
	           (bs_mode == BS_CONTROL && is_in_table(ch, &fmt_table));
#if MSDOS_COMPILER==WIN32C
	if (!ubin && utf_mode == 2 && ch < 0x10000)
	{
		/*
		 * Consider it binary if it can't be converted.
		 */
		BOOL used_default = TRUE;
		WideCharToMultiByte(GetConsoleOutputCP(), WC_NO_BEST_FIT_CHARS, (LPCWSTR) &ch, 1, NULL, 0, NULL, &used_default);
		if (used_default)
			ubin = 1;
	}
#endif
	return ubin;
}

/*
 * Is this a double width UTF-8 character?
 */
	public int
is_wide_char(ch)
	LWCHAR ch;
{
	return is_in_table(ch, &wide_table);
}

/*
 * Is a character a UTF-8 combining character?
 * A combining char acts like an ordinary char, but if it follows
 * a specific char (not any char), the two combine into one glyph.
 */
	public int
is_combining_char(ch1, ch2)
	LWCHAR ch1;
	LWCHAR ch2;
{
	/* The table is small; use linear search. */
	int i;
	for (i = 0;  i < sizeof(comb_table)/sizeof(*comb_table);  i++)
	{
		if (ch1 == comb_table[i].first &&
		    ch2 == comb_table[i].last)
			return 1;
	}
	return 0;
}

