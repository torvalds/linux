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
    struct employee {
	const char *e_first;
	const char *e_last;
	unsigned e_dept;
    } employees[] = {
	{ "Terry", "Jones", 660 },
	{ "Leslie", "Patterson", 341 },
	{ "Ashley", "Smith", 1440 },
	{ NULL, NULL }
    }, *ep = employees;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_set_info(NULL, info, info_count);

    xo_set_flags(NULL, XOF_DTRT);

    xo_open_container("employees");
    xo_open_list("employee");

    for ( ; ep->e_first; ep++) {
	xo_open_instance("employee");
	xo_emit("{:first-name} {:last-name} works in dept #{:department/%u}\n",
		ep->e_first, ep->e_last, ep->e_dept);
	xo_close_instance_d();
    }

    xo_close_list_d();
    xo_close_container_d();

    xo_finish();

    return 0;
}
