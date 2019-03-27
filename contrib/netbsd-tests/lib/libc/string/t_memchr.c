/* $NetBSD: t_memchr.c,v 1.3 2012/04/06 07:53:10 jruoho Exp $ */

/*
 * Written by J.T. Conklin <jtc@acorntoolworks.com>
 * Public domain.
 */

#include <atf-c.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

ATF_TC(memchr_basic);
ATF_TC_HEAD(memchr_basic, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test memchr(3) results, #1");
}

ATF_TC_BODY(memchr_basic, tc)
{
	/* try to trick the compiler */
	void * (*f)(const void *, int, size_t) = memchr;

	unsigned int a, t;
	void *off, *off2;
	char buf[32];

	struct tab {
		const char	*val;
		size_t  	 len;
		char		 match;
		ssize_t		 off;
	};

	const struct tab tab[] = {
		{ "",			0, 0, 0 },

		{ "/",			0, 0, 0 },
		{ "/",			1, 1, 0 },
		{ "/a",			2, 1, 0 },
		{ "/ab",		3, 1, 0 },
		{ "/abc",		4, 1, 0 },
		{ "/abcd",		5, 1, 0 },
		{ "/abcde",		6, 1, 0 },
		{ "/abcdef",		7, 1, 0 },
		{ "/abcdefg",		8, 1, 0 },

		{ "a/",			1, 0, 0 },
		{ "a/",			2, 1, 1 },
		{ "a/b",		3, 1, 1 },
		{ "a/bc",		4, 1, 1 },
		{ "a/bcd",		5, 1, 1 },
		{ "a/bcde",		6, 1, 1 },
		{ "a/bcdef",		7, 1, 1 },
		{ "a/bcdefg",		8, 1, 1 },

		{ "ab/",		2, 0, 0 },
		{ "ab/",		3, 1, 2 },
		{ "ab/c",		4, 1, 2 },
		{ "ab/cd",		5, 1, 2 },
		{ "ab/cde",		6, 1, 2 },
		{ "ab/cdef",		7, 1, 2 },
		{ "ab/cdefg",		8, 1, 2 },

		{ "abc/",		3, 0, 0 },
		{ "abc/",		4, 1, 3 },
		{ "abc/d",		5, 1, 3 },
		{ "abc/de",		6, 1, 3 },
		{ "abc/def",		7, 1, 3 },
		{ "abc/defg",		8, 1, 3 },

		{ "abcd/",		4, 0, 0 },
		{ "abcd/",		5, 1, 4 },
		{ "abcd/e",		6, 1, 4 },
		{ "abcd/ef",		7, 1, 4 },
		{ "abcd/efg",		8, 1, 4 },

		{ "abcde/",		5, 0, 0 },
		{ "abcde/",		6, 1, 5 },
		{ "abcde/f",		7, 1, 5 },
		{ "abcde/fg",		8, 1, 5 },

		{ "abcdef/",		6, 0, 0 },
		{ "abcdef/",		7, 1, 6 },
		{ "abcdef/g",		8, 1, 6 },

		{ "abcdefg/",		7, 0, 0 },
		{ "abcdefg/",		8, 1, 7 },

		{ "\xff\xff\xff\xff" "efg/",	8, 1, 7 },
		{ "a" "\xff\xff\xff\xff" "fg/",	8, 1, 7 },
		{ "ab" "\xff\xff\xff\xff" "g/",	8, 1, 7 },
		{ "abc" "\xff\xff\xff\xff" "/",	8, 1, 7 },
	};

	for (a = 1; a < 1 + sizeof(long); ++a) {
		for (t = 0; t < (sizeof(tab) / sizeof(tab[0])); ++t) {
			buf[a-1] = '/';
			strcpy(&buf[a], tab[t].val);

			off = f(&buf[a], '/', tab[t].len);
			if (tab[t].match == 0) {
				if (off != 0) {
					fprintf(stderr, "a = %d, t = %d\n",
					    a, t);
					atf_tc_fail("should not have found "
					    " char past len");
				}
			} else if (tab[t].match == 1) {
				if (tab[t].off != ((char*)off - &buf[a])) {
					fprintf(stderr, "a = %d, t = %d\n",
					    a, t);
					atf_tc_fail("char not found at "
					    "correct offset");
				}
	    		} else {
				fprintf(stderr, "a = %d, t = %d\n", a, t);
				atf_tc_fail("Corrupt test case data");
			}

			/* check zero extension of char arg */
			off2 = f(&buf[a], 0xffffff00 | '/', tab[t].len);
			if (off2 != off)
				atf_tc_fail("zero extension of char arg "
				    "failed");
		}
	}
}

ATF_TC(memchr_simple);
ATF_TC_HEAD(memchr_simple, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test memchr(3) results, #2");
}

ATF_TC_BODY(memchr_simple, tc)
{
	char buf[] = "abcdefg";
	short i = 7;

	ATF_CHECK(memchr(buf, 'a', 0) == NULL);
	ATF_CHECK(memchr(buf, 'g', 0) == NULL);
	ATF_CHECK(memchr(buf, 'x', 7) == NULL);

	ATF_CHECK(memchr("\0", 'x', 0) == NULL);
	ATF_CHECK(memchr("\0", 'x', 1) == NULL);

	while (i <= 14) {

		ATF_CHECK(memchr(buf, 'a', i) == buf + 0);
		ATF_CHECK(memchr(buf, 'b', i) == buf + 1);
		ATF_CHECK(memchr(buf, 'c', i) == buf + 2);
		ATF_CHECK(memchr(buf, 'd', i) == buf + 3);
		ATF_CHECK(memchr(buf, 'e', i) == buf + 4);
		ATF_CHECK(memchr(buf, 'f', i) == buf + 5);
		ATF_CHECK(memchr(buf, 'g', i) == buf + 6);

		i *= 2;
	}
}

ATF_TC(memrchr_simple);
ATF_TC_HEAD(memrchr_simple, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test memrchr(3) results");
}

ATF_TC_BODY(memrchr_simple, tc)
{
	char buf[] = "abcdabcd";

	ATF_CHECK(memrchr(buf, 'a', 0) == NULL);
	ATF_CHECK(memrchr(buf, 'g', 0) == NULL);
	ATF_CHECK(memrchr(buf, 'x', 8) == NULL);

	ATF_CHECK(memrchr("\0", 'x', 0) == NULL);
	ATF_CHECK(memrchr("\0", 'x', 1) == NULL);

	ATF_CHECK(memrchr(buf, 'a', 8) == buf + 4);
	ATF_CHECK(memrchr(buf, 'b', 8) == buf + 5);
	ATF_CHECK(memrchr(buf, 'c', 8) == buf + 6);
	ATF_CHECK(memrchr(buf, 'd', 8) == buf + 7);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, memchr_basic);
	ATF_TP_ADD_TC(tp, memchr_simple);
	ATF_TP_ADD_TC(tp, memrchr_simple);

	return atf_no_error();
}
