/* $NetBSD: t_fmemopen.c,v 1.4 2013/10/19 17:45:00 christos Exp $ */

/*-
 * Copyright (c)2010 Takehiko NOZAKI,
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <atf-c.h>
#else
#if defined(__linux__)
#define _GNU_SOURCE
#include <features.h>
#endif
#include <assert.h>
#include <stdio.h>
#define ATF_TC(arg0)		static void arg0##_head(void)
#define ATF_TC_HEAD(arg0, arg1)	static void arg0##_head()
#define atf_tc_set_md_var(arg0, arg1, ...) do {	\
	printf(__VA_ARGS__);			\
	puts("");				\
} while (/*CONSTCOND*/0)
#define ATF_TC_BODY(arg0, arg1)	static void arg0##_body()
#define ATF_CHECK(arg0)		assert(arg0)
#define ATF_TP_ADD_TCS(arg0)	int main(void)
#define ATF_TP_ADD_TC(arg0, arg1) arg1##_head(); arg1##_body()
#define atf_no_error()		0
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

const char *mode_rwa[] = {
    "r", "rb", "r+", "rb+", "r+b",
    "w", "wb", "w+", "wb+", "w+b",
    "a", "ab", "a+", "ab+", "a+b",
    NULL
};

const char *mode_r[] = { "r", "rb", "r+", "rb+", "r+b", NULL };
const char *mode_w[] = { "w", "wb", "w+", "wb+", "w+b", NULL };
const char *mode_a[] = { "a", "ab", "a+", "ab+", "a+b", NULL }; 

struct testcase {
	const char *s;
	off_t n;
} testcases[] = {
#define TESTSTR(s)	{ s, sizeof(s)-1 }
	TESTSTR("\0he quick brown fox jumps over the lazy dog"),
	TESTSTR("T\0e quick brown fox jumps over the lazy dog"),
	TESTSTR("Th\0 quick brown fox jumps over the lazy dog"),
	TESTSTR("The\0quick brown fox jumps over the lazy dog"),
	TESTSTR("The \0uick brown fox jumps over the lazy dog"),
	TESTSTR("The q\0ick brown fox jumps over the lazy dog"),
	TESTSTR("The qu\0ck brown fox jumps over the lazy dog"),
	TESTSTR("The qui\0k brown fox jumps over the lazy dog"),
	TESTSTR("The quic\0 brown fox jumps over the lazy dog"),
	TESTSTR("The quick\0brown fox jumps over the lazy dog"),
	TESTSTR("The quick \0rown fox jumps over the lazy dog"),
	TESTSTR("The quick b\0own fox jumps over the lazy dog"),
	TESTSTR("The quick br\0wn fox jumps over the lazy dog"),
	TESTSTR("The quick bro\0n fox jumps over the lazy dog"),
	TESTSTR("The quick brow\0 fox jumps over the lazy dog"),
	TESTSTR("The quick brown\0fox jumps over the lazy dog"),
	TESTSTR("The quick brown \0ox jumps over the lazy dog"),
	TESTSTR("The quick brown f\0x jumps over the lazy dog"),
	TESTSTR("The quick brown fo\0 jumps over the lazy dog"),
	TESTSTR("The quick brown fox\0jumps over the lazy dog"),
	TESTSTR("The quick brown fox \0umps over the lazy dog"),
	TESTSTR("The quick brown fox j\0mps over the lazy dog"),
	TESTSTR("The quick brown fox ju\0ps over the lazy dog"),
	TESTSTR("The quick brown fox jum\0s over the lazy dog"),
	TESTSTR("The quick brown fox jump\0 over the lazy dog"),
	TESTSTR("The quick brown fox jumps\0over the lazy dog"),
	TESTSTR("The quick brown fox jumps \0ver the lazy dog"),
	TESTSTR("The quick brown fox jumps o\0er the lazy dog"),
	TESTSTR("The quick brown fox jumps ov\0r the lazy dog"),
	TESTSTR("The quick brown fox jumps ove\0 the lazy dog"),
	TESTSTR("The quick brown fox jumps over\0the lazy dog"),
	TESTSTR("The quick brown fox jumps over \0he lazy dog"),
	TESTSTR("The quick brown fox jumps over t\0e lazy dog"),
	TESTSTR("The quick brown fox jumps over th\0 lazy dog"),
	TESTSTR("The quick brown fox jumps over the\0lazy dog"),
	TESTSTR("The quick brown fox jumps over the \0azy dog"),
	TESTSTR("The quick brown fox jumps over the l\0zy dog"),
	TESTSTR("The quick brown fox jumps over the la\0y dog"),
	TESTSTR("The quick brown fox jumps over the laz\0 dog"),
	TESTSTR("The quick brown fox jumps over the lazy\0dog"),
	TESTSTR("The quick brown fox jumps over the lazy \0og"),
	TESTSTR("The quick brown fox jumps over the lazy d\0g"),
	TESTSTR("The quick brown fox jumps over the lazy do\0"),
	TESTSTR("The quick brown fox jumps over the lazy dog"),
	{ NULL, 0 },
};

