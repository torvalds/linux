/*
 * Copyright (c) 2000-2001, 2004 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: vfscanf.c,v 1.55 2013-11-22 20:51:44 ca Exp $")

#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <setjmp.h>
#include <sm/time.h>
#include <sm/varargs.h>
#include <sm/config.h>
#include <sm/io.h>
#include <sm/signal.h>
#include <sm/clock.h>
#include <sm/string.h>
#include "local.h"

#define BUF		513	/* Maximum length of numeric string. */

/* Flags used during conversion. */
#define LONG		0x01	/* l: long or double */
#define SHORT		0x04	/* h: short */
#define QUAD		0x08	/* q: quad (same as ll) */
#define SUPPRESS	0x10	/* suppress assignment */
#define POINTER		0x20	/* weird %p pointer (`fake hex') */
#define NOSKIP		0x40	/* do not skip blanks */

/*
**  The following are used in numeric conversions only:
**  SIGNOK, NDIGITS, DPTOK, and EXPOK are for floating point;
**  SIGNOK, NDIGITS, PFXOK, and NZDIGITS are for integral.
*/

#define SIGNOK		0x080	/* +/- is (still) legal */
#define NDIGITS		0x100	/* no digits detected */

#define DPTOK		0x200	/* (float) decimal point is still legal */
#define EXPOK		0x400	/* (float) exponent (e+3, etc) still legal */

#define PFXOK		0x200	/* 0x prefix is (still) legal */
#define NZDIGITS	0x400	/* no zero digits detected */

/* Conversion types. */
#define CT_CHAR		0	/* %c conversion */
#define CT_CCL		1	/* %[...] conversion */
#define CT_STRING	2	/* %s conversion */
#define CT_INT		3	/* integer, i.e., strtoll or strtoull */
#define CT_FLOAT	4	/* floating, i.e., strtod */

static void		scanalrm __P((int));
static unsigned char	*sm_sccl __P((char *, unsigned char *));
static jmp_buf		ScanTimeOut;

/*
**  SCANALRM -- handler when timeout activated for sm_io_vfscanf()
**
**  Returns flow of control to where setjmp(ScanTimeOut) was set.
**
**	Parameters:
**		sig -- unused
**
**	Returns:
**		does not return
**
**	Side Effects:
**		returns flow of control to setjmp(ScanTimeOut).
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED0 */
static void
scanalrm(sig)
	int sig;
{
	longjmp(ScanTimeOut, 1);
}

/*
**  SM_VFSCANF -- convert input into data units
**
**	Parameters:
**		fp -- file pointer for input data
**		timeout -- time intvl allowed to complete (milliseconds)
**		fmt0 -- format for finding data units
**		ap -- vectors for memory location for storing data units
**
**	Results:
**		Success: number of data units assigned
**		Failure: SM_IO_EOF
*/

