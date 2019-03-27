/*
 * Copyright (c) 2000-2001, 2004 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 * Copyright (c) 1990
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
SM_IDSTR(id, "@(#)$Id: vfprintf.c,v 1.55 2013-11-22 20:51:44 ca Exp $")

/*
**  Overall:
**  Actual printing innards.
**  This code is large and complicated...
*/

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sm/config.h>
#include <sm/varargs.h>
#include <sm/io.h>
#include <sm/heap.h>
#include <sm/conf.h>
#include "local.h"
#include "fvwrite.h"

static int	sm_bprintf __P((SM_FILE_T *, const char *, va_list));
static void	sm_find_arguments __P((const char *, va_list , va_list **));
static void	sm_grow_type_table_x __P((unsigned char **, int *));
static int	sm_print __P((SM_FILE_T *, int, struct sm_uio *));

/*
**  SM_PRINT -- print/flush to the file
**
**  Flush out all the vectors defined by the given uio,
**  then reset it so that it can be reused.
**
**	Parameters:
**		fp -- file pointer
**		timeout -- time to complete operation (milliseconds)
**		uio -- vector list of memory locations of data for printing
**
**	Results:
**		Success: 0 (zero)
**		Failure:
*/

static int
sm_print(fp, timeout, uio)
	SM_FILE_T *fp;
	int timeout;
	register struct sm_uio *uio;
{
	register int err;

	if (uio->uio_resid == 0)
	{
		uio->uio_iovcnt = 0;
		return 0;
	}
	err = sm_fvwrite(fp, timeout, uio);
	uio->uio_resid = 0;
	uio->uio_iovcnt = 0;
	return err;
}

/*
**  SM_BPRINTF -- allow formating to an unbuffered file.
**
**  Helper function for `fprintf to unbuffered unix file': creates a
**  temporary buffer (via a "fake" file pointer).
**  We only work on write-only files; this avoids
**  worries about ungetc buffers and so forth.
**
**	Parameters:
**		fp -- the file to send the o/p to
**		fmt -- format instructions for the o/p
**		ap -- vectors of data units used for formating
**
**	Results:
**		Failure: SM_IO_EOF and errno set
**		Success: number of data units used in the formating
**
**	Side effects:
**		formatted o/p can be SM_IO_BUFSIZ length maximum
*/

static int
sm_bprintf(fp, fmt, ap)
	SM_FILE_T *fp;
	const char *fmt;
	SM_VA_LOCAL_DECL
{
	int ret;
	SM_FILE_T fake;
	unsigned char buf[SM_IO_BUFSIZ];
	extern const char SmFileMagic[];

	/* copy the important variables */
	fake.sm_magic = SmFileMagic;
	fake.f_timeout = SM_TIME_FOREVER;
	fake.f_timeoutstate = SM_TIME_BLOCK;
	fake.f_flags = fp->f_flags & ~SMNBF;
	fake.f_file = fp->f_file;
	fake.f_cookie = fp->f_cookie;
	fake.f_write = fp->f_write;
	fake.f_close = NULL;
	fake.f_open = NULL;
	fake.f_read = NULL;
	fake.f_seek = NULL;
	fake.f_setinfo = fake.f_getinfo = NULL;
	fake.f_type = "sm_bprintf:fake";

	/* set up the buffer */
	fake.f_bf.smb_base = fake.f_p = buf;
	fake.f_bf.smb_size = fake.f_w = sizeof(buf);
	fake.f_lbfsize = 0;	/* not actually used, but Just In Case */

	/* do the work, then copy any error status */
	ret = sm_io_vfprintf(&fake, SM_TIME_FOREVER, fmt, ap);
	if (ret >= 0 && sm_io_flush(&fake, SM_TIME_FOREVER))
		ret = SM_IO_EOF;	/* errno set by sm_io_flush */
	if (fake.f_flags & SMERR)
		fp->f_flags |= SMERR;
	return ret;
}


