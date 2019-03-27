/*
 * Copyright (c) Ian F. Darwin 1986-1995.
 * Software written by Ian F. Darwin and others;
 * maintained 1995-present by Christos Zoulas and others.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * print.c - debugging printout routines
 */

#include "file.h"

#ifndef lint
FILE_RCSID("@(#)$File: print.c,v 1.82 2017/02/10 18:14:01 christos Exp $")
#endif  /* lint */

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <time.h>

#define SZOF(a)	(sizeof(a) / sizeof(a[0]))

#include "cdf.h"

#ifndef COMPILE_ONLY
protected void
file_mdump(struct magic *m)
{
	static const char optyp[] = { FILE_OPS };
	char tbuf[26];

	(void) fprintf(stderr, "%u: %.*s %u", m->lineno,
	    (m->cont_level & 7) + 1, ">>>>>>>>", m->offset);

	if (m->flag & INDIR) {
		(void) fprintf(stderr, "(%s,",
		    /* Note: type is unsigned */
		    (m->in_type < file_nnames) ? file_names[m->in_type] :
		    "*bad in_type*");
		if (m->in_op & FILE_OPINVERSE)
			(void) fputc('~', stderr);
		(void) fprintf(stderr, "%c%u),",
		    ((size_t)(m->in_op & FILE_OPS_MASK) <
		    SZOF(optyp)) ? optyp[m->in_op & FILE_OPS_MASK] : '?',
		    m->in_offset);
	}
	(void) fprintf(stderr, " %s%s", (m->flag & UNSIGNED) ? "u" : "",
	    /* Note: type is unsigned */
	    (m->type < file_nnames) ? file_names[m->type] : "*bad type");
	if (m->mask_op & FILE_OPINVERSE)
		(void) fputc('~', stderr);

	if (IS_STRING(m->type)) {
		if (m->str_flags) {
			(void) fputc('/', stderr);
			if (m->str_flags & STRING_COMPACT_WHITESPACE) 
				(void) fputc(CHAR_COMPACT_WHITESPACE, stderr);
			if (m->str_flags & STRING_COMPACT_OPTIONAL_WHITESPACE) 
				(void) fputc(CHAR_COMPACT_OPTIONAL_WHITESPACE,
				    stderr);
			if (m->str_flags & STRING_IGNORE_LOWERCASE) 
				(void) fputc(CHAR_IGNORE_LOWERCASE, stderr);
			if (m->str_flags & STRING_IGNORE_UPPERCASE) 
				(void) fputc(CHAR_IGNORE_UPPERCASE, stderr);
			if (m->str_flags & REGEX_OFFSET_START) 
				(void) fputc(CHAR_REGEX_OFFSET_START, stderr);
			if (m->str_flags & STRING_TEXTTEST)
				(void) fputc(CHAR_TEXTTEST, stderr);
			if (m->str_flags & STRING_BINTEST)
				(void) fputc(CHAR_BINTEST, stderr);
			if (m->str_flags & PSTRING_1_BE)
				(void) fputc(CHAR_PSTRING_1_BE, stderr);
			if (m->str_flags & PSTRING_2_BE)
				(void) fputc(CHAR_PSTRING_2_BE, stderr);
			if (m->str_flags & PSTRING_2_LE)
				(void) fputc(CHAR_PSTRING_2_LE, stderr);
			if (m->str_flags & PSTRING_4_BE)
				(void) fputc(CHAR_PSTRING_4_BE, stderr);
			if (m->str_flags & PSTRING_4_LE)
				(void) fputc(CHAR_PSTRING_4_LE, stderr);
			if (m->str_flags & PSTRING_LENGTH_INCLUDES_ITSELF)
				(void) fputc(
				    CHAR_PSTRING_LENGTH_INCLUDES_ITSELF,
				    stderr);
		}
		if (m->str_range)
			(void) fprintf(stderr, "/%u", m->str_range);
	}
	else {
		if ((size_t)(m->mask_op & FILE_OPS_MASK) < SZOF(optyp))
			(void) fputc(optyp[m->mask_op & FILE_OPS_MASK], stderr);
		else
			(void) fputc('?', stderr);
			
		if (m->num_mask) {
			(void) fprintf(stderr, "%.8llx",
			    (unsigned long long)m->num_mask);
		}
	}
	(void) fprintf(stderr, ",%c", m->reln);

	if (m->reln != 'x') {
		switch (m->type) {
		case FILE_BYTE:
		case FILE_SHORT:
		case FILE_LONG:
		case FILE_LESHORT:
		case FILE_LELONG:
		case FILE_MELONG:
		case FILE_BESHORT:
		case FILE_BELONG:
		case FILE_INDIRECT:
			(void) fprintf(stderr, "%d", m->value.l);
			break;
		case FILE_BEQUAD:
		case FILE_LEQUAD:
		case FILE_QUAD:
			(void) fprintf(stderr, "%" INT64_T_FORMAT "d",
			    (unsigned long long)m->value.q);
			break;
		case FILE_PSTRING:
		case FILE_STRING:
		case FILE_REGEX:
		case FILE_BESTRING16:
		case FILE_LESTRING16:
		case FILE_SEARCH:
			file_showstr(stderr, m->value.s, (size_t)m->vallen);
			break;
		case FILE_DATE:
		case FILE_LEDATE:
		case FILE_BEDATE:
		case FILE_MEDATE:
			(void)fprintf(stderr, "%s,",
			    file_fmttime(m->value.l, 0, tbuf));
			break;
		case FILE_LDATE:
		case FILE_LELDATE:
		case FILE_BELDATE:
		case FILE_MELDATE:
			(void)fprintf(stderr, "%s,",
			    file_fmttime(m->value.l, FILE_T_LOCAL, tbuf));
			break;
		case FILE_QDATE:
		case FILE_LEQDATE:
		case FILE_BEQDATE:
			(void)fprintf(stderr, "%s,",
			    file_fmttime(m->value.q, 0, tbuf));
			break;
		case FILE_QLDATE:
		case FILE_LEQLDATE:
		case FILE_BEQLDATE:
			(void)fprintf(stderr, "%s,",
			    file_fmttime(m->value.q, FILE_T_LOCAL, tbuf));
			break;
		case FILE_QWDATE:
		case FILE_LEQWDATE:
		case FILE_BEQWDATE:
			(void)fprintf(stderr, "%s,",
			    file_fmttime(m->value.q, FILE_T_WINDOWS, tbuf));
			break;
		case FILE_FLOAT:
		case FILE_BEFLOAT:
		case FILE_LEFLOAT:
			(void) fprintf(stderr, "%G", m->value.f);
			break;
		case FILE_DOUBLE:
		case FILE_BEDOUBLE:
		case FILE_LEDOUBLE:
			(void) fprintf(stderr, "%G", m->value.d);
			break;
		case FILE_DEFAULT:
			/* XXX - do anything here? */
			break;
		case FILE_USE:
		case FILE_NAME:
		case FILE_DER:
			(void) fprintf(stderr, "'%s'", m->value.s);
			break;
		default:
			(void) fprintf(stderr, "*bad type %d*", m->type);
			break;
		}
	}
	(void) fprintf(stderr, ",\"%s\"]\n", m->desc);
}
#endif

