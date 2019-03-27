/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xo.h"

xo_info_t info[] = {
    { "employee", "object", "Employee data" },
    { "first-name", "string", "First name of employee" },
    { "last-name", "string", "Last name of employee" },
    { "department", "number", "Department number" },
};
int info_count = (sizeof(info) / sizeof(info[0]));

int
main (int argc, char **argv)
{
    unsigned opt_count = 1;
    unsigned opt_extra = 0;

    struct employee {
	const char *e_first;
	const char *e_last;
	unsigned e_dept;
    } employees[] = {
	{ "Terry", "Jones", 660 },
	{ "Leslie", "Patterson", 341 },
	{ "Ashley", "Smith", 1440 },
	{ NULL, NULL }
    }, *ep;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    for (argc = 1; argv[argc]; argc++) {
	if (strcmp(argv[argc], "count") == 0) {
	    if (argv[argc + 1])
		opt_count = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "extra") == 0) {
	    if (argv[argc + 1])
		opt_extra = atoi(argv[++argc]);
	}
    }

    xo_set_info(NULL, info, info_count);

    xo_open_container("employees");
    xo_open_list("employee");

    xo_emit("[{:extra/%*s}]\n", opt_extra, "");

    xo_emit("{T:/%13s} {T:/%5s} {T:/%6s} {T:/%7s} {T:/%8s}  {T:Size(s)}\n",
	    "Type", "InUse", "MemUse", "HighUse", "Requests");
    xo_open_list("memory");
    xo_open_instance("memory");

#define PRIu64 "llu"
#define TO_ULL(_x) ((unsigned long long) _x)
    xo_emit("{k:type/%13s} {:in-use/%5" PRIu64 "} "
	    "{:memory-use/%5" PRIu64 "}{U:K} {:high-use/%7s} "
	    "{:requests/%8" PRIu64 "}  ",
	    "name", TO_ULL(12345), TO_ULL(54321), "-", TO_ULL(32145));

    int first = 1, i;
#if 0
    xo_open_list("size");
    for (i = 0; i < 32; i++) {
	if (!first)
	    xo_emit(",");
	xo_emit("{l:size/%d}", 1 << (i + 4));
	first = 0;
    }
    xo_close_list("size");
#endif
    xo_close_instance("memory");
    xo_emit("\n");
    xo_close_list("memory");

    while (opt_count-- != 0) {
	for (ep = employees; ep->e_first; ep++) {
	    xo_open_instance("employee");
	    xo_emit("{:first-name} {:last-name} works in "
		    "dept #{:department/%u}\n",
		    ep->e_first, ep->e_last, ep->e_dept);
	    xo_close_instance("employee");
	}
    }

    xo_emit("done\n");

    xo_close_list("employee");
    xo_close_container("employees");

    xo_finish();

    return 0;
}