#define BUF		40

#define STATIC_ARG_TBL_SIZE 8	/* Size of static argument table. */


/* Macros for converting digits to letters and vice versa */
#define to_digit(c)	((c) - '0')
#define is_digit(c)	((unsigned) to_digit(c) <= 9)
#define to_char(n)	((char) (n) + '0')

/* Flags used during conversion. */
#define ALT		0x001		/* alternate form */
#define HEXPREFIX	0x002		/* add 0x or 0X prefix */
#define LADJUST		0x004		/* left adjustment */
#define LONGINT		0x010		/* long integer */
#define QUADINT		0x020		/* quad integer */
#define SHORTINT	0x040		/* short integer */
#define ZEROPAD		0x080		/* zero (as opposed to blank) pad */
#define FPT		0x100		/* Floating point number */

/*
**  SM_IO_VFPRINTF -- performs actual formating for o/p
**
**	Parameters:
**		fp -- file pointer for o/p
**		timeout -- time to complete the print
**		fmt0 -- formating directives
**		ap -- vectors with data units for formating
**
**	Results:
**		Success: number of data units used for formatting
**		Failure: SM_IO_EOF and sets errno
*/

int
sm_io_vfprintf(fp, timeout, fmt0, ap)
	SM_FILE_T *fp;
	int timeout;
	const char *fmt0;
	SM_VA_LOCAL_DECL
{
	register char *fmt;	/* format string */
	register int ch;	/* character from fmt */
	register int n, m, n2;	/* handy integers (short term usage) */
	register char *cp;	/* handy char pointer (short term usage) */
	register struct sm_iov *iovp;/* for PRINT macro */
	register int flags;	/* flags as above */
	int ret;		/* return value accumulator */
	int width;		/* width from format (%8d), or 0 */
	int prec;		/* precision from format (%.3d), or -1 */
	char sign;		/* sign prefix (' ', '+', '-', or \0) */
	wchar_t wc;
	ULONGLONG_T _uquad;	/* integer arguments %[diouxX] */
	enum { OCT, DEC, HEX } base;/* base for [diouxX] conversion */
	int dprec;		/* a copy of prec if [diouxX], 0 otherwise */
	int realsz;		/* field size expanded by dprec */
	int size;		/* size of converted field or string */
	char *xdigs="0123456789abcdef"; /* digits for [xX] conversion */
#define NIOV 8
	struct sm_uio uio;	/* output information: summary */
	struct sm_iov iov[NIOV];/* ... and individual io vectors */
	char buf[BUF];		/* space for %c, %[diouxX], %[eEfgG] */
	char ox[2];		/* space for 0x hex-prefix */
	va_list *argtable;	/* args, built due to positional arg */
	va_list statargtable[STATIC_ARG_TBL_SIZE];
	int nextarg;		/* 1-based argument index */
	va_list orgap;		/* original argument pointer */

	/*
	**  Choose PADSIZE to trade efficiency vs. size.  If larger printf
	**  fields occur frequently, increase PADSIZE and make the initialisers
	**  below longer.
	*/
#define PADSIZE	16		/* pad chunk size */
	static char blanks[PADSIZE] =
	 {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
	static char zeroes[PADSIZE] =
	 {'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'};

	/*
	**  BEWARE, these `goto error' on error, and PAD uses `n'.
	*/
#define PRINT(ptr, len) do { \
	iovp->iov_base = (ptr); \
	iovp->iov_len = (len); \
	uio.uio_resid += (len); \
	iovp++; \
	if (++uio.uio_iovcnt >= NIOV) \
	{ \
		if (sm_print(fp, timeout, &uio)) \
			goto error; \
		iovp = iov; \
	} \
} while (0)
#define PAD(howmany, with) do \
{ \
	if ((n = (howmany)) > 0) \
	{ \
		while (n > PADSIZE) { \
			PRINT(with, PADSIZE); \
			n -= PADSIZE; \
		} \
		PRINT(with, n); \
	} \
} while (0)
#define FLUSH() do \
{ \
	if (uio.uio_resid && sm_print(fp, timeout, &uio)) \
		goto error; \
	uio.uio_iovcnt = 0; \
	iovp = iov; \
} while (0)

	/*
	**  To extend shorts properly, we need both signed and unsigned
	**  argument extraction methods.
	*/
#define SARG() \
	(flags&QUADINT ? SM_VA_ARG(ap, LONGLONG_T) : \
	    flags&LONGINT ? GETARG(long) : \
	    flags&SHORTINT ? (long) (short) GETARG(int) : \
	    (long) GETARG(int))
#define UARG() \
	(flags&QUADINT ? SM_VA_ARG(ap, ULONGLONG_T) : \
	    flags&LONGINT ? GETARG(unsigned long) : \
	    flags&SHORTINT ? (unsigned long) (unsigned short) GETARG(int) : \
	    (unsigned long) GETARG(unsigned int))

	/*
	**  Get * arguments, including the form *nn$.  Preserve the nextarg
	**  that the argument can be gotten once the type is determined.
	*/
#define GETASTER(val) \
	n2 = 0; \
	cp = fmt; \
	while (is_digit(*cp)) \
	{ \
		n2 = 10 * n2 + to_digit(*cp); \
		cp++; \
	} \
	if (*cp == '$') \
	{ \
		int hold = nextarg; \
		if (argtable == NULL) \
		{ \
			argtable = statargtable; \
			sm_find_arguments(fmt0, orgap, &argtable); \
		} \
		nextarg = n2; \
		val = GETARG(int); \
		nextarg = hold; \
		fmt = ++cp; \
	} \
	else \
	{ \
		val = GETARG(int); \
	}

/*
**  Get the argument indexed by nextarg.   If the argument table is
**  built, use it to get the argument.  If its not, get the next
**  argument (and arguments must be gotten sequentially).
*/

#if SM_VA_STD
# define GETARG(type) \
	(((argtable != NULL) ? (void) (ap = argtable[nextarg]) : (void) 0), \
	 nextarg++, SM_VA_ARG(ap, type))
#else /* SM_VA_STD */
# define GETARG(type) \
	((argtable != NULL) ? (*((type*)(argtable[nextarg++]))) : \
			      (nextarg++, SM_VA_ARG(ap, type)))
#endif /* SM_VA_STD */

	/* sorry, fprintf(read_only_file, "") returns SM_IO_EOF, not 0 */
	if (cantwrite(fp))
	{
		errno = EBADF;
		return SM_IO_EOF;
	}

	/* optimise fprintf(stderr) (and other unbuffered Unix files) */
	if ((fp->f_flags & (SMNBF|SMWR|SMRW)) == (SMNBF|SMWR) &&
	    fp->f_file >= 0)
		return sm_bprintf(fp, fmt0, ap);

	fmt = (char *) fmt0;
	argtable = NULL;
	nextarg = 1;
	SM_VA_COPY(orgap, ap);
	uio.uio_iov = iovp = iov;
	uio.uio_resid = 0;
	uio.uio_iovcnt = 0;
	ret = 0;

	/* Scan the format for conversions (`%' character). */
	for (;;)
	{
		cp = fmt;
		n = 0;
		while ((wc = *fmt) != '\0')
		{
			if (wc == '%')
			{
				n = 1;
				break;
			}
			fmt++;
		}
		if ((m = fmt - cp) != 0)
		{
			PRINT(cp, m);
			ret += m;
		}
		if (n <= 0)
			goto done;
		fmt++;		/* skip over '%' */

		flags = 0;
		dprec = 0;
		width = 0;
		prec = -1;
		sign = '\0';

rflag:		ch = *fmt++;
reswitch:	switch (ch)
		{
		  case ' ':

			/*
			**  ``If the space and + flags both appear, the space
			**  flag will be ignored.''
			**	-- ANSI X3J11
			*/

			if (!sign)
				sign = ' ';
			goto rflag;
		  case '#':
			flags |= ALT;
			goto rflag;
		  case '*':

			/*
			**  ``A negative field width argument is taken as a
			**  - flag followed by a positive field width.''
			**	-- ANSI X3J11
			**  They don't exclude field widths read from args.
			*/

			GETASTER(width);
			if (width >= 0)
				goto rflag;
			width = -width;
			/* FALLTHROUGH */
		  case '-':
			flags |= LADJUST;
			goto rflag;
		  case '+':
			sign = '+';
			goto rflag;
		  case '.':
			if ((ch = *fmt++) == '*')
			{
				GETASTER(n);
				prec = n < 0 ? -1 : n;
				goto rflag;
			}
			n = 0;
			while (is_digit(ch))
			{
				n = 10 * n + to_digit(ch);
				ch = *fmt++;
			}
			if (ch == '$')
			{
				nextarg = n;
				if (argtable == NULL)
				{
					argtable = statargtable;
					sm_find_arguments(fmt0, orgap,
					    &argtable);
				}
				goto rflag;
			}
			prec = n < 0 ? -1 : n;
			goto reswitch;
		  case '0':

			/*
			**  ``Note that 0 is taken as a flag, not as the
			**  beginning of a field width.''
			**	-- ANSI X3J11
			*/

			flags |= ZEROPAD;
			goto rflag;
		  case '1': case '2': case '3': case '4':
		  case '5': case '6': case '7': case '8': case '9':
			n = 0;
			do
			{
				n = 10 * n + to_digit(ch);
				ch = *fmt++;
			} while (is_digit(ch));
			if (ch == '$')
			{
				nextarg = n;
				if (argtable == NULL)
				{
					argtable = statargtable;
					sm_find_arguments(fmt0, orgap,
					    &argtable);
				}
				goto rflag;
			}
			width = n;
			goto reswitch;
		  case 'h':
			flags |= SHORTINT;
			goto rflag;
		  case 'l':
			if (*fmt == 'l')
			{
				fmt++;
				flags |= QUADINT;
			}
			else
			{
				flags |= LONGINT;
			}
			goto rflag;
		  case 'q':
			flags |= QUADINT;
			goto rflag;
		  case 'c':
			*(cp = buf) = GETARG(int);
			size = 1;
			sign = '\0';
			break;
		  case 'D':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		  case 'd':
		  case 'i':
			_uquad = SARG();
			if ((LONGLONG_T) _uquad < 0)
			{
				_uquad = -(LONGLONG_T) _uquad;
				sign = '-';
			}
			base = DEC;
			goto number;
		  case 'e':
		  case 'E':
		  case 'f':
		  case 'g':
		  case 'G':
			{
				double val;
				char *p;
				char fmt[16];
				char out[150];
				size_t len;

				/*
				**  This code implements floating point output
				**  in the most portable manner possible,
				**  relying only on 'sprintf' as defined by
				**  the 1989 ANSI C standard.
				**  We silently cap width and precision
				**  at 120, to avoid buffer overflow.
				*/

				val = GETARG(double);

				p = fmt;
				*p++ = '%';
				if (sign)
					*p++ = sign;
				if (flags & ALT)
					*p++ = '#';
				if (flags & LADJUST)
					*p++ = '-';
				if (flags & ZEROPAD)
					*p++ = '0';
				*p++ = '*';
				if (prec >= 0)
				{
					*p++ = '.';
					*p++ = '*';
				}
				*p++ = ch;
				*p = '\0';

				if (width > 120)
					width = 120;
				if (prec > 120)
					prec = 120;
				if (prec >= 0)
#if HASSNPRINTF
					snprintf(out, sizeof(out), fmt, width,
						prec, val);
#else /* HASSNPRINTF */
					sprintf(out, fmt, width, prec, val);
#endif /* HASSNPRINTF */
				else
#if HASSNPRINTF
					snprintf(out, sizeof(out), fmt, width,
						val);
#else /* HASSNPRINTF */
					sprintf(out, fmt, width, val);
#endif /* HASSNPRINTF */
				len = strlen(out);
				PRINT(out, len);
				FLUSH();
				continue;
			}
		case 'n':
			if (flags & QUADINT)
				*GETARG(LONGLONG_T *) = ret;
			else if (flags & LONGINT)
				*GETARG(long *) = ret;
			else if (flags & SHORTINT)
				*GETARG(short *) = ret;
			else
				*GETARG(int *) = ret;
			continue;	/* no output */
		  case 'O':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		  case 'o':
			_uquad = UARG();
			base = OCT;
			goto nosign;
		  case 'p':

			/*
			**  ``The argument shall be a pointer to void.  The
			**  value of the pointer is converted to a sequence
			**  of printable characters, in an implementation-
			**  defined manner.''
			**	-- ANSI X3J11
			*/

			/* NOSTRICT */
			{
				union
				{
					void *p;
					ULONGLONG_T ll;
					unsigned long l;
					unsigned i;
				} u;
				u.p = GETARG(void *);
				if (sizeof(void *) == sizeof(ULONGLONG_T))
					_uquad = u.ll;
				else if (sizeof(void *) == sizeof(long))
					_uquad = u.l;
				else
					_uquad = u.i;
			}
			base = HEX;
			xdigs = "0123456789abcdef";
			flags |= HEXPREFIX;
			ch = 'x';
			goto nosign;
		  case 's':
			if ((cp = GETARG(char *)) == NULL)
				cp = "(null)";
			if (prec >= 0)
			{
				/*
				**  can't use strlen; can only look for the
				**  NUL in the first `prec' characters, and
				**  strlen() will go further.
				*/

				char *p = memchr(cp, 0, prec);

				if (p != NULL)
				{
					size = p - cp;
					if (size > prec)
						size = prec;
				}
				else
					size = prec;
			}
			else
				size = strlen(cp);
			sign = '\0';
			break;
		  case 'U':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		  case 'u':
			_uquad = UARG();
			base = DEC;
			goto nosign;
		  case 'X':
			xdigs = "0123456789ABCDEF";
			goto hex;
		  case 'x':
			xdigs = "0123456789abcdef";
hex:			_uquad = UARG();
			base = HEX;
			/* leading 0x/X only if non-zero */
			if (flags & ALT && _uquad != 0)
				flags |= HEXPREFIX;

			/* unsigned conversions */
nosign:			sign = '\0';

			/*
			**  ``... diouXx conversions ... if a precision is
			**  specified, the 0 flag will be ignored.''
			**	-- ANSI X3J11
			*/

number:			if ((dprec = prec) >= 0)
				flags &= ~ZEROPAD;

			/*
			**  ``The result of converting a zero value with an
			**  explicit precision of zero is no characters.''
			**	-- ANSI X3J11
			*/

			cp = buf + BUF;
			if (_uquad != 0 || prec != 0)
			{
				/*
				**  Unsigned mod is hard, and unsigned mod
				**  by a constant is easier than that by
				**  a variable; hence this switch.
				*/

				switch (base)
				{
				  case OCT:
					do
					{
						*--cp = to_char(_uquad & 7);
						_uquad >>= 3;
					} while (_uquad);
					/* handle octal leading 0 */
					if (flags & ALT && *cp != '0')
						*--cp = '0';
					break;

				  case DEC:
					/* many numbers are 1 digit */
					while (_uquad >= 10)
					{
						*--cp = to_char(_uquad % 10);
						_uquad /= 10;
					}
					*--cp = to_char(_uquad);
					break;

				  case HEX:
					do
					{
						*--cp = xdigs[_uquad & 15];
						_uquad >>= 4;
					} while (_uquad);
					break;

				  default:
					cp = "bug in sm_io_vfprintf: bad base";
					size = strlen(cp);
					goto skipsize;
				}
			}
			size = buf + BUF - cp;
		  skipsize:
			break;
		  default:	/* "%?" prints ?, unless ? is NUL */
			if (ch == '\0')
				goto done;
			/* pretend it was %c with argument ch */
			cp = buf;
			*cp = ch;
			size = 1;
			sign = '\0';
			break;
		}

		/*
		**  All reasonable formats wind up here.  At this point, `cp'
		**  points to a string which (if not flags&LADJUST) should be
		**  padded out to `width' places.  If flags&ZEROPAD, it should
		**  first be prefixed by any sign or other prefix; otherwise,
		**  it should be blank padded before the prefix is emitted.
		**  After any left-hand padding and prefixing, emit zeroes
		**  required by a decimal [diouxX] precision, then print the
		**  string proper, then emit zeroes required by any leftover
		**  floating precision; finally, if LADJUST, pad with blanks.
		**
		**  Compute actual size, so we know how much to pad.
		**  size excludes decimal prec; realsz includes it.
		*/

		realsz = dprec > size ? dprec : size;
		if (sign)
			realsz++;
		else if (flags & HEXPREFIX)
			realsz+= 2;

		/* right-adjusting blank padding */
		if ((flags & (LADJUST|ZEROPAD)) == 0)
			PAD(width - realsz, blanks);

		/* prefix */
		if (sign)
		{
			PRINT(&sign, 1);
		}
		else if (flags & HEXPREFIX)
		{
			ox[0] = '0';
			ox[1] = ch;
			PRINT(ox, 2);
		}

		/* right-adjusting zero padding */
		if ((flags & (LADJUST|ZEROPAD)) == ZEROPAD)
			PAD(width - realsz, zeroes);

		/* leading zeroes from decimal precision */
		PAD(dprec - size, zeroes);

		/* the string or number proper */
		PRINT(cp, size);
		/* left-adjusting padding (always blank) */
		if (flags & LADJUST)
			PAD(width - realsz, blanks);

		/* finally, adjust ret */
		ret += width > realsz ? width : realsz;

		FLUSH();	/* copy out the I/O vectors */
	}
done:
	FLUSH();
error:
	if ((argtable != NULL) && (argtable != statargtable))
		sm_free(argtable);
	return sm_error(fp) ? SM_IO_EOF : ret;
	/* NOTREACHED */
}

