/*
 * Copyright (C) Paul Mackerras 1997.
 * Copyright (C) Leigh Brown 2002.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "of1275.h"

int
read(ihandle instance, void *buf, int buflen)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	ihandle instance;
	void *buf;
	int buflen;
	int actual;
    } args;

    args.service = "read";
    args.nargs = 3;
    args.nret = 1;
    args.instance = instance;
    args.buf = buf;
    args.buflen = buflen;
    args.actual = -1;
    (*of_prom_entry)(&args);
    return args.actual;
}