ATF_TC(test00);
ATF_TC_HEAD(test00, tc)
{
	atf_tc_set_md_var(tc, "descr", "test00");
}
ATF_TC_BODY(test00, tc)
{
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	for (p = &mode_rwa[0]; *p != NULL; ++p) {
		fp = fmemopen(&buf[0], sizeof(buf), *p);
/*
 * Upon successful completion, fmemopen() shall return a pointer to the
 * object controlling the stream.
 */
		ATF_CHECK(fp != NULL);

		ATF_CHECK(fclose(fp) == 0);
	}
}

ATF_TC(test01);
ATF_TC_HEAD(test01, tc)
{
	atf_tc_set_md_var(tc, "descr", "test01");
}
ATF_TC_BODY(test01, tc)
{
	const char **p;
	const char *mode[] = {
	    "r+", "rb+", "r+b",
	    "w+", "wb+", "w+b",
	    "a+", "ab+", "a+b",
	    NULL
	};
	FILE *fp;

	for (p = &mode[0]; *p != NULL; ++p) {
/*
 * If a null pointer is specified as the buf argument, fmemopen() shall
 * allocate size bytes of memory as if by a call to malloc().
 */
		fp = fmemopen(NULL, BUFSIZ, *p);
		ATF_CHECK(fp != NULL);

/*
 * If buf is a null pointer, the initial position shall always be set
 * to the beginning of the buffer.
 */
		ATF_CHECK(ftello(fp) == (off_t)0);

		ATF_CHECK(fclose(fp) == 0);
	}
}

ATF_TC(test02);
ATF_TC_HEAD(test02, tc)
{
        atf_tc_set_md_var(tc, "descr", "test02");
}
ATF_TC_BODY(test02, tc)
{
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	for (p = &mode_r[0]; *p != NULL; ++p) {

		memset(&buf[0], 0x1, sizeof(buf));
		fp = fmemopen(&buf[0], sizeof(buf), *p);
		ATF_CHECK(fp != NULL);

/*
 * This position is initially set to either the beginning of the buffer
 * (for r and w modes)
 */
		ATF_CHECK((unsigned char)buf[0] == 0x1);
		ATF_CHECK(ftello(fp) == (off_t)0);

/*
 * The stream also maintains the size of the current buffer contents.
 * For modes r and r+ the size is set to the value given by the size argument.
 */
#if !defined(__GLIBC__)
		ATF_CHECK(fseeko(fp, (off_t)0, SEEK_END) == 0);
		ATF_CHECK(ftello(fp) == (off_t)sizeof(buf));
#endif
		ATF_CHECK(fclose(fp) == 0);
	}
}

ATF_TC(test03);
ATF_TC_HEAD(test03, tc)
{
        atf_tc_set_md_var(tc, "descr", "test03");
}
ATF_TC_BODY(test03, tc)
{
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;
 
	for (p = &mode_w[0]; *p != NULL; ++p) {

		memset(&buf[0], 0x1, sizeof(buf));
		fp = fmemopen(&buf[0], sizeof(buf), *p);
		ATF_CHECK(fp != NULL);

/*
 * This position is initially set to either the beginning of the buffer
 * (for r and w modes)
 */
		ATF_CHECK(buf[0] == '\0');
		ATF_CHECK(ftello(fp) == (off_t)0);

/*
 * For modes w and w+ the initial size is zero
 */
		ATF_CHECK(fseeko(fp, (off_t)0, SEEK_END) == 0);
		ATF_CHECK(ftello(fp) == (off_t)0);

		ATF_CHECK(fclose(fp) == 0);
	}
}

ATF_TC(test04);
ATF_TC_HEAD(test04, tc)
{
	atf_tc_set_md_var(tc, "descr", "test04");
}
ATF_TC_BODY(test04, tc)
{
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

/*
 * or to the first null byte in the buffer (for a modes)
 */
	for (p = &mode_a[0]; *p != NULL; ++p) {

		memset(&buf[0], 0x1, sizeof(buf));
		fp = fmemopen(&buf[0], sizeof(buf), *p);
		ATF_CHECK(fp != NULL);

		ATF_CHECK((unsigned char)buf[0] == 0x1);

/* If no null byte is found in append mode,
 * the initial position is set to one byte after the end of the buffer.
 */
#if !defined(__GLIBC__)
		ATF_CHECK(ftello(fp) == (off_t)sizeof(buf));
#endif

/*
 * and for modes a and a+ the initial size is either the position of the
 * first null byte in the buffer or the value of the size argument
 * if no null byte is found.
 */
#if !defined(__GLIBC__)
		ATF_CHECK(fseeko(fp, (off_t)0, SEEK_END) == 0);
		ATF_CHECK(ftello(fp) == (off_t)sizeof(buf));
#endif

		ATF_CHECK(fclose(fp) == 0);
	}
}

