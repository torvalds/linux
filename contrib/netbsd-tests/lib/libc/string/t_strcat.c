/* $NetBSD: t_strcat.c,v 1.2 2011/07/14 05:46:04 jruoho Exp $ */

/*
 * Written by J.T. Conklin <jtc@acorntoolworks.com>
 * Public domain.
 */

#include <atf-c.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

ATF_TC(strcat_basic);
ATF_TC_HEAD(strcat_basic, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test strcat(3) results");
}

ATF_TC_BODY(strcat_basic, tc)
{
	/* try to trick the compiler */
	char * (*f)(char *, const char *s) = strcat;

	unsigned int a0, a1, t0, t1;
	char buf0[64];
	char buf1[64];
	char *ret;

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

	for (a0 = 0; a0 < sizeof(long); ++a0) {
		for (a1 = 0; a1 < sizeof(long); ++a1) {
			for (t0 = 0; t0 < __arraycount(tab); ++t0) {
				for (t1 = 0; t1 < __arraycount(tab); ++t1) {

					memcpy(&buf0[a0], tab[t0].val,
					    tab[t0].len + 1);
					memcpy(&buf1[a1], tab[t1].val,
					    tab[t1].len + 1);

					ret = f(&buf0[a0], &buf1[a1]);

					/*
					 * verify strcat returns address
					 * of first parameter
					 */
					if (&buf0[a0] != ret) {
						fprintf(stderr, "a0 %d, a1 %d, "
						    "t0 %d, t1 %d\n",
						    a0, a1, t0, t1);
						atf_tc_fail("strcat did not "
						    "return its first arg");
					}

					/* verify string copied correctly */
					if (memcmp(&buf0[a0] + tab[t0].len,
						   &buf1[a1],
						   tab[t1].len + 1) != 0) {
						fprintf(stderr, "a0 %d, a1 %d, "
						    "t0 %d, t1 %d\n",
						    a0, a1, t0, t1);
						atf_tc_fail("string not copied "
						    "correctly");
					}
				}
			}
		}
	}
}

ATF_TC(strncat_simple);
ATF_TC_HEAD(strncat_simple, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test strncat(3) results");
}

ATF_TC_BODY(strncat_simple, tc)
{
	char buf[100] = "abcdefg";

	ATF_CHECK(strncat(buf, "xxx", 0) == buf);
	ATF_CHECK(strcmp(buf, "abcdefg") == 0);
	ATF_CHECK(strncat(buf, "xxx", 1) == buf);
	ATF_CHECK(strcmp(buf, "abcdefgx") == 0);
	ATF_CHECK(strncat(buf, "xxx", 2) == buf);
	ATF_CHECK(strcmp(buf, "abcdefgxxx") == 0);
	ATF_CHECK(strncat(buf, "\0", 1) == buf);
	ATF_CHECK(strcmp(buf, "abcdefgxxx") == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, strcat_basic);
	ATF_TP_ADD_TC(tp, strncat_simple);

	return atf_no_error();
}
