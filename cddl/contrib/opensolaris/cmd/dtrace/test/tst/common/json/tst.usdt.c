/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2012 (c), Joyent, Inc.  All rights reserved.
 */

#include <sys/sdt.h>
#include <stdio.h>
#include <stdlib.h>
#include "usdt.h"

#define	FMT	"{" \
		"  \"sizes\": [ \"first\", 2, %f ]," \
		"  \"index\": %d," \
		"  \"facts\": {" \
		"    \"odd\": \"%s\"," \
		"    \"even\": \"%s\"" \
		"  }," \
		"  \"action\": \"%s\"" \
		"}\n"

int
waiting(volatile int *a)
{
	return (*a);
}

int
main(int argc, char **argv)
{
	volatile int a = 0;
	int idx;
	double size = 250.5;

	while (waiting(&a) == 0)
		continue;

	for (idx = 0; idx < 10; idx++) {
		char *odd, *even, *json, *action;

		size *= 1.78;
		odd = idx % 2 == 1 ? "true" : "false";
		even = idx % 2 == 0 ? "true" : "false";
		action = idx == 7 ? "ignore" : "print";

		asprintf(&json, FMT, size, idx, odd, even, action);
		BUNYAN_FAKE_LOG_DEBUG(json);
		free(json);
	}

	BUNYAN_FAKE_LOG_DEBUG("{\"finished\": true}");

	return (0);
}