ATF_TC(test05);
ATF_TC_HEAD(test05, tc)
{
	atf_tc_set_md_var(tc, "descr", "test05");
}
ATF_TC_BODY(test05, tc)
{
	const char **p;
	FILE *fp;
	char buf[BUFSIZ];

	for (p = &mode_rwa[0]; *p != NULL; ++p) {
/*
 * Otherwise, a null pointer shall be returned, and errno shall be set
 * to indicate the error.
 */
		errno = 0;
		fp = fmemopen(NULL, (size_t)0, *p);
		ATF_CHECK(fp == NULL);
		ATF_CHECK(errno == EINVAL);

		errno = 0;
		fp = fmemopen((void *)&buf[0], 0, *p);
		ATF_CHECK(fp == NULL);
		ATF_CHECK(errno == EINVAL);
	}
}

ATF_TC(test06);
ATF_TC_HEAD(test06, tc)
{
	atf_tc_set_md_var(tc, "descr", "test06");
}
ATF_TC_BODY(test06, tc)
{
	const char **p;
	const char *mode[] = { "", " ", "???", NULL };
	FILE *fp;

	for (p = &mode[0]; *p != NULL; ++p) {
/*
 * The value of the mode argument is not valid.
 */
		fp = fmemopen(NULL, 1, *p);
		ATF_CHECK(fp == NULL);
		ATF_CHECK(errno == EINVAL);
	}
}

ATF_TC(test07);
ATF_TC_HEAD(test07, tc)
{
	atf_tc_set_md_var(tc, "descr", "test07");
}
ATF_TC_BODY(test07, tc)
{
#if !defined(__GLIBC__)
	const char **p;
	const char *mode[] = {
	    "r", "rb",
	    "w", "wb",
	    "a", "ab",
	    NULL
	};
	FILE *fp;

	for (p = &mode[0]; *p != NULL; ++p) {
/*
 * Because this feature is only useful when the stream is opened for updating
 * (because there is no way to get a pointer to the buffer) the fmemopen()
 * call may fail if the mode argument does not include a '+' . 
 */
		errno = 0;
		fp = fmemopen(NULL, 1, *p);
		ATF_CHECK(fp == NULL);
		ATF_CHECK(errno == EINVAL);
	}
#endif
}

ATF_TC(test08);
ATF_TC_HEAD(test08, tc)
{
	atf_tc_set_md_var(tc, "descr", "test08");
}
ATF_TC_BODY(test08, tc)
{
#if !defined(__GLIBC__)
	const char **p;
	const char *mode[] = {
	    "r+", "rb+", "r+b",
	    "w+", "wb+", "w+b",
	    "a+", "ab+", "a+b",
	    NULL
	};
	FILE *fp;

	for (p = &mode[0]; *p != NULL; ++p) {
/*
 * The buf argument is a null pointer and the allocation of a buffer of
 * length size has failed.
 */
		fp = fmemopen(NULL, SIZE_MAX, *p);
		ATF_CHECK(fp == NULL);
		ATF_CHECK(errno == ENOMEM);
	}
#endif
}

/*
 * test09 - test14:
 * An attempt to seek a memory buffer stream to a negative position or to a
 * position larger than the buffer size given in the size argument shall fail.
 */