/*VARARGS*/
protected void
file_magwarn(struct magic_set *ms, const char *f, ...)
{
	va_list va;

	/* cuz we use stdout for most, stderr here */
	(void) fflush(stdout); 

	if (ms->file)
		(void) fprintf(stderr, "%s, %lu: ", ms->file,
		    (unsigned long)ms->line);
	(void) fprintf(stderr, "Warning: ");
	va_start(va, f);
	(void) vfprintf(stderr, f, va);
	va_end(va);
	(void) fputc('\n', stderr);
}

protected const char *
file_fmttime(uint64_t v, int flags, char *buf)
{
	char *pp;
	time_t t;
	struct tm *tm, tmz;

	if (flags & FILE_T_WINDOWS) {
		struct timespec ts;
		cdf_timestamp_to_timespec(&ts, CAST(cdf_timestamp_t, v));
		t = ts.tv_sec;
	} else {
		// XXX: perhaps detect and print something if overflow
		// on 32 bit time_t?
		t = (time_t)v;
	}

	if (flags & FILE_T_LOCAL) {
		tm = localtime_r(&t, &tmz);
	} else {
		tm = gmtime_r(&t, &tmz);
	}
	if (tm == NULL)
		goto out;
	pp = asctime_r(tm, buf);

	if (pp == NULL)
		goto out;
	pp[strcspn(pp, "\n")] = '\0';
	return pp;
out:
	return strcpy(buf, "*Invalid time*");
}
