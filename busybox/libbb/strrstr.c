/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2008 Bernhard Reutner-Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/*
 * The strrstr() function finds the last occurrence of the substring needle
 * in the string haystack. The terminating nul characters are not compared.
 */
char* FAST_FUNC strrstr(const char *haystack, const char *needle)
{
	char *r = NULL;

	if (!needle[0])
		return (char*)haystack + strlen(haystack);
	while (1) {
		char *p = strstr(haystack, needle);
		if (!p)
			return r;
		r = p;
		haystack = p + 1;
	}
}

#if ENABLE_UNIT_TEST

BBUNIT_DEFINE_TEST(strrstr)
{
	static const struct {
		const char *h, *n;
		int pos;
	} test_array[] = {
		/* 0123456789 */
		{ "baaabaaab",  "aaa", 5  },
		{ "baaabaaaab", "aaa", 6  },
		{ "baaabaab",   "aaa", 1  },
		{ "aaa",        "aaa", 0  },
		{ "aaa",        "a",   2  },
		{ "aaa",        "bbb", -1 },
		{ "a",          "aaa", -1 },
		{ "aaa",        "",    3  },
		{ "",           "aaa", -1 },
		{ "",           "",    0  },
	};

	int i;

	i = 0;
	while (i < sizeof(test_array) / sizeof(test_array[0])) {
		const char *r = strrstr(test_array[i].h, test_array[i].n);
		if (r == NULL)
			r = test_array[i].h - 1;
		BBUNIT_ASSERT_EQ(r, test_array[i].h + test_array[i].pos);
		i++;
	}

	BBUNIT_ENDTEST;
}

#endif /* ENABLE_UNIT_TEST */
