/* $NetBSD: t_strchr.c,v 1.2 2017/01/10 15:34:49 christos Exp $ */

/*
 * Written by J.T. Conklin <jtc@acorntoolworks.com>
 * Public domain.
 */

#include <atf-c.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

static char	*slow_strchr(char *, int);
static void	 verify_strchr(char *, int, unsigned int, unsigned int);

char * (*volatile strchr_fn)(const char *, int);

static char *
slow_strchr(char *buf, int ch)
{
	unsigned char c = 1;

	ch &= 0xff;

	for (; c != 0; buf++) {
		c = *buf;
		if (c == ch)
			return buf;
	}
	return 0;
}

static void
verify_strchr(char *buf, int ch, unsigned int t, unsigned int a)
{
	const char *off, *ok_off;

	off = strchr_fn(buf, ch);
	ok_off = slow_strchr(buf, ch);
	if (off == ok_off)
		return;

	fprintf(stderr, "test_strchr(\"%s\", %#x) gave %zd not %zd (test %d, "
	    "alignment %d)\n",
	    buf, ch, off ? off - buf : -1, ok_off ? ok_off - buf : -1, t, a);

	atf_tc_fail("Check stderr for details");
}

ATF_TC(strchr_basic);
ATF_TC_HEAD(strchr_basic, tc)
{

        atf_tc_set_md_var(tc, "descr", "Test strchr(3) results");
}

ATF_TC_BODY(strchr_basic, tc)
{
	void *dl_handle;
	char *off;
	char buf[32];
	unsigned int t, a;

	const char *tab[] = {
		"",
		"a",
		"aa",
		"abc",
		"abcd",
		"abcde",
		"abcdef",
		"abcdefg",
		"abcdefgh",

		"/",
		"//",
		"/a",
		"/a/",
		"/ab",
		"/ab/",
		"/abc",
		"/abc/",
		"/abcd",
		"/abcd/",
		"/abcde",
		"/abcde/",
		"/abcdef",
		"/abcdef/",
		"/abcdefg",
		"/abcdefg/",
		"/abcdefgh",
		"/abcdefgh/",

		"a/",
		"a//",
		"a/a",
		"a/a/",
		"a/ab",
		"a/ab/",
		"a/abc",
		"a/abc/",
		"a/abcd",
		"a/abcd/",
		"a/abcde",
		"a/abcde/",
		"a/abcdef",
		"a/abcdef/",
		"a/abcdefg",
		"a/abcdefg/",
		"a/abcdefgh",
		"a/abcdefgh/",

		"ab/",
		"ab//",
		"ab/a",
		"ab/a/",
		"ab/ab",
		"ab/ab/",
		"ab/abc",
		"ab/abc/",
		"ab/abcd",
		"ab/abcd/",
		"ab/abcde",
		"ab/abcde/",
		"ab/abcdef",
		"ab/abcdef/",
		"ab/abcdefg",
		"ab/abcdefg/",
		"ab/abcdefgh",
		"ab/abcdefgh/",

		"abc/",
		"abc//",
		"abc/a",
		"abc/a/",
		"abc/ab",
		"abc/ab/",
		"abc/abc",
		"abc/abc/",
		"abc/abcd",
		"abc/abcd/",
		"abc/abcde",
		"abc/abcde/",
		"abc/abcdef",
		"abc/abcdef/",
		"abc/abcdefg",
		"abc/abcdefg/",
		"abc/abcdefgh",
		"abc/abcdefgh/",

		"abcd/",
		"abcd//",
		"abcd/a",
		"abcd/a/",
		"abcd/ab",
		"abcd/ab/",
		"abcd/abc",
		"abcd/abc/",
		"abcd/abcd",
		"abcd/abcd/",
		"abcd/abcde",
		"abcd/abcde/",
		"abcd/abcdef",
		"abcd/abcdef/",
		"abcd/abcdefg",
		"abcd/abcdefg/",
		"abcd/abcdefgh",
		"abcd/abcdefgh/",

		"abcde/",
		"abcde//",
		"abcde/a",
		"abcde/a/",
		"abcde/ab",
		"abcde/ab/",
		"abcde/abc",
		"abcde/abc/",
		"abcde/abcd",
		"abcde/abcd/",
		"abcde/abcde",
		"abcde/abcde/",
		"abcde/abcdef",
		"abcde/abcdef/",
		"abcde/abcdefg",
		"abcde/abcdefg/",
		"abcde/abcdefgh",
		"abcde/abcdefgh/",

		"abcdef/",
		"abcdef//",
		"abcdef/a",
		"abcdef/a/",
		"abcdef/ab",
		"abcdef/ab/",
		"abcdef/abc",
		"abcdef/abc/",
		"abcdef/abcd",
		"abcdef/abcd/",
		"abcdef/abcde",
		"abcdef/abcde/",
		"abcdef/abcdef",
		"abcdef/abcdef/",
		"abcdef/abcdefg",
		"abcdef/abcdefg/",
		"abcdef/abcdefgh",
		"abcdef/abcdefgh/",

		"abcdefg/",
		"abcdefg//",
		"abcdefg/a",
		"abcdefg/a/",
		"abcdefg/ab",
		"abcdefg/ab/",
		"abcdefg/abc",
		"abcdefg/abc/",
		"abcdefg/abcd",
		"abcdefg/abcd/",
		"abcdefg/abcde",
		"abcdefg/abcde/",
		"abcdefg/abcdef",
		"abcdefg/abcdef/",
		"abcdefg/abcdefg",
		"abcdefg/abcdefg/",
		"abcdefg/abcdefgh",
		"abcdefg/abcdefgh/",

		"abcdefgh/",
		"abcdefgh//",
		"abcdefgh/a",
		"abcdefgh/a/",
		"abcdefgh/ab",
		"abcdefgh/ab/",
		"abcdefgh/abc",
		"abcdefgh/abc/",
		"abcdefgh/abcd",
		"abcdefgh/abcd/",
		"abcdefgh/abcde",
		"abcdefgh/abcde/",
		"abcdefgh/abcdef",
		"abcdefgh/abcdef/",
		"abcdefgh/abcdefg",
		"abcdefgh/abcdefg/",
		"abcdefgh/abcdefgh",
		"abcdefgh/abcdefgh/",
	};

	dl_handle = dlopen(NULL, RTLD_LAZY);
	strchr_fn = dlsym(dl_handle, "test_strlen");
	if (!strchr_fn)
		strchr_fn = strchr;

	for (a = 3; a < 3 + sizeof(long); ++a) {
		/* Put char and a \0 before the buffer */
		buf[a-1] = '/';
		buf[a-2] = '0';
		buf[a-3] = 0xff;
		for (t = 0; t < (sizeof(tab) / sizeof(tab[0])); ++t) {
			int len = strlen(tab[t]) + 1;
			memcpy(&buf[a], tab[t], len);

			/* Put the char we are looking for after the \0 */
			buf[a + len] = '/';

			/* Check search for NUL at end of string */
			verify_strchr(buf + a, 0, t, a);

			/* Then for the '/' in the strings */
			verify_strchr(buf + a, '/', t, a);

		   	/* check zero extension of char arg */
		   	verify_strchr(buf + a, 0xffffff00 | '/', t, a);

		   	/* Replace all the '/' with 0xff */
		   	while ((off = slow_strchr(buf + a, '/')) != NULL)
				*off = 0xff;

			buf[a + len] = 0xff;

			/* Check we can search for 0xff as well as '/' */
			verify_strchr(buf + a, 0xff, t, a);
		}
	}
	(void)dlclose(dl_handle);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, strchr_basic);

	return atf_no_error();
}