/* Type ids for argument type table. */
#define T_UNUSED	0
#define T_SHORT		1
#define T_U_SHORT	2
#define TP_SHORT	3
#define T_INT		4
#define T_U_INT		5
#define TP_INT		6
#define T_LONG		7
#define T_U_LONG	8
#define TP_LONG		9
#define T_QUAD		10
#define T_U_QUAD	11
#define TP_QUAD		12
#define T_DOUBLE	13
#define TP_CHAR		15
#define TP_VOID		16

/*
**  SM_FIND_ARGUMENTS -- find all args when a positional parameter is found.
**
**  Find all arguments when a positional parameter is encountered.  Returns a
**  table, indexed by argument number, of pointers to each arguments.  The
**  initial argument table should be an array of STATIC_ARG_TBL_SIZE entries.
**  It will be replaced with a malloc-ed one if it overflows.
**
**	Parameters:
**		fmt0 -- formating directives
**		ap -- vector list of data unit for formating consumption
**		argtable -- an indexable table (returned) of 'ap'
**
**	Results:
**		none.
*/

static void
sm_find_arguments(fmt0, ap, argtable)
	const char *fmt0;
	SM_VA_LOCAL_DECL
	va_list **argtable;
{
	register char *fmt;	/* format string */
	register int ch;	/* character from fmt */
	register int n, n2;	/* handy integer (short term usage) */
	register char *cp;	/* handy char pointer (short term usage) */
	register int flags;	/* flags as above */
	unsigned char *typetable; /* table of types */
	unsigned char stattypetable[STATIC_ARG_TBL_SIZE];
	int tablesize;		/* current size of type table */
	int tablemax;		/* largest used index in table */
	int nextarg;		/* 1-based argument index */

	/* Add an argument type to the table, expanding if necessary. */
#define ADDTYPE(type) \
	((nextarg >= tablesize) ? \
		(sm_grow_type_table_x(&typetable, &tablesize), 0) : 0, \
	typetable[nextarg++] = type, \
	(nextarg > tablemax) ? tablemax = nextarg : 0)

#define ADDSARG() \
	((flags & LONGINT) ? ADDTYPE(T_LONG) : \
		((flags & SHORTINT) ? ADDTYPE(T_SHORT) : ADDTYPE(T_INT)))

#define ADDUARG() \
	((flags & LONGINT) ? ADDTYPE(T_U_LONG) : \
		((flags & SHORTINT) ? ADDTYPE(T_U_SHORT) : ADDTYPE(T_U_INT)))

	/* Add * arguments to the type array. */
#define ADDASTER() \
	n2 = 0; \
	cp = fmt; \
	while (is_digit(*cp)) \
	{ \
		n2 = 10 * n2 + to_digit(*cp); \
		cp++; \
	} \
	if (*cp == '$') \
	{ \
		int hold = nextarg; \
		nextarg = n2; \
		ADDTYPE (T_INT); \
		nextarg = hold; \
		fmt = ++cp; \
	} \
	else \
	{ \
		ADDTYPE (T_INT); \
	}
	fmt = (char *) fmt0;
	typetable = stattypetable;
	tablesize = STATIC_ARG_TBL_SIZE;
	tablemax = 0;
	nextarg = 1;
	(void) memset(typetable, T_UNUSED, STATIC_ARG_TBL_SIZE);

	/* Scan the format for conversions (`%' character). */
	for (;;)
	{
		for (cp = fmt; (ch = *fmt) != '\0' && ch != '%'; fmt++)
			/* void */;
		if (ch == '\0')
			goto done;
		fmt++;		/* skip over '%' */

		flags = 0;

rflag:		ch = *fmt++;
reswitch:	switch (ch)
		{
		  case ' ':
		  case '#':
			goto rflag;
		  case '*':
			ADDASTER();
			goto rflag;
		  case '-':
		  case '+':
			goto rflag;
		  case '.':
			if ((ch = *fmt++) == '*')
			{
				ADDASTER();
				goto rflag;
			}
			while (is_digit(ch))
			{
				ch = *fmt++;
			}
			goto reswitch;
		  case '0':
			goto rflag;
		  case '1': case '2': case '3': case '4':
		  case '5': case '6': case '7': case '8': case '9':
			n = 0;
			do
			{
				n = 10 * n + to_digit(ch);
				ch = *fmt++;
			} while (is_digit(ch));
			if (ch == '$')
			{
				nextarg = n;
				goto rflag;
			}
			goto reswitch;
		  case 'h':
			flags |= SHORTINT;
			goto rflag;
		  case 'l':
			flags |= LONGINT;
			goto rflag;
		  case 'q':
			flags |= QUADINT;
			goto rflag;
		  case 'c':
			ADDTYPE(T_INT);
			break;
		  case 'D':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		  case 'd':
		  case 'i':
			if (flags & QUADINT)
			{
				ADDTYPE(T_QUAD);
			}
			else
			{
				ADDSARG();
			}
			break;
		  case 'e':
		  case 'E':
		  case 'f':
		  case 'g':
		  case 'G':
			ADDTYPE(T_DOUBLE);
			break;
		  case 'n':
			if (flags & QUADINT)
				ADDTYPE(TP_QUAD);
			else if (flags & LONGINT)
				ADDTYPE(TP_LONG);
			else if (flags & SHORTINT)
				ADDTYPE(TP_SHORT);
			else
				ADDTYPE(TP_INT);
			continue;	/* no output */
		  case 'O':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		  case 'o':
			if (flags & QUADINT)
				ADDTYPE(T_U_QUAD);
			else
				ADDUARG();
			break;
		  case 'p':
			ADDTYPE(TP_VOID);
			break;
		  case 's':
			ADDTYPE(TP_CHAR);
			break;
		  case 'U':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		  case 'u':
			if (flags & QUADINT)
				ADDTYPE(T_U_QUAD);
			else
				ADDUARG();
			break;
		  case 'X':
		  case 'x':
			if (flags & QUADINT)
				ADDTYPE(T_U_QUAD);
			else
				ADDUARG();
			break;
		  default:	/* "%?" prints ?, unless ? is NUL */
			if (ch == '\0')
				goto done;
			break;
		}
	}
done:
	/* Build the argument table. */
	if (tablemax >= STATIC_ARG_TBL_SIZE)
	{
		*argtable = (va_list *)
		    sm_malloc(sizeof(va_list) * (tablemax + 1));
	}

	for (n = 1; n <= tablemax; n++)
	{
		SM_VA_COPY((*argtable)[n], ap);
		switch (typetable [n])
		{
		  case T_UNUSED:
			(void) SM_VA_ARG(ap, int);
			break;
		  case T_SHORT:
			(void) SM_VA_ARG(ap, int);
			break;
		  case T_U_SHORT:
			(void) SM_VA_ARG(ap, int);
			break;
		  case TP_SHORT:
			(void) SM_VA_ARG(ap, short *);
			break;
		  case T_INT:
			(void) SM_VA_ARG(ap, int);
			break;
		  case T_U_INT:
			(void) SM_VA_ARG(ap, unsigned int);
			break;
		  case TP_INT:
			(void) SM_VA_ARG(ap, int *);
			break;
		  case T_LONG:
			(void) SM_VA_ARG(ap, long);
			break;
		  case T_U_LONG:
			(void) SM_VA_ARG(ap, unsigned long);
			break;
		  case TP_LONG:
			(void) SM_VA_ARG(ap, long *);
			break;
		  case T_QUAD:
			(void) SM_VA_ARG(ap, LONGLONG_T);
			break;
		  case T_U_QUAD:
			(void) SM_VA_ARG(ap, ULONGLONG_T);
			break;
		  case TP_QUAD:
			(void) SM_VA_ARG(ap, LONGLONG_T *);
			break;
		  case T_DOUBLE:
			(void) SM_VA_ARG(ap, double);
			break;
		  case TP_CHAR:
			(void) SM_VA_ARG(ap, char *);
			break;
		  case TP_VOID:
			(void) SM_VA_ARG(ap, void *);
			break;
		}
	}

	if ((typetable != NULL) && (typetable != stattypetable))
		sm_free(typetable);
}

/*
**  SM_GROW_TYPE_TABLE -- Increase the size of the type table.
**
**	Parameters:
**		tabletype -- type of table to grow
**		tablesize -- requested new table size
**
**	Results:
**		Raises an exception if can't allocate memory.
*/

static void
sm_grow_type_table_x(typetable, tablesize)
	unsigned char **typetable;
	int *tablesize;
{
	unsigned char *oldtable = *typetable;
	int newsize = *tablesize * 2;

	if (*tablesize == STATIC_ARG_TBL_SIZE)
	{
		*typetable = (unsigned char *) sm_malloc_x(sizeof(unsigned char)
							   * newsize);
		(void) memmove(*typetable, oldtable, *tablesize);
	}
	else
	{
		*typetable = (unsigned char *) sm_realloc_x(typetable,
					sizeof(unsigned char) * newsize);
	}
	(void) memset(&typetable [*tablesize], T_UNUSED,
		       (newsize - *tablesize));

	*tablesize = newsize;
}
