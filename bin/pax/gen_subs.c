/*	$OpenBSD: gen_subs.c,v 1.34 2024/04/27 19:49:42 florian Exp $	*/
/*	$NetBSD: gen_subs.c,v 1.5 1995/03/21 09:07:26 cgd Exp $	*/

/*-
 * Copyright (c) 1992 Keith Muller.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>
#include <vis.h>

#include "pax.h"
#include "extern.h"

/*
 * a collection of general purpose subroutines used by pax
 */

/*
 * constants used by ls_list() when printing out archive members
 */
#define MODELEN 20
#define DATELEN 64
#define SECSPERDAY	(24 * 60 * 60)
#define SIXMONTHS	(SECSPERDAY * 365 / 2)
#define CURFRMT		"%b %e %H:%M"
#define OLDFRMT		"%b %e  %Y"
#define NAME_WIDTH	8
#define	TIMEFMT(t, now) \
	(((t) + SIXMONTHS <= (now) || (t) > (now)) ? OLDFRMT : CURFRMT)

/*
 * ls_list()
 *	list the members of an archive in ls format
 */

void
ls_list(ARCHD *arcn, time_t now, FILE *fp)
{
	struct stat *sbp;
	struct tm *tm;
	char f_mode[MODELEN];
	char f_date[DATELEN];
	int term;

	term = zeroflag ? '\0' : '\n';	/* path termination character */

	/*
	 * if not verbose, just print the file name
	 */
	if (!vflag) {
		if (zeroflag)
			(void)fputs(arcn->name, fp);
		else
			safe_print(arcn->name, fp);
		(void)putc(term, fp);
		(void)fflush(fp);
		return;
	}

	/*
	 * user wants long mode
	 */
	sbp = &(arcn->sb);
	strmode(sbp->st_mode, f_mode);

	/*
	 * print file mode, link count, uid, gid and time
	 */
	if ((tm = localtime(&(sbp->st_mtime))) == NULL)
		f_date[0] = '\0';
	else if (strftime(f_date, sizeof(f_date), TIMEFMT(sbp->st_mtime, now),
	    tm) == 0)
		f_date[0] = '\0';
	(void)fprintf(fp, "%s%2u %-*.*s %-*.*s ", f_mode, sbp->st_nlink,
		NAME_WIDTH, UT_NAMESIZE, user_from_uid(sbp->st_uid, 0),
		NAME_WIDTH, UT_NAMESIZE, group_from_gid(sbp->st_gid, 0));

	/*
	 * print device id's for devices, or sizes for other nodes
	 */
	if ((arcn->type == PAX_CHR) || (arcn->type == PAX_BLK))
		(void)fprintf(fp, "%4lu, %4lu ",
		    (unsigned long)MAJOR(sbp->st_rdev),
		    (unsigned long)MINOR(sbp->st_rdev));
	else {
		(void)fprintf(fp, "%9llu ", sbp->st_size);
	}

	/*
	 * print name and link info for hard and soft links
	 */
	(void)fputs(f_date, fp);
	(void)putc(' ', fp);
	safe_print(arcn->name, fp);
	if (PAX_IS_HARDLINK(arcn->type)) {
		fputs(" == ", fp);
		safe_print(arcn->ln_name, fp);
	} else if (arcn->type == PAX_SLK) {
		fputs(" -> ", fp);
		safe_print(arcn->ln_name, fp);
	}
	(void)putc(term, fp);
	(void)fflush(fp);
}

/*
 * tty_ls()
 *	print a short summary of file to tty.
 */

void
ls_tty(ARCHD *arcn)
{
	struct tm *tm;
	char f_date[DATELEN];
	char f_mode[MODELEN];
	time_t now = time(NULL);

	/*
	 * convert time to string, and print
	 */
	if ((tm = localtime(&(arcn->sb.st_mtime))) == NULL)
		f_date[0] = '\0';
	else if (strftime(f_date, DATELEN, TIMEFMT(arcn->sb.st_mtime, now),
	    tm) == 0)
		f_date[0] = '\0';
	strmode(arcn->sb.st_mode, f_mode);
	tty_prnt("%s%s %s\n", f_mode, f_date, arcn->name);
}

void
safe_print(const char *str, FILE *fp)
{
	char visbuf[5];
	const char *cp;

	/*
	 * if printing to a tty, use vis(3) to print special characters.
	 */
	if (isatty(fileno(fp))) {
		for (cp = str; *cp; cp++) {
			(void)vis(visbuf, cp[0], VIS_CSTYLE, cp[1]);
			(void)fputs(visbuf, fp);
		}
	} else {
		(void)fputs(str, fp);
	}
}

/*
 * asc_ul()
 *	convert hex/octal character string into a u_long. We do not have to
 *	check for overflow! (the headers in all supported formats are not large
 *	enough to create an overflow).
 *	NOTE: strings passed to us are NOT TERMINATED.
 * Return:
 *	unsigned long value
 */