ATF_TC(test09);
ATF_TC_HEAD(test09, tc)
{
	atf_tc_set_md_var(tc, "descr", "test09");
}
ATF_TC_BODY(test09, tc)
{
	struct testcase *t;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;
	off_t i;

	for (t = &testcases[0]; t->s != NULL; ++t) {
		for (p = &mode_rwa[0]; *p != NULL; ++p) {

			memcpy(&buf[0], t->s, t->n);
			fp = fmemopen(&buf[0], t->n, *p);
			ATF_CHECK(fp != NULL);

/*
 * test fmemopen_seek(SEEK_SET)
 */
			/* zero */
			ATF_CHECK(fseeko(fp, (off_t)0, SEEK_SET) == 0);
			ATF_CHECK(ftello(fp) == (off_t)0);

			/* positive */
			for (i = (off_t)1; i <= (off_t)t->n; ++i) {
				ATF_CHECK(fseeko(fp, i, SEEK_SET) == 0);
				ATF_CHECK(ftello(fp) == i);
			}
			/* positive + OOB */
			ATF_CHECK(fseeko(fp, t->n + 1, SEEK_SET) == -1);
			ATF_CHECK(ftello(fp) == t->n);

			/* negative + OOB */
			ATF_CHECK(fseeko(fp, (off_t)-1, SEEK_SET) == -1);
			ATF_CHECK(ftello(fp) == t->n);

			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

const char *mode_rw[] = {
    "r", "rb", "r+", "rb+", "r+b",
    "w", "wb", "w+", "wb+", "w+b",
    NULL
};

ATF_TC(test10);
ATF_TC_HEAD(test10, tc)
{
	atf_tc_set_md_var(tc, "descr", "test10");
}
ATF_TC_BODY(test10, tc)
{
	struct testcase *t;
	off_t i;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	for (t = &testcases[0]; t->s != NULL; ++t) {
		for (p = &mode_rw[0]; *p != NULL; ++p) {

			memcpy(&buf[0], t->s, t->n);
			fp = fmemopen(&buf[0], t->n, *p);
			ATF_CHECK(fp != NULL);

/*
 * test fmemopen_seek(SEEK_CUR)
 */
			ATF_CHECK(ftello(fp) == (off_t)0);

			/* zero */
			ATF_CHECK(fseeko(fp, (off_t)0, SEEK_CUR) == 0);
			ATF_CHECK(ftello(fp) == (off_t)0);

			/* negative & OOB */
			ATF_CHECK(fseeko(fp, (off_t)-1, SEEK_CUR) == -1);
			ATF_CHECK(ftello(fp) == (off_t)0);

			/* positive */
			for (i = 0; i < (off_t)t->n; ++i) {
				ATF_CHECK(fseeko(fp, (off_t)1, SEEK_CUR) == 0);
				ATF_CHECK(ftello(fp) == i + 1);
			}

			/* positive & OOB */
			ATF_CHECK(fseeko(fp, (off_t)1, SEEK_CUR) == -1);
			ATF_CHECK(ftello(fp) == (off_t)t->n);

			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

ATF_TC(test11);
ATF_TC_HEAD(test11, tc)
{
	atf_tc_set_md_var(tc, "descr", "test11");
}
ATF_TC_BODY(test11, tc)
{
	struct testcase *t;
	off_t len, rest, i;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	/* test fmemopen_seek(SEEK_CUR) */
	for (t = &testcases[0]; t->s != NULL; ++t) {
		len = (off_t)strnlen(t->s, t->n);
		rest = (off_t)t->n - len;
		for (p = &mode_a[0]; *p != NULL; ++p) {

			memcpy(&buf[0], t->s, t->n);
			fp = fmemopen(&buf[0], t->n, *p);
			ATF_CHECK(fp != NULL);
/*
 * test fmemopen_seek(SEEK_CUR)
 */
#if defined(__GLIBC__)
			if (i < (off_t)t->n) {
#endif
			/* zero */
			ATF_CHECK(fseeko(fp, (off_t)0, SEEK_CUR) == 0);
			ATF_CHECK(ftello(fp) == len);

			/* posive */
			for (i = (off_t)1; i <= rest; ++i) {
				ATF_CHECK(fseeko(fp, (off_t)1, SEEK_CUR) == 0);
				ATF_CHECK(ftello(fp) == len + i);
			}

			/* positive + OOB */
			ATF_CHECK(fseeko(fp, (off_t)1, SEEK_CUR) == -1);
			ATF_CHECK(ftello(fp) == (off_t)t->n);

			/* negative */
			for (i = (off_t)1; i <= (off_t)t->n; ++i) {
				ATF_CHECK(fseeko(fp, (off_t)-1, SEEK_CUR) == 0);
				ATF_CHECK(ftello(fp) == (off_t)t->n - i);
			}

			/* negative + OOB */
			ATF_CHECK(fseeko(fp, (off_t)-1, SEEK_CUR) == -1);
			ATF_CHECK(ftello(fp) == (off_t)0);

#if defined(__GLIBC__)
			}
#endif
			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

#ifndef __FreeBSD__
ATF_TC(test12);
ATF_TC_HEAD(test12, tc)
{
	atf_tc_set_md_var(tc, "descr", "test12");
}
ATF_TC_BODY(test12, tc)
{
	struct testcase *t;
	off_t len, rest, i;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	/* test fmemopen_seek(SEEK_END) */
	for (t = &testcases[0]; t->s != NULL; ++t) {
		len = (off_t)strnlen(t->s, t->n);
		rest = t->n - len;
		for (p = &mode_r[0]; *p != NULL; ++p) {

			memcpy(buf, t->s, t->n);
			fp = fmemopen(&buf[0], t->n, *p);
			ATF_CHECK(fp != NULL);

/*
 * test fmemopen_seek(SEEK_END)
 */
#if !defined(__GLIBC__)
			ATF_CHECK(ftello(fp) == (off_t)0);

			/* zero */
			ATF_CHECK(fseeko(fp, (off_t)0, SEEK_END) == 0);
			ATF_CHECK(ftello(fp) == len);

			/* positive + OOB */
			ATF_CHECK(fseeko(fp, rest + 1, SEEK_END) == -1);
			ATF_CHECK(ftello(fp) == len);

			/* negative + OOB */
			ATF_CHECK(fseeko(fp, -(len + 1), SEEK_END) == -1);
			ATF_CHECK(ftello(fp) == len);

			/* positive */
			for (i = 1; i <= rest; ++i) {
				ATF_CHECK(fseeko(fp, i, SEEK_END) == 0);
				ATF_CHECK(ftello(fp) == len + i);
			}

			/* negative */
			for (i = 1; i < len; ++i) {
				ATF_CHECK(fseeko(fp, -i, SEEK_END) == 0);
				ATF_CHECK(ftello(fp) == len - i);
			}
#endif
			ATF_CHECK(fclose(fp) == 0);
		}
	}
}
#endif

ATF_TC(test13);
ATF_TC_HEAD(test13, tc)
{
	atf_tc_set_md_var(tc, "descr", "test13");
}
ATF_TC_BODY(test13, tc)
{
	struct testcase *t;
#ifndef __FreeBSD__
	off_t i;
#endif
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	/* test fmemopen_seek(SEEK_END) */
	for (t = &testcases[0]; t->s != NULL; ++t) {
		for (p = &mode_w[0]; *p != NULL; ++p) {

			memcpy(buf, t->s, t->n);
			fp = fmemopen(&buf[0], t->n, *p);
			ATF_CHECK(fp != NULL);
/*
 * test fmemopen_seek(SEEK_END)
 */
#if !defined(__GLIBC__)
			ATF_CHECK(ftello(fp) == (off_t)0);
			ATF_CHECK(buf[0] == '\0');

			/* zero */
			ATF_CHECK(fseeko(fp, (off_t)0, SEEK_END) == 0);
			ATF_CHECK(ftello(fp) == (off_t)0);

			/* positive + OOB */
			ATF_CHECK(fseeko(fp, (off_t)t->n + 1, SEEK_END) == -1);
			ATF_CHECK(ftello(fp) == (off_t)0);

			/* negative + OOB */
			ATF_CHECK(fseeko(fp, -1, SEEK_END) == -1);
			ATF_CHECK(ftello(fp) == (off_t)0);
#endif

#ifndef __FreeBSD__
			/* positive */
			for (i = 1; i <= t->n; ++i) {
				ATF_CHECK(fseeko(fp, i, SEEK_END) == 0);
				ATF_CHECK(ftello(fp) == i);
			}
#endif
			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

ATF_TC(test14);
ATF_TC_HEAD(test14, tc)
{
	atf_tc_set_md_var(tc, "descr", "test14");
}
ATF_TC_BODY(test14, tc)
{
	struct testcase *t;
	off_t len, rest, i;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	/* test fmemopen_seek(SEEK_END) */
	for (t = &testcases[0]; t->s != NULL; ++t) {
		len = (off_t)strnlen(t->s, t->n);
		rest = (off_t)t->n - len;
		for (p = &mode_a[0]; *p != NULL; ++p) {

			memcpy(buf, t->s, t->n);
			fp = fmemopen(&buf[0], t->n, *p);
			ATF_CHECK(fp != NULL);
/*
 * test fmemopen_seek(SEEK_END)
 */
#if !defined(__GLIBC__)
			ATF_CHECK(ftello(fp) == len);

			/* zero */
			ATF_CHECK(fseeko(fp, 0, SEEK_END) == 0);
			ATF_CHECK(ftello(fp) == len);

			/* positive + OOB */
			ATF_CHECK(fseeko(fp, rest + 1, SEEK_END) == -1);
			ATF_CHECK(ftello(fp) == len);

			/* negative + OOB */
			ATF_CHECK(fseeko(fp, -(len + 1), SEEK_END) == -1);
			ATF_CHECK(ftello(fp) == len);

#ifndef __FreeBSD__
			/* positive */
			for (i = 1; i <= rest; ++i) {
				ATF_CHECK(fseeko(fp, i, SEEK_END) == 0);
				ATF_CHECK(ftello(fp) == len + i);
			}
#endif

			/* negative */
			for (i = 1; i < len; ++i) {
				ATF_CHECK(fseeko(fp, -i, SEEK_END) == 0);
				ATF_CHECK(ftello(fp) == len - i);
			}
#endif
			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

const char *mode_rw1[] = {
    "r", "rb", "r+", "rb+", "r+b",
    "w+", "wb+",
    NULL
};

#ifndef __FreeBSD__

/* test15 - 18:
 * When a stream open for writing is flushed or closed, a null byte is written
 * at the current position or at the end of the buffer, depending on the size
 * of the contents.
 */

ATF_TC(test15);
ATF_TC_HEAD(test15, tc)
{
	atf_tc_set_md_var(tc, "descr", "test15");
}
ATF_TC_BODY(test15, tc)
{
	struct testcase *t;
	const char **p;
	char buf0[BUFSIZ];
	FILE *fp;
	int i;

	for (t = &testcases[0]; t->s != NULL; ++t) {
		for (p = &mode_rw1[0]; *p != NULL; ++p) {

			memcpy(&buf0[0], t->s, t->n);
			fp = fmemopen(&buf0[0], t->n, *p);
			ATF_CHECK(fp != NULL);
/*
 * test fmemopen_read + fgetc(3)
 */
			for (i = 0; i < t->n; ++i) {
				ATF_CHECK(ftello(fp) == (off_t)i);
				ATF_CHECK(fgetc(fp) == buf0[i]);
				ATF_CHECK(feof(fp) == 0);
				ATF_CHECK(ftello(fp) == (off_t)i + 1);
			}
			ATF_CHECK(fgetc(fp) == EOF);
			ATF_CHECK(feof(fp) != 0);
			ATF_CHECK(ftello(fp) == (off_t)t->n);
			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

ATF_TC(test16);
ATF_TC_HEAD(test16, tc)
{
	atf_tc_set_md_var(tc, "descr", "test16");
}
ATF_TC_BODY(test16, tc)
{
	struct testcase *t;
	const char **p;
	char buf0[BUFSIZ], buf1[BUFSIZ];
	FILE *fp;

	for (t = &testcases[0]; t->s != NULL; ++t) {
		for (p = &mode_rw1[0]; *p != NULL; ++p) {

			memcpy(&buf0[0], t->s, t->n);
			buf1[t->n] = 0x1;
			fp = fmemopen(&buf0[0], t->n, *p);
			ATF_CHECK(fp != NULL);
/*
 * test fmemopen_read + fread(4)
 */
			ATF_CHECK(ftello(fp) == (off_t)0);
			ATF_CHECK(fread(&buf1[0], 1, sizeof(buf1), fp) == (size_t)t->n);
			ATF_CHECK(feof(fp) != 0);
			ATF_CHECK(memcmp(&buf0[0], &buf1[0], t->n) == 0);
			ATF_CHECK((unsigned char)buf1[t->n] == 0x1);

			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

const char *mode_a1[] = { "a+", "ab+", NULL };

ATF_TC(test17);
ATF_TC_HEAD(test17, tc)
{
	atf_tc_set_md_var(tc, "descr", "test17");
}
ATF_TC_BODY(test17, tc)
{
	struct testcase *t;
	size_t len;
	int i;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	for (t = &testcases[0]; t->s != NULL; ++t) {
		len = strnlen(t->s, t->n);
		for (p = &mode_a1[0]; *p != NULL; ++p) {

			memcpy(&buf[0], t->s, t->n);
			fp = fmemopen(&buf[0], t->n, *p);
			ATF_CHECK(fp != NULL);
/*
 * test fmemopen_read + fgetc(3)
 */
#if defined(__GLIBC__)
			if (i < t->n) {
#endif
			for (i = len; i < t->n; ++i) {
				ATF_CHECK(ftello(fp) == (off_t)i);
				ATF_CHECK(fgetc(fp) == buf[i]);
				ATF_CHECK(feof(fp) == 0);
				ATF_CHECK(ftello(fp) == (off_t)i + 1);
			}
			ATF_CHECK(fgetc(fp) == EOF);
			ATF_CHECK(feof(fp) != 0);
			ATF_CHECK(ftello(fp) == (off_t)t->n);
			rewind(fp);
			for (i = 0; i < t->n; ++i) {
				ATF_CHECK(ftello(fp) == (off_t)i);
				ATF_CHECK(fgetc(fp) == buf[i]);
				ATF_CHECK(feof(fp) == 0);
				ATF_CHECK(ftello(fp) == (off_t)i + 1);
			}
			ATF_CHECK(fgetc(fp) == EOF);
			ATF_CHECK(feof(fp) != 0);
			ATF_CHECK(ftello(fp) == (off_t)t->n);
#if defined(__GLIBC__)
			}
#endif
			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

ATF_TC(test18);
ATF_TC_HEAD(test18, tc)
{
	atf_tc_set_md_var(tc, "descr", "test18");
}
ATF_TC_BODY(test18, tc)
{
	struct testcase *t;
	size_t len;
	const char **p;
	char buf0[BUFSIZ], buf1[BUFSIZ];
	FILE *fp;

	for (t = &testcases[0]; t->s != NULL; ++t) {
		len = strnlen(t->s, t->n);
		for (p = &mode_a1[0]; *p != NULL; ++p) {

			memcpy(&buf0[0], t->s, t->n);
			buf1[t->n - len] = 0x1;
			fp = fmemopen(&buf0[0], t->n, *p);
			ATF_CHECK(fp != NULL);
/*
 * test fmemopen_read + fread(3)
 */
#if defined(__GLIBC__)
			if (i < t->n) {
#endif
			ATF_CHECK(ftello(fp) == (off_t)len);
			ATF_CHECK(fread(&buf1[0], 1, sizeof(buf1), fp)
			    == t->n - len);
			ATF_CHECK(feof(fp) != 0);
			ATF_CHECK(!memcmp(&buf0[len], &buf1[0], t->n - len));
			ATF_CHECK((unsigned char)buf1[t->n - len] == 0x1);
			rewind(fp);
			buf1[t->n] = 0x1;
			ATF_CHECK(ftello(fp) == (off_t)0);
			ATF_CHECK(fread(&buf1[0], 1, sizeof(buf1), fp)
			    == (size_t)t->n);
			ATF_CHECK(feof(fp) != 0);
			ATF_CHECK(!memcmp(&buf0[0], &buf1[0], t->n));
			ATF_CHECK((unsigned char)buf1[t->n] == 0x1);
#if defined(__GLIBC__)
			}
#endif
			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

/*
 * test19 - test22:
 * If a stream open for update is flushed or closed and the last write has
 * advanced the current buffer size, a null byte is written at the end of the
 * buffer if it fits.
 */

const char *mode_rw2[] = {
    "r+", "rb+", "r+b",
    "w", "wb", "w+", "wb+", "w+b",
    NULL
};

ATF_TC(test19);
ATF_TC_HEAD(test19, tc)
{
	atf_tc_set_md_var(tc, "descr", "test19");
}
ATF_TC_BODY(test19, tc)
{
	struct testcase *t;
	int i;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	for (t = &testcases[0]; t->s != NULL; ++t) {
		for (p = &mode_rw2[0]; *p != NULL; ++p) {

			memcpy(&buf[0], t->s, t->n);
			buf[t->n] = 0x1;
			fp = fmemopen(&buf[0], t->n + 1, *p);
			ATF_CHECK(fp != NULL);
			setbuf(fp, NULL);
/*
 * test fmemopen_write + fputc(3)
 */
			for (i = 0; i < t->n; ++i) {
				ATF_CHECK(ftello(fp) == (off_t)i);
				ATF_CHECK(fputc(t->s[i], fp) == t->s[i]);
				ATF_CHECK(buf[i] == t->s[i]);
				ATF_CHECK(ftello(fp) == (off_t)i + 1);
				ATF_CHECK(buf[i] == t->s[i]);
#if !defined(__GLIBC__)
				ATF_CHECK(buf[i + 1] == '\0');
#endif
			}

/* don't accept non nul character at end of buffer */
			ATF_CHECK(fputc(0x1, fp) == EOF);
			ATF_CHECK(ftello(fp) == (off_t)t->n);
			ATF_CHECK(feof(fp) == 0);

/* accept nul character at end of buffer */
			ATF_CHECK(fputc('\0', fp) == '\0');
			ATF_CHECK(ftello(fp) == (off_t)t->n + 1);
			ATF_CHECK(feof(fp) == 0);

/* reach EOF */
			ATF_CHECK(fputc('\0', fp) == EOF);
			ATF_CHECK(ftello(fp) == (off_t)t->n + 1);

			/* compare */
			ATF_CHECK(memcmp(&buf[0], t->s, t->n) == 0);
			ATF_CHECK(buf[t->n] == '\0');

			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

ATF_TC(test20);
ATF_TC_HEAD(test20, tc)
{
	atf_tc_set_md_var(tc, "descr", "test20");
}
ATF_TC_BODY(test20, tc)
{
	struct testcase *t;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	for (t = &testcases[0]; t->s != NULL; ++t) {
		for (p = &mode_rw2[0]; *p != NULL; ++p) {

			memcpy(&buf[0], t->s, t->n);
			buf[t->n] = 0x1;
			fp = fmemopen(&buf[0], t->n + 1, *p);
			ATF_CHECK(fp != NULL);
			setbuf(fp, NULL);
			ATF_CHECK(fwrite(t->s, 1, t->n, fp) == (size_t)t->n);
/*
 * test fmemopen_write + fwrite(3)
 */
#if !defined(__GLIBC__)
			ATF_CHECK(buf[t->n] == '\0');

/* don't accept non nul character at end of buffer */
			ATF_CHECK(fwrite("\x1", 1, 1, fp) == 0);
			ATF_CHECK(ftello(fp) == (off_t)t->n);
			ATF_CHECK(feof(fp) == 0);
#endif

/* accept nul character at end of buffer */
			ATF_CHECK(fwrite("\x0", 1, 1, fp) == 1);
			ATF_CHECK(ftello(fp) == (off_t)t->n + 1);
			ATF_CHECK(feof(fp) == 0);

/* reach EOF */
			ATF_CHECK(fputc('\0', fp) == EOF);
			ATF_CHECK(ftello(fp) == (off_t)t->n + 1);

/* compare */
			ATF_CHECK(memcmp(&buf[0], t->s, t->n) == 0);
			ATF_CHECK(buf[t->n] == '\0');

			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

ATF_TC(test21);
ATF_TC_HEAD(test21, tc)
{
	atf_tc_set_md_var(tc, "descr", "test21");
}
ATF_TC_BODY(test21, tc)
{
	struct testcase *t;
	int len, i;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	for (t = &testcases[0]; t->s != NULL; ++t) {
		len = strnlen(t->s, t->n);
		for (p = &mode_a[0]; *p != NULL; ++p) {
			memcpy(&buf[0], t->s, t->n);
			fp = fmemopen(&buf[0], t->n, *p);
			ATF_CHECK(fp != NULL);
			setbuf(fp, NULL);
/*
 * test fmemopen_write + fputc(3)
 */
			if (len < t->n) {
				for (i = len; i < t->n - 1; ++i) {
					ATF_CHECK(ftello(fp) == (off_t)i);
					ATF_CHECK(fputc(t->s[i - len], fp)
					    == t->s[i - len]);
					ATF_CHECK(buf[i] == t->s[i - len]);
					ATF_CHECK(ftello(fp) == (off_t)i + 1);
#if !defined(__GLIBC__)
					ATF_CHECK(buf[i + 1] == '\0');
#endif
				}

/* don't accept non nul character at end of buffer */
				ATF_CHECK(ftello(fp) == (off_t)t->n - 1);
				ATF_CHECK(fputc(0x1, fp) == EOF);
				ATF_CHECK(ftello(fp) == (off_t)t->n - 1);

/* accept nul character at end of buffer */
				ATF_CHECK(ftello(fp) == (off_t)t->n - 1);
				ATF_CHECK(fputc('\0', fp) == '\0');
				ATF_CHECK(ftello(fp) == (off_t)t->n);
			}

/* reach EOF */
			ATF_CHECK(ftello(fp) == (off_t)t->n);
			ATF_CHECK(fputc('\0', fp) == EOF);
			ATF_CHECK(ftello(fp) == (off_t)t->n);

			ATF_CHECK(fclose(fp) == 0);
		}
	}
}

ATF_TC(test22);
ATF_TC_HEAD(test22, tc)
{
	atf_tc_set_md_var(tc, "descr", "test22");
}
ATF_TC_BODY(test22, tc)
{
	struct testcase *t0, *t1;
	size_t len0, len1, nleft;
	const char **p;
	char buf[BUFSIZ];
	FILE *fp;

	for (t0 = &testcases[0]; t0->s != NULL; ++t0) {
		len0 = strnlen(t0->s, t0->n);
		for (t1 = &testcases[0]; t1->s != NULL; ++t1) {
			len1 = strnlen(t1->s, t1->n);
			for (p = &mode_a[0]; *p != NULL; ++p) {

				memcpy(&buf[0], t0->s, t0->n);
				fp = fmemopen(&buf[0], t0->n, *p);
				ATF_CHECK(fp != NULL);
				setbuf(fp, NULL);
/*
 * test fmemopen_write + fwrite(3)
 */
				nleft = t0->n - len0;
#if !defined(__GLIBC__)
				if (nleft == 0 || len1 == nleft - 1) {
					ATF_CHECK(fwrite(t1->s, 1, t1->n, fp)
					    == nleft);
					ATF_CHECK(ftell(fp) == t1->n);
				} else {
					ATF_CHECK(fwrite(t1->s, 1, t1->n, fp)
					    == nleft - 1);
					ATF_CHECK(ftell(fp) == t1->n - 1);
				}
#endif
				ATF_CHECK(fclose(fp) == 0);
			}
		}
	}
}
#endif

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, test00);
	ATF_TP_ADD_TC(tp, test01);
	ATF_TP_ADD_TC(tp, test02);
	ATF_TP_ADD_TC(tp, test03);
	ATF_TP_ADD_TC(tp, test04);
	ATF_TP_ADD_TC(tp, test05);
	ATF_TP_ADD_TC(tp, test06);
	ATF_TP_ADD_TC(tp, test07);
	ATF_TP_ADD_TC(tp, test08);
	ATF_TP_ADD_TC(tp, test09);
	ATF_TP_ADD_TC(tp, test10);
	ATF_TP_ADD_TC(tp, test11);
#ifndef __FreeBSD__
	ATF_TP_ADD_TC(tp, test12);
#endif
	ATF_TP_ADD_TC(tp, test13);
	ATF_TP_ADD_TC(tp, test14);
#ifndef __FreeBSD__
	ATF_TP_ADD_TC(tp, test15);
	ATF_TP_ADD_TC(tp, test16);
	ATF_TP_ADD_TC(tp, test17);
	ATF_TP_ADD_TC(tp, test18);
	ATF_TP_ADD_TC(tp, test19);
	ATF_TP_ADD_TC(tp, test20);
	ATF_TP_ADD_TC(tp, test21);
	ATF_TP_ADD_TC(tp, test22);
#endif

	return atf_no_error();
}
