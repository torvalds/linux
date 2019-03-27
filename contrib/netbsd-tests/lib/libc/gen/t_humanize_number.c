/*	$NetBSD: t_humanize_number.c,v 1.9 2017/01/10 15:20:44 christos Exp $	*/

/*-
 * Copyright (c) 2010, 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <atf-c.h>

#include <err.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

const struct hnopts {
	size_t ho_len;
	int64_t ho_num;
	const char *ho_suffix;
	int ho_scale;
	int ho_flags;
	int ho_retval;			/* expected return value */
	const char *ho_retstr;		/* expected string in buffer */
} hnopts[] = {
	/*
	 * Rev. 1.6 produces "10.0".
	 */
	{ 5, 10737418236ULL * 1024, "",
	  HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL, 3, "10T" },

	{ 5, 10450000, "",
	  HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL, 3, "10M" },
	{ 5, 10500000, "",		/* just for reference */
	  HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL, 3, "10M" },

	/*
	 * Trailing space.  Rev. 1.7 produces "1 ".
	 */
	{ 5, 1, "", 0, HN_NOSPACE, 1, "1" },

	{ 5, 1, "", 0, 0, 2, "1 " }, /* just for reference */
	{ 5, 1, "", 0, HN_B, 3, "1 B" }, /* and more ... */
	{ 5, 1, "", 0, HN_DECIMAL, 2, "1 " },
	{ 5, 1, "", 0, HN_NOSPACE | HN_B, 2, "1B" },
	{ 5, 1, "", 0, HN_B | HN_DECIMAL, 3, "1 B" },
	{ 5, 1, "", 0, HN_NOSPACE | HN_B | HN_DECIMAL, 2, "1B" },

	/*
	 * Space and HN_B.  Rev. 1.7 produces "1B".
	 */
	{ 5, 1, "", HN_AUTOSCALE, HN_B, 3, "1 B" },
	{ 5, 1000, "",			/* just for reference */
	  HN_AUTOSCALE, HN_B, 3, "1 K" },

	/*
	 * Truncated output.  Rev. 1.7 produces "1.0 K".
	 */
#ifndef __FreeBSD__
	{ 6, 1000, "A", HN_AUTOSCALE, HN_DECIMAL, -1, "" },

	/*
	 * Failure case reported by Greg Troxel <gdt@NetBSD.org>.
	 * Rev. 1.11 incorrectly returns 5 with filling the buffer
	 * with "1000".
	 */
	{ 5, 1048258238, "",
	  HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL, 4, "1.0G" },
	/* Similar case it prints 1000 where it shouldn't */
	{ 5, 1023488, "",
	  HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL, 4, "1.0M" },
#endif
	{ 5, 1023999, "",
	  HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL, 4, "1.0M" },
};

struct hnflags {
	int hf_flags;
	const char *hf_name;
};

const struct hnflags scale_flags[] = {
	{ HN_GETSCALE, "HN_GETSCALE" },
	{ HN_AUTOSCALE, "HN_AUTOSCALE" },
};
const struct hnflags normal_flags[] = {
	{ HN_DECIMAL, "HN_DECIMAL" },
	{ HN_NOSPACE, "HN_NOSPACE" },
	{ HN_B, "HN_B" },
	{ HN_DIVISOR_1000, "HN_DIVISOR_1000" },
};

const char *formatflags(char *, size_t, const struct hnflags *, size_t, int);
void	    newline(void);
void	    w_printf(const char *, ...) __printflike(1, 2);
int	    main(int, char *[]);

const char *
formatflags(char *buf, size_t buflen, const struct hnflags *hfs,
    size_t hfslen, int flags)
{
	const struct hnflags *hf;
	char *p = buf;
	ssize_t len = buflen;
	unsigned int i, found;
	int n;

	if (flags == 0) {
		snprintf(buf, buflen, "0");
		return (buf);
	}
	for (i = found = 0; i < hfslen && flags & ~found; i++) {
		hf = &hfs[i];
		if (flags & hf->hf_flags) {
			found |= hf->hf_flags;
			n = snprintf(p, len, "|%s", hf->hf_name);
			if (n >= len) {
				p = buf;
				len = buflen;
				/* Print `flags' as number */
				goto bad;
			}
			p += n;
			len -= n;
		}
	}
	flags &= ~found;
	if (flags)
bad:
		snprintf(p, len, "|0x%x", flags);
	return (*buf == '|' ? buf + 1 : buf);
}

static int col, bol = 1;
void
newline(void)
{

	fprintf(stderr, "\n");
	col = 0;
	bol = 1;
}

