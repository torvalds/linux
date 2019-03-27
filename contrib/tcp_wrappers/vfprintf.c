 /*
  * vfprintf() and vprintf() clones. They will produce unexpected results
  * when excessive dynamic ("*") field widths are specified. To be used for
  * testing purposes only.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) vfprintf.c 1.2 94/03/23 17:44:46";
#endif

#include <stdio.h>
#include <ctype.h>
#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

/* vfprintf - print variable-length argument list to stream */

int     vfprintf(fp, format, ap)
FILE   *fp;
char   *format;
va_list ap;
{
    char    fmt[BUFSIZ];		/* format specifier */
    register char *fmtp;
    register char *cp;
    int     count = 0;

    /*
     * Iterate over characters in the format string, picking up arguments
     * when format specifiers are found.
     */

    for (cp = format; *cp; cp++) {
	if (*cp != '%') {
	    putc(*cp, fp);			/* ordinary character */
	    count++;
	} else {

	    /*
	     * Format specifiers are handled one at a time, since we can only
	     * deal with arguments one at a time. Try to determine the end of
	     * the format specifier. We do not attempt to fully parse format
	     * strings, since we are ging to let fprintf() do the hard work.
	     * In regular expression notation, we recognize:
	     * 
	     * %-?0?([0-9]+|\*)?\.?([0-9]+|\*)?l?[a-z]
	     * 
	     * which includes some combinations that do not make sense.
	     */

	    fmtp = fmt;
	    *fmtp++ = *cp++;
	    if (*cp == '-')			/* left-adjusted field? */
		*fmtp++ = *cp++;
	    if (*cp == '0')			/* zero-padded field? */
		*fmtp++ = *cp++;
	    if (*cp == '*') {			/* dynamic field witdh */
		sprintf(fmtp, "%d", va_arg(ap, int));
		fmtp += strlen(fmtp);
		cp++;
	    } else {
		while (isdigit(*cp))		/* hard-coded field width */
		    *fmtp++ = *cp++;
	    }
	    if (*cp == '.')			/* width/precision separator */
		*fmtp++ = *cp++;
	    if (*cp == '*') {			/* dynamic precision */
		sprintf(fmtp, "%d", va_arg(ap, int));
		fmtp += strlen(fmtp);
		cp++;
	    } else {
		while (isdigit(*cp))		/* hard-coded precision */
		    *fmtp++ = *cp++;
	    }
	    if (*cp == 'l')			/* long whatever */
		*fmtp++ = *cp++;
	    if (*cp == 0)			/* premature end, punt */
		break;
	    *fmtp++ = *cp;			/* type (checked below) */
	    *fmtp = 0;

	    /* Execute the format string - let fprintf() do the hard work. */

	    switch (fmtp[-1]) {
	    case 's':				/* string-valued argument */
		count += fprintf(fp, fmt, va_arg(ap, char *));
		break;
	    case 'c':				/* integral-valued argument */
	    case 'd':
	    case 'u':
	    case 'o':
	    case 'x':
		if (fmtp[-2] == 'l')
		    count += fprintf(fp, fmt, va_arg(ap, long));
		else
		    count += fprintf(fp, fmt, va_arg(ap, int));
		break;
	    case 'e':				/* float-valued argument */
	    case 'f':
	    case 'g':
		count += fprintf(fp, fmt, va_arg(ap, double));
		break;
	    default:				/* anything else */
		putc(fmtp[-1], fp);
		count++;
		break;
	    }
	}
    }
    return (count);
}

/* vprintf - print variable-length argument list to stdout */

vprintf(format, ap)
char   *format;
va_list ap;
{
    return (vfprintf(stdout, format, ap));
}
