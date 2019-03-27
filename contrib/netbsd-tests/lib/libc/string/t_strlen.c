/* $NetBSD: t_strlen.c,v 1.6 2017/01/14 20:49:24 christos Exp $ */

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
#include <unistd.h>

static void	write_num(int);

static void
write_num(int val)
{
	char buf[20];
	int i;

	for (i = sizeof buf; --i >= 0;) {
		buf[i] = '0' + val % 10;
		val /= 10;
		if (val == 0) {
			write(2, buf + i, sizeof buf - i);
			return;
		}
	}
	write(2, "overflow", 8);
}

ATF_TC(strlen_basic);
ATF_TC_HEAD(strlen_basic, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test strlen(3) results");
}

ATF_TC_BODY(strlen_basic, tc)
{
	void *dl_handle;
	/* try to trick the compiler */
	size_t (*strlen_fn)(const char *);

	unsigned int a, t;
	size_t len;
	char buf[64];

	struct tab {
		const char*	val;
		size_t		len;
	};

	const struct tab tab[] = {
		/*
		 * patterns that check for all combinations of leading and
		 * trailing unaligned characters (on a 64 bit processor)
		 */

		{ "",				0 },
		{ "a",				1 },
		{ "ab",				2 },
		{ "abc",			3 },
		{ "abcd",			4 },
		{ "abcde",			5 },
		{ "abcdef",			6 },
		{ "abcdefg",			7 },
		{ "abcdefgh",			8 },
		{ "abcdefghi",			9 },
		{ "abcdefghij",			10 },
		{ "abcdefghijk",		11 },
		{ "abcdefghijkl",		12 },
		{ "abcdefghijklm",		13 },
		{ "abcdefghijklmn",		14 },
		{ "abcdefghijklmno",		15 },
		{ "abcdefghijklmnop",		16 },
		{ "abcdefghijklmnopq",		17 },
		{ "abcdefghijklmnopqr",		18 },
		{ "abcdefghijklmnopqrs",	19 },
		{ "abcdefghijklmnopqrst",	20 },
		{ "abcdefghijklmnopqrstu",	21 },
		{ "abcdefghijklmnopqrstuv",	22 },
		{ "abcdefghijklmnopqrstuvw",	23 },

		/*
		 * patterns that check for the cases where the expression:
		 *
		 *	((word - 0x7f7f..7f) & 0x8080..80)
		 *
		 * returns non-zero even though there are no zero bytes in
		 * the word.
		 */

		{ "" "\xff\xff\xff\xff\xff\xff\xff\xff" "abcdefgh",	16 },
		{ "a" "\xff\xff\xff\xff\xff\xff\xff\xff" "bcdefgh",	16 },
		{ "ab" "\xff\xff\xff\xff\xff\xff\xff\xff" "cdefgh",	16 },
		{ "abc" "\xff\xff\xff\xff\xff\xff\xff\xff" "defgh",	16 },
		{ "abcd" "\xff\xff\xff\xff\xff\xff\xff\xff" "efgh",	16 },
		{ "abcde" "\xff\xff\xff\xff\xff\xff\xff\xff" "fgh",	16 },
		{ "abcdef" "\xff\xff\xff\xff\xff\xff\xff\xff" "gh",	16 },
		{ "abcdefg" "\xff\xff\xff\xff\xff\xff\xff\xff" "h",	16 },
		{ "abcdefgh" "\xff\xff\xff\xff\xff\xff\xff\xff" "",	16 },
	};

	/*
	 * During testing it is useful have the rest of the program
	 * use a known good version!
	 */
	dl_handle = dlopen(NULL, RTLD_LAZY);
	strlen_fn = dlsym(dl_handle, "test_strlen");
	if (!strlen_fn)
		strlen_fn = strlen;

	for (a = 0; a < sizeof(long); ++a) {
		for (t = 0; t < (sizeof(tab) / sizeof(tab[0])); ++t) {

			memcpy(&buf[a], tab[t].val, tab[t].len + 1);
			len = strlen_fn(&buf[a]);

			if (len != tab[t].len) {
				/* Write error without using printf / strlen */
				write(2, "alignment ", 10);
				write_num(a);
				write(2, ", test ", 7);
				write_num(t);
				write(2, ", got len ", 10);
				write_num(len);
				write(2, ", not ", 6);
				write_num(tab[t].len);
				write(2, ", for '", 7);
				write(2, tab[t].val, tab[t].len);
				write(2, "'\n", 2);
				atf_tc_fail("See stderr for details");
			}
		}
	}
	(void)dlclose(dl_handle);
}

ATF_TC(strlen_huge);
ATF_TC_HEAD(strlen_huge, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test strlen(3) with huge strings");
}

ATF_TC_BODY(strlen_huge, tc)
{
	long page;
	char *str;
	size_t i;

	page = sysconf(_SC_PAGESIZE);
	ATF_REQUIRE(page >= 0);

	for (i = 1; i < 1000; i = i + 100) {

		str = malloc(i * page + 1);

		if (str == NULL)
			continue;

		(void)memset(str, 'x', i * page);
		str[i * page] = '\0';

		ATF_REQUIRE(strlen(str) == i * page);
		free(str);
	}
}

ATF_TC(strnlen_basic);
ATF_TC_HEAD(strnlen_basic, tc)
{
        atf_tc_set_md_var(tc, "descr", "A naive test of strnlen(3)");
}

ATF_TC_BODY(strnlen_basic, tc)
{
	char buf[1];

	buf[0] = '\0';

	ATF_CHECK(strnlen(buf, 000) == 0);
	ATF_CHECK(strnlen(buf, 111) == 0);

	ATF_CHECK(strnlen("xxx", 0) == 0);
	ATF_CHECK(strnlen("xxx", 1) == 1);
	ATF_CHECK(strnlen("xxx", 2) == 2);
	ATF_CHECK(strnlen("xxx", 3) == 3);
	ATF_CHECK(strnlen("xxx", 9) == 3);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, strlen_basic);
	ATF_TP_ADD_TC(tp, strlen_huge);
	ATF_TP_ADD_TC(tp, strnlen_basic);

	return atf_no_error();
}
