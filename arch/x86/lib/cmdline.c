// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Misc librarized functions for cmdline poking.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/setup.h>
#include <asm/cmdline.h>

static inline int myisspace(u8 c)
{
	return c <= ' ';	/* Close enough approximation */
}

/*
 * Find a boolean option (like quiet,noapic,nosmp....)
 *
 * @cmdline: the cmdline string
 * @max_cmdline_size: the maximum size of cmdline
 * @option: option string to look for
 *
 * Returns the position of that @option (starts counting with 1)
 * or 0 on not found.  @option will only be found if it is found
 * as an entire word in @cmdline.  For instance, if @option="car"
 * then a cmdline which contains "cart" will not match.
 */
static int
__cmdline_find_option_bool(const char *cmdline, int max_cmdline_size,
			   const char *option)
{
	char c;
	int pos = 0, wstart = 0;
	const char *opptr = NULL;
	enum {
		st_wordstart = 0,	/* Start of word/after whitespace */
		st_wordcmp,	/* Comparing this word */
		st_wordskip,	/* Miscompare, skip */
	} state = st_wordstart;

	if (!cmdline)
		return -1;      /* No command line */

	/*
	 * This 'pos' check ensures we do not overrun
	 * a non-NULL-terminated 'cmdline'
	 */
	while (pos < max_cmdline_size) {
		c = *(char *)cmdline++;
		pos++;

		switch (state) {
		case st_wordstart:
			if (!c)
				return 0;
			else if (myisspace(c))
				break;

			state = st_wordcmp;
			opptr = option;
			wstart = pos;
			fallthrough;

		case st_wordcmp:
			if (!*opptr) {
				/*
				 * We matched all the way to the end of the
				 * option we were looking for.  If the
				 * command-line has a space _or_ ends, then
				 * we matched!
				 */
				if (!c || myisspace(c))
					return wstart;
				/*
				 * We hit the end of the option, but _not_
				 * the end of a word on the cmdline.  Not
				 * a match.
				 */
			} else if (!c) {
				/*
				 * Hit the NULL terminator on the end of
				 * cmdline.
				 */
				return 0;
			} else if (c == *opptr++) {
				/*
				 * We are currently matching, so continue
				 * to the next character on the cmdline.
				 */
				break;
			}
			state = st_wordskip;
			fallthrough;

		case st_wordskip:
			if (!c)
				return 0;
			else if (myisspace(c))
				state = st_wordstart;
			break;
		}
	}

	return 0;	/* Buffer overrun */
}

/*
 * Find a non-boolean option (i.e. option=argument). In accordance with
 * standard Linux practice, if this option is repeated, this returns the
 * last instance on the command line.
 *
 * @cmdline: the cmdline string
 * @max_cmdline_size: the maximum size of cmdline
 * @option: option string to look for
 * @buffer: memory buffer to return the option argument
 * @bufsize: size of the supplied memory buffer
 *
 * Returns the length of the argument (regardless of if it was
 * truncated to fit in the buffer), or -1 on not found.
 */
static int
__cmdline_find_option(const char *cmdline, int max_cmdline_size,
		      const char *option, char *buffer, int bufsize)
{
	char c;
	int pos = 0, len = -1;
	const char *opptr = NULL;
	char *bufptr = buffer;
	enum {
		st_wordstart = 0,	/* Start of word/after whitespace */
		st_wordcmp,	/* Comparing this word */
		st_wordskip,	/* Miscompare, skip */
		st_bufcpy,	/* Copying this to buffer */
	} state = st_wordstart;

	if (!cmdline)
		return -1;      /* No command line */

	/*
	 * This 'pos' check ensures we do not overrun
	 * a non-NULL-terminated 'cmdline'
	 */
	while (pos++ < max_cmdline_size) {
		c = *(char *)cmdline++;
		if (!c)
			break;

		switch (state) {
		case st_wordstart:
			if (myisspace(c))
				break;

			state = st_wordcmp;
			opptr = option;
			fallthrough;

		case st_wordcmp:
			if ((c == '=') && !*opptr) {
				/*
				 * We matched all the way to the end of the
				 * option we were looking for, prepare to
				 * copy the argument.
				 */
				len = 0;
				bufptr = buffer;
				state = st_bufcpy;
				break;
			} else if (c == *opptr++) {
				/*
				 * We are currently matching, so continue
				 * to the next character on the cmdline.
				 */
				break;
			}
			state = st_wordskip;
			fallthrough;

		case st_wordskip:
			if (myisspace(c))
				state = st_wordstart;
			break;

		case st_bufcpy:
			if (myisspace(c)) {
				state = st_wordstart;
			} else {
				/*
				 * Increment len, but don't overrun the
				 * supplied buffer and leave room for the
				 * NULL terminator.
				 */
				if (++len < bufsize)
					*bufptr++ = c;
			}
			break;
		}
	}

	if (bufsize)
		*bufptr = '\0';

	return len;
}

int cmdline_find_option_bool(const char *cmdline, const char *option)
{
	return __cmdline_find_option_bool(cmdline, COMMAND_LINE_SIZE, option);
}

int cmdline_find_option(const char *cmdline, const char *option, char *buffer,
			int bufsize)
{
	return __cmdline_find_option(cmdline, COMMAND_LINE_SIZE, option,
				     buffer, bufsize);
}