int
sm_vfscanf(fp, timeout, fmt0, ap)
	register SM_FILE_T *fp;
	int SM_NONVOLATILE timeout;
	char const *fmt0;
	va_list SM_NONVOLATILE ap;
{
	register unsigned char *SM_NONVOLATILE fmt = (unsigned char *) fmt0;
	register int c;		/* character from format, or conversion */
	register size_t width;	/* field width, or 0 */
	register char *p;	/* points into all kinds of strings */
	register int n;		/* handy integer */
	register int flags;	/* flags as defined above */
	register char *p0;	/* saves original value of p when necessary */
	int nassigned;		/* number of fields assigned */
	int nread;		/* number of characters consumed from fp */
	int base;		/* base argument to strtoll/strtoull */

	/* conversion function (strtoll/strtoull) */
	ULONGLONG_T (*ccfn) __P((const char *, char **, int));
	char ccltab[256];	/* character class table for %[...] */
	char buf[BUF];		/* buffer for numeric conversions */
	SM_EVENT *evt = NULL;

	/* `basefix' is used to avoid `if' tests in the integer scanner */
	static short basefix[17] =
		{ 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

	if (timeout == SM_TIME_DEFAULT)
		timeout = fp->f_timeout;
	if (timeout == SM_TIME_IMMEDIATE)
	{
		/*
		**  Filling the buffer will take time and we are wanted to
		**  return immediately. So...
		*/

		errno = EAGAIN;
		return SM_IO_EOF;
	}

	if (timeout != SM_TIME_FOREVER)
	{
		if (setjmp(ScanTimeOut) != 0)
		{
			errno = EAGAIN;
			return SM_IO_EOF;
		}

		evt = sm_seteventm(timeout, scanalrm, 0);
	}

	nassigned = 0;
	nread = 0;
	base = 0;		/* XXX just to keep gcc happy */
	ccfn = NULL;		/* XXX just to keep gcc happy */
	for (;;)
	{
		c = *fmt++;
		if (c == 0)
		{
			if (evt != NULL)
				sm_clrevent(evt); /*  undo our timeout */
			return nassigned;
		}
		if (isspace(c))
		{
			while ((fp->f_r > 0 || sm_refill(fp, SM_TIME_FOREVER)
						== 0) &&
			    isspace(*fp->f_p))
				nread++, fp->f_r--, fp->f_p++;
			continue;
		}
		if (c != '%')
			goto literal;
		width = 0;
		flags = 0;

		/*
		**  switch on the format.  continue if done;
		**  break once format type is derived.
		*/

again:		c = *fmt++;
		switch (c)
		{
		  case '%':
literal:
			if (fp->f_r <= 0 && sm_refill(fp, SM_TIME_FOREVER))
				goto input_failure;
			if (*fp->f_p != c)
				goto match_failure;
			fp->f_r--, fp->f_p++;
			nread++;
			continue;

		  case '*':
			flags |= SUPPRESS;
			goto again;
		  case 'h':
			flags |= SHORT;
			goto again;
		  case 'l':
			if (*fmt == 'l')
			{
				fmt++;
				flags |= QUAD;
			}
			else
			{
				flags |= LONG;
			}
			goto again;
		  case 'q':
			flags |= QUAD;
			goto again;

		  case '0': case '1': case '2': case '3': case '4':
		  case '5': case '6': case '7': case '8': case '9':
			width = width * 10 + c - '0';
			goto again;

		/*
		**  Conversions.
		**  Those marked `compat' are for 4.[123]BSD compatibility.
		**
		**  (According to ANSI, E and X formats are supposed
		**  to the same as e and x.  Sorry about that.)
		*/

		  case 'D':	/* compat */
			flags |= LONG;
			/* FALLTHROUGH */
		  case 'd':
			c = CT_INT;
			ccfn = (ULONGLONG_T (*)())sm_strtoll;
			base = 10;
			break;

		  case 'i':
			c = CT_INT;
			ccfn = (ULONGLONG_T (*)())sm_strtoll;
			base = 0;
			break;

		  case 'O':	/* compat */
			flags |= LONG;
			/* FALLTHROUGH */
		  case 'o':
			c = CT_INT;
			ccfn = sm_strtoull;
			base = 8;
			break;

		  case 'u':
			c = CT_INT;
			ccfn = sm_strtoull;
			base = 10;
			break;

		  case 'X':
		  case 'x':
			flags |= PFXOK;	/* enable 0x prefixing */
			c = CT_INT;
			ccfn = sm_strtoull;
			base = 16;
			break;

		  case 'E':
		  case 'G':
		  case 'e':
		  case 'f':
		  case 'g':
			c = CT_FLOAT;
			break;

		  case 's':
			c = CT_STRING;
			break;

		  case '[':
			fmt = sm_sccl(ccltab, fmt);
			flags |= NOSKIP;
			c = CT_CCL;
			break;

		  case 'c':
			flags |= NOSKIP;
			c = CT_CHAR;
			break;

		  case 'p':	/* pointer format is like hex */
			flags |= POINTER | PFXOK;
			c = CT_INT;
			ccfn = sm_strtoull;
			base = 16;
			break;

		  case 'n':
			if (flags & SUPPRESS)	/* ??? */
				continue;
			if (flags & SHORT)
				*SM_VA_ARG(ap, short *) = nread;
			else if (flags & LONG)
				*SM_VA_ARG(ap, long *) = nread;
			else
				*SM_VA_ARG(ap, int *) = nread;
			continue;

		/* Disgusting backwards compatibility hacks.	XXX */
		  case '\0':	/* compat */
			if (evt != NULL)
				sm_clrevent(evt); /*  undo our timeout */
			return SM_IO_EOF;

		  default:	/* compat */
			if (isupper(c))
				flags |= LONG;
			c = CT_INT;
			ccfn = (ULONGLONG_T (*)()) sm_strtoll;
			base = 10;
			break;
		}

		/* We have a conversion that requires input. */
		if (fp->f_r <= 0 && sm_refill(fp, SM_TIME_FOREVER))
			goto input_failure;

		/*
		**  Consume leading white space, except for formats
		**  that suppress this.
		*/

		if ((flags & NOSKIP) == 0)
		{
			while (isspace(*fp->f_p))
			{
				nread++;
				if (--fp->f_r > 0)
					fp->f_p++;
				else if (sm_refill(fp, SM_TIME_FOREVER))
					goto input_failure;
			}
			/*
			**  Note that there is at least one character in
			**  the buffer, so conversions that do not set NOSKIP
			**  can no longer result in an input failure.
			*/
		}

		/* Do the conversion. */
		switch (c)
		{
		  case CT_CHAR:
			/* scan arbitrary characters (sets NOSKIP) */
			if (width == 0)
				width = 1;
			if (flags & SUPPRESS)
			{
				size_t sum = 0;
				for (;;)
				{
					if ((size_t) (n = fp->f_r) < width)
					{
						sum += n;
						width -= n;
						fp->f_p += n;
						if (sm_refill(fp,
							      SM_TIME_FOREVER))
						{
							if (sum == 0)
								goto input_failure;
							break;
						}
					}
					else
					{
						sum += width;
						fp->f_r -= width;
						fp->f_p += width;
						break;
					}
				}
				nread += sum;
			}
			else
			{
				size_t r;

				r = sm_io_read(fp, SM_TIME_FOREVER,
						(void *) SM_VA_ARG(ap, char *),
						width);
				if (r == 0)
					goto input_failure;
				nread += r;
				nassigned++;
			}
			break;

		  case CT_CCL:
			/* scan a (nonempty) character class (sets NOSKIP) */
			if (width == 0)
				width = (size_t)~0;	/* `infinity' */

			/* take only those things in the class */
			if (flags & SUPPRESS)
			{
				n = 0;
				while (ccltab[*fp->f_p] != '\0')
				{
					n++, fp->f_r--, fp->f_p++;
					if (--width == 0)
						break;
					if (fp->f_r <= 0 &&
					    sm_refill(fp, SM_TIME_FOREVER))
					{
						if (n == 0) /* XXX how? */
							goto input_failure;
						break;
					}
				}
				if (n == 0)
					goto match_failure;
			}
			else
			{
				p0 = p = SM_VA_ARG(ap, char *);
				while (ccltab[*fp->f_p] != '\0')
				{
					fp->f_r--;
					*p++ = *fp->f_p++;
					if (--width == 0)
						break;
					if (fp->f_r <= 0 &&
					    sm_refill(fp, SM_TIME_FOREVER))
					{
						if (p == p0)
							goto input_failure;
						break;
					}
				}
				n = p - p0;
				if (n == 0)
					goto match_failure;
				*p = 0;
				nassigned++;
			}
			nread += n;
			break;

		  case CT_STRING:
			/* like CCL, but zero-length string OK, & no NOSKIP */
			if (width == 0)
				width = (size_t)~0;
			if (flags & SUPPRESS)
			{
				n = 0;
				while (!isspace(*fp->f_p))
				{
					n++, fp->f_r--, fp->f_p++;
					if (--width == 0)
						break;
					if (fp->f_r <= 0 &&
					    sm_refill(fp, SM_TIME_FOREVER))
						break;
				}
				nread += n;
			}
			else
			{
				p0 = p = SM_VA_ARG(ap, char *);
				while (!isspace(*fp->f_p))
				{
					fp->f_r--;
					*p++ = *fp->f_p++;
					if (--width == 0)
						break;
					if (fp->f_r <= 0 &&
					    sm_refill(fp, SM_TIME_FOREVER))
						break;
				}
				*p = 0;
				nread += p - p0;
				nassigned++;
			}
			continue;

		  case CT_INT:
			/* scan an integer as if by strtoll/strtoull */
#if SM_CONF_BROKEN_SIZE_T
			if (width == 0 || width > sizeof(buf) - 1)
				width = sizeof(buf) - 1;
#else /* SM_CONF_BROKEN_SIZE_T */
			/* size_t is unsigned, hence this optimisation */
			if (--width > sizeof(buf) - 2)
				width = sizeof(buf) - 2;
			width++;
#endif /* SM_CONF_BROKEN_SIZE_T */
			flags |= SIGNOK | NDIGITS | NZDIGITS;
			for (p = buf; width > 0; width--)
			{
				c = *fp->f_p;

				/*
				**  Switch on the character; `goto ok'
				**  if we accept it as a part of number.
				*/

				switch (c)
				{

				/*
				**  The digit 0 is always legal, but is
				**  special.  For %i conversions, if no
				**  digits (zero or nonzero) have been
				**  scanned (only signs), we will have
				**  base==0.  In that case, we should set
				**  it to 8 and enable 0x prefixing.
				**  Also, if we have not scanned zero digits
				**  before this, do not turn off prefixing
				**  (someone else will turn it off if we
				**  have scanned any nonzero digits).
				*/

				  case '0':
					if (base == 0)
					{
						base = 8;
						flags |= PFXOK;
					}
					if (flags & NZDIGITS)
					    flags &= ~(SIGNOK|NZDIGITS|NDIGITS);
					else
					    flags &= ~(SIGNOK|PFXOK|NDIGITS);
					goto ok;

				/* 1 through 7 always legal */
				  case '1': case '2': case '3':
				  case '4': case '5': case '6': case '7':
					base = basefix[base];
					flags &= ~(SIGNOK | PFXOK | NDIGITS);
					goto ok;

				/* digits 8 and 9 ok iff decimal or hex */
				  case '8': case '9':
					base = basefix[base];
					if (base <= 8)
						break;	/* not legal here */
					flags &= ~(SIGNOK | PFXOK | NDIGITS);
					goto ok;

				/* letters ok iff hex */
				  case 'A': case 'B': case 'C':
				  case 'D': case 'E': case 'F':
				  case 'a': case 'b': case 'c':
				  case 'd': case 'e': case 'f':

					/* no need to fix base here */
					if (base <= 10)
						break;	/* not legal here */
					flags &= ~(SIGNOK | PFXOK | NDIGITS);
					goto ok;

				/* sign ok only as first character */
				  case '+': case '-':
					if (flags & SIGNOK)
					{
						flags &= ~SIGNOK;
						goto ok;
					}
					break;

				/* x ok iff flag still set & 2nd char */
				  case 'x': case 'X':
					if (flags & PFXOK && p == buf + 1)
					{
						base = 16;	/* if %i */
						flags &= ~PFXOK;
						goto ok;
					}
					break;
				}

				/*
				**  If we got here, c is not a legal character
				**  for a number.  Stop accumulating digits.
				*/

				break;
		ok:
				/* c is legal: store it and look at the next. */
				*p++ = c;
				if (--fp->f_r > 0)
					fp->f_p++;
				else if (sm_refill(fp, SM_TIME_FOREVER))
					break;		/* SM_IO_EOF */
			}

			/*
			**  If we had only a sign, it is no good; push
			**  back the sign.  If the number ends in `x',
			**  it was [sign] '0' 'x', so push back the x
			**  and treat it as [sign] '0'.
			*/

			if (flags & NDIGITS)
			{
				if (p > buf)
					(void) sm_io_ungetc(fp, SM_TIME_DEFAULT,
							    *(unsigned char *)--p);
				goto match_failure;
			}
			c = ((unsigned char *)p)[-1];
			if (c == 'x' || c == 'X')
			{
				--p;
				(void) sm_io_ungetc(fp, SM_TIME_DEFAULT, c);
			}
			if ((flags & SUPPRESS) == 0)
			{
				ULONGLONG_T res;

				*p = 0;
				res = (*ccfn)(buf, (char **)NULL, base);
				if (flags & POINTER)
					*SM_VA_ARG(ap, void **) =
					    (void *)(long) res;
				else if (flags & QUAD)
					*SM_VA_ARG(ap, LONGLONG_T *) = res;
				else if (flags & LONG)
					*SM_VA_ARG(ap, long *) = res;
				else if (flags & SHORT)
					*SM_VA_ARG(ap, short *) = res;
				else
					*SM_VA_ARG(ap, int *) = res;
				nassigned++;
			}
			nread += p - buf;
			break;

		  case CT_FLOAT:
			/* scan a floating point number as if by strtod */
			if (width == 0 || width > sizeof(buf) - 1)
				width = sizeof(buf) - 1;
			flags |= SIGNOK | NDIGITS | DPTOK | EXPOK;
			for (p = buf; width; width--)
			{
				c = *fp->f_p;

				/*
				**  This code mimicks the integer conversion
				**  code, but is much simpler.
				*/

				switch (c)
				{

				  case '0': case '1': case '2': case '3':
				  case '4': case '5': case '6': case '7':
				  case '8': case '9':
					flags &= ~(SIGNOK | NDIGITS);
					goto fok;

				  case '+': case '-':
					if (flags & SIGNOK)
					{
						flags &= ~SIGNOK;
						goto fok;
					}
					break;
				  case '.':
					if (flags & DPTOK)
					{
						flags &= ~(SIGNOK | DPTOK);
						goto fok;
					}
					break;
				  case 'e': case 'E':

					/* no exponent without some digits */
					if ((flags&(NDIGITS|EXPOK)) == EXPOK)
					{
						flags =
						    (flags & ~(EXPOK|DPTOK)) |
						    SIGNOK | NDIGITS;
						goto fok;
					}
					break;
				}
				break;
		fok:
				*p++ = c;
				if (--fp->f_r > 0)
					fp->f_p++;
				else if (sm_refill(fp, SM_TIME_FOREVER))
					break;	/* SM_IO_EOF */
			}

			/*
			**  If no digits, might be missing exponent digits
			**  (just give back the exponent) or might be missing
			**  regular digits, but had sign and/or decimal point.
			*/

			if (flags & NDIGITS)
			{
				if (flags & EXPOK)
				{
					/* no digits at all */
					while (p > buf)
						(void) sm_io_ungetc(fp,
							     SM_TIME_DEFAULT,
							     *(unsigned char *)--p);
					goto match_failure;
				}

				/* just a bad exponent (e and maybe sign) */
				c = *(unsigned char *) --p;
				if (c != 'e' && c != 'E')
				{
					(void) sm_io_ungetc(fp, SM_TIME_DEFAULT,
							    c); /* sign */
					c = *(unsigned char *)--p;
				}
				(void) sm_io_ungetc(fp, SM_TIME_DEFAULT, c);
			}
			if ((flags & SUPPRESS) == 0)
			{
				double res;

				*p = 0;
				res = strtod(buf, (char **) NULL);
				if (flags & LONG)
					*SM_VA_ARG(ap, double *) = res;
				else
					*SM_VA_ARG(ap, float *) = res;
				nassigned++;
			}
			nread += p - buf;
			break;
		}
	}
input_failure:
	if (evt != NULL)
		sm_clrevent(evt); /*  undo our timeout */
	return nassigned ? nassigned : -1;
match_failure:
	if (evt != NULL)
		sm_clrevent(evt); /*  undo our timeout */
	return nassigned;
}

/*
**  SM_SCCL -- sequenced character comparison list
**
**  Fill in the given table from the scanset at the given format
**  (just after `[').  Return a pointer to the character past the
**  closing `]'.  The table has a 1 wherever characters should be
**  considered part of the scanset.
**
**	Parameters:
**		tab -- array flagging "active" char's to match (returned)
**		fmt -- character list (within "[]")
**
**	Results:
*/

static unsigned char *
sm_sccl(tab, fmt)
	register char *tab;
	register unsigned char *fmt;
{
	register int c, n, v;

	/* first `clear' the whole table */
	c = *fmt++;		/* first char hat => negated scanset */
	if (c == '^')
	{
		v = 1;		/* default => accept */
		c = *fmt++;	/* get new first char */
	}
	else
		v = 0;		/* default => reject */

	/* should probably use memset here */
	for (n = 0; n < 256; n++)
		tab[n] = v;
	if (c == 0)
		return fmt - 1;	/* format ended before closing ] */

	/*
	**  Now set the entries corresponding to the actual scanset
	**  to the opposite of the above.
	**
	**  The first character may be ']' (or '-') without being special;
	**  the last character may be '-'.
	*/

	v = 1 - v;
	for (;;)
	{
		tab[c] = v;		/* take character c */
doswitch:
		n = *fmt++;		/* and examine the next */
		switch (n)
		{

		  case 0:			/* format ended too soon */
			return fmt - 1;

		  case '-':
			/*
			**  A scanset of the form
			**	[01+-]
			**  is defined as `the digit 0, the digit 1,
			**  the character +, the character -', but
			**  the effect of a scanset such as
			**	[a-zA-Z0-9]
			**  is implementation defined.  The V7 Unix
			**  scanf treats `a-z' as `the letters a through
			**  z', but treats `a-a' as `the letter a, the
			**  character -, and the letter a'.
			**
			**  For compatibility, the `-' is not considerd
			**  to define a range if the character following
			**  it is either a close bracket (required by ANSI)
			**  or is not numerically greater than the character
			**  we just stored in the table (c).
			*/

			n = *fmt;
			if (n == ']' || n < c)
			{
				c = '-';
				break;	/* resume the for(;;) */
			}
			fmt++;
			do
			{
				/* fill in the range */
				tab[++c] = v;
			} while (c < n);
#if 1	/* XXX another disgusting compatibility hack */

			/*
			**  Alas, the V7 Unix scanf also treats formats
			**  such as [a-c-e] as `the letters a through e'.
			**  This too is permitted by the standard....
			*/

			goto doswitch;
#else
			c = *fmt++;
			if (c == 0)
				return fmt - 1;
			if (c == ']')
				return fmt;
			break;
#endif

		  case ']':		/* end of scanset */
			return fmt;

		  default:		/* just another character */
			c = n;
			break;
		}
	}
	/* NOTREACHED */
}