u_long
asc_ul(char *str, int len, int base)
{
	char *stop;
	u_long tval = 0;

	stop = str + len;

	/*
	 * skip over leading blanks and zeros
	 */
	while ((str < stop) && ((*str == ' ') || (*str == '0')))
		++str;

	/*
	 * for each valid digit, shift running value (tval) over to next digit
	 * and add next digit
	 */
	if (base == HEX) {
		while (str < stop) {
			if ((*str >= '0') && (*str <= '9'))
				tval = (tval << 4) + (*str++ - '0');
			else if ((*str >= 'A') && (*str <= 'F'))
				tval = (tval << 4) + 10 + (*str++ - 'A');
			else if ((*str >= 'a') && (*str <= 'f'))
				tval = (tval << 4) + 10 + (*str++ - 'a');
			else
				break;
		}
	} else {
		while ((str < stop) && (*str >= '0') && (*str <= '7'))
			tval = (tval << 3) + (*str++ - '0');
	}
	return(tval);
}

/*
 * ul_asc()
 *	convert an unsigned long into an hex/oct ascii string. pads with LEADING
 *	ascii 0's to fill string completely
 *	NOTE: the string created is NOT TERMINATED.
 */

int
ul_asc(u_long val, char *str, int len, int base)
{
	char *pt;
	u_long digit;

	/*
	 * WARNING str is not '\0' terminated by this routine
	 */
	pt = str + len - 1;

	/*
	 * do a tailwise conversion (start at right most end of string to place
	 * least significant digit). Keep shifting until conversion value goes
	 * to zero (all digits were converted)
	 */
	if (base == HEX) {
		while (pt >= str) {
			if ((digit = (val & 0xf)) < 10)
				*pt-- = '0' + (char)digit;
			else
				*pt-- = 'a' + (char)(digit - 10);
			val >>= 4;
			if (val == 0)
				break;
		}
	} else {
		while (pt >= str) {
			*pt-- = '0' + (char)(val & 0x7);
			val >>= 3;
			if (val == 0)
				break;
		}
	}

	/*
	 * pad with leading ascii ZEROS. We return -1 if we ran out of space.
	 */
	while (pt >= str)
		*pt-- = '0';
	if (val != 0)
		return(-1);
	return(0);
}

/*
 * asc_ull()
 *	Convert hex/octal character string into a unsigned long long.
 *	We do not have to check for overflow!  (The headers in all
 *	supported formats are not large enough to create an overflow).
 *	NOTE: strings passed to us are NOT TERMINATED.
 * Return:
 *	unsigned long long value
 */

unsigned long long
asc_ull(char *str, int len, int base)
{
	char *stop;
	unsigned long long tval = 0;

	stop = str + len;

	/*
	 * skip over leading blanks and zeros
	 */
	while ((str < stop) && ((*str == ' ') || (*str == '0')))
		++str;

	/*
	 * for each valid digit, shift running value (tval) over to next digit
	 * and add next digit
	 */
	if (base == HEX) {
		while (str < stop) {
			if ((*str >= '0') && (*str <= '9'))
				tval = (tval << 4) + (*str++ - '0');
			else if ((*str >= 'A') && (*str <= 'F'))
				tval = (tval << 4) + 10 + (*str++ - 'A');
			else if ((*str >= 'a') && (*str <= 'f'))
				tval = (tval << 4) + 10 + (*str++ - 'a');
			else
				break;
		}
	} else {
		while ((str < stop) && (*str >= '0') && (*str <= '7'))
			tval = (tval << 3) + (*str++ - '0');
	}
	return(tval);
}

/*
 * ull_asc()
 *	Convert an unsigned long long into a hex/oct ascii string.
 *	Pads with LEADING ascii 0's to fill string completely
 *	NOTE: the string created is NOT TERMINATED.
 */

int
ull_asc(unsigned long long val, char *str, int len, int base)
{
	char *pt;
	unsigned long long digit;

	/*
	 * WARNING str is not '\0' terminated by this routine
	 */
	pt = str + len - 1;

	/*
	 * do a tailwise conversion (start at right most end of string to place
	 * least significant digit). Keep shifting until conversion value goes
	 * to zero (all digits were converted)
	 */
	if (base == HEX) {
		while (pt >= str) {
			if ((digit = (val & 0xf)) < 10)
				*pt-- = '0' + (char)digit;
			else
				*pt-- = 'a' + (char)(digit - 10);
			val >>= 4;
			if (val == 0)
				break;
		}
	} else {
		while (pt >= str) {
			*pt-- = '0' + (char)(val & 0x7);
			val >>= 3;
			if (val == 0)
				break;
		}
	}

	/*
	 * pad with leading ascii ZEROS. We return -1 if we ran out of space.
	 */
	while (pt >= str)
		*pt-- = '0';
	if (val != 0)
		return(-1);
	return(0);
}

/*
 * Copy at max min(bufz, fieldsz) chars from field to buf, stopping
 * at the first NUL char. NUL terminate buf if there is room left.
 */
size_t
fieldcpy(char *buf, size_t bufsz, const char *field, size_t fieldsz)
{
	char *p = buf;
	const char *q = field;
	size_t i = 0;

	if (fieldsz > bufsz)
		fieldsz = bufsz;
	while (i < fieldsz && *q != '\0') {
		*p++ = *q++;
		i++;
	}
	if (i < bufsz)
		*p = '\0';
	return(i);
}
