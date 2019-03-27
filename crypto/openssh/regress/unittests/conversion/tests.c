/* 	$OpenBSD: tests.c,v 1.1 2017/03/14 01:20:29 dtucker Exp $ */
/*
 * Regress test for conversions
 *
 * Placed in the public domain
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "../test_helper/test_helper.h"

#include "misc.h"

void
tests(void)
{
	char buf[1024];

	TEST_START("conversion_convtime");
	ASSERT_LONG_EQ(convtime("0"), 0);
	ASSERT_LONG_EQ(convtime("1"), 1);
	ASSERT_LONG_EQ(convtime("1S"), 1);
	/* from the examples in the comment above the function */
	ASSERT_LONG_EQ(convtime("90m"), 5400);
	ASSERT_LONG_EQ(convtime("1h30m"), 5400);
	ASSERT_LONG_EQ(convtime("2d"), 172800);
	ASSERT_LONG_EQ(convtime("1w"), 604800);

	/* negative time is not allowed */
	ASSERT_LONG_EQ(convtime("-7"), -1);
	ASSERT_LONG_EQ(convtime("-9d"), -1);
	
	/* overflow */
	snprintf(buf, sizeof buf, "%llu", (unsigned long long)LONG_MAX + 1);
	ASSERT_LONG_EQ(convtime(buf), -1);

	/* overflow with multiplier */
	snprintf(buf, sizeof buf, "%lluM", (unsigned long long)LONG_MAX/60 + 1);
	ASSERT_LONG_EQ(convtime(buf), -1);
	ASSERT_LONG_EQ(convtime("1000000000000000000000w"), -1);
	TEST_DONE();
}