void
w_printf(const char *fmt, ...)
{
	char buf[80];
	va_list ap;
	int n;

	va_start(ap, fmt);
	if (col >= 0) {
		n = vsnprintf(buf, sizeof(buf), fmt, ap);
		if (n >= (int)sizeof(buf)) {
			col = -1;
			goto overflow;
		} else if (n == 0)
			goto out;

		if (!bol) {
			if (col + n > 75)
				fprintf(stderr, "\n    "), col = 4;
			else
				fprintf(stderr, " "), col++;
		}
		fprintf(stderr, "%s", buf);
		col += n;
		bol = 0;
	} else {
overflow:
		vfprintf(stderr, fmt, ap);
	}
out:
	va_end(ap);
}

ATF_TC(humanize_number_basic);
ATF_TC_HEAD(humanize_number_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test humanize_number(3)");
}

ATF_TC_BODY(humanize_number_basic, tc)
{
	char fbuf[128];
	const struct hnopts *ho;
	char *buf = NULL;
	size_t buflen = 0;
	unsigned int i;
	int rv = 0;

	for (i = 0; i < __arraycount(hnopts); i++) {
		ho = &hnopts[i];
		if (buflen < ho->ho_len) {
			buflen = ho->ho_len;
			buf = realloc(buf, buflen);
			if (buf == NULL)
				atf_tc_fail("realloc(..., %zu) failed", buflen);
		}

		rv = humanize_number(buf, ho->ho_len, ho->ho_num,
		    ho->ho_suffix, ho->ho_scale, ho->ho_flags);

		if (rv == ho->ho_retval &&
		    (rv == -1 || strcmp(buf, ho->ho_retstr) == 0))
			continue;

		w_printf("humanize_number(\"%s\", %zu, %" PRId64 ",",
		    ho->ho_retstr, ho->ho_len, ho->ho_num);
		w_printf("\"%s\",", ho->ho_suffix);
		w_printf("%s,", formatflags(fbuf, sizeof(fbuf), scale_flags,
		    sizeof(scale_flags) / sizeof(scale_flags[0]),
		    ho->ho_scale));
		w_printf("%s)", formatflags(fbuf, sizeof(fbuf), normal_flags,
		    sizeof(normal_flags) / sizeof(normal_flags[0]),
		    ho->ho_flags));
		w_printf("= %d,", ho->ho_retval);
		w_printf("but got");
		w_printf("%d/[%s]", rv, rv == -1 ? "" : buf);
		newline();
		atf_tc_fail_nonfatal("Failed for table entry %d", i);
	}
	free(buf);
}

ATF_TC(humanize_number_big);
ATF_TC_HEAD(humanize_number_big, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test humanize "
	    "big numbers (PR lib/44097)");
}

ATF_TC_BODY(humanize_number_big, tc)
{
	char buf[1024];
	int rv;

	/*
	 * Seems to work.
	 */
	(void)memset(buf, 0, sizeof(buf));

	rv = humanize_number(buf, 10, 10000, "", HN_AUTOSCALE, HN_NOSPACE);

	ATF_REQUIRE(rv != -1);
	ATF_CHECK_STREQ(buf, "10000");

	/*
	 * A bogus value with large number.
	 */
	(void)memset(buf, 0, sizeof(buf));

	rv = humanize_number(buf, 10, INT64_MAX, "", HN_AUTOSCALE, HN_NOSPACE);

	ATF_REQUIRE(rv != -1);
	ATF_REQUIRE(strcmp(buf, "0") != 0);

	/*
	 * Large buffer with HN_AUTOSCALE. Entirely bogus.
	 */
	(void)memset(buf, 0, sizeof(buf));

	rv = humanize_number(buf, sizeof(buf), 10000, "",
	    HN_AUTOSCALE, HN_NOSPACE);

	ATF_REQUIRE(rv != -1);
	ATF_REQUIRE(strcmp(buf, "0%d%s%d%s%s%s") != 0);

	/*
	 * Tight buffer.
	 *
	 * The man page says that len must be at least 4.
	 * 3 works, but anything less that will not. This
	 * is because baselen starts with 2 for positive
	 * numbers.
	 */
	(void)memset(buf, 0, sizeof(buf));

	rv = humanize_number(buf, 3, 1, "", HN_AUTOSCALE, HN_NOSPACE);

	ATF_REQUIRE(rv != -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, humanize_number_basic);
	ATF_TP_ADD_TC(tp, humanize_number_big);

	return atf_no_error();
}
