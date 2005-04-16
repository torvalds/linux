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
getprop(phandle node, const char *name, void *buf, int buflen)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	phandle node;
	const char *name;
	void *buf;
	int buflen;
	int size;
    } args;

    args.service = "getprop";
    args.nargs = 4;
    args.nret = 1;
    args.node = node;
    args.name = name;
    args.buf = buf;
    args.buflen = buflen;
    args.size = -1;
    (*of_prom_entry)(&args);
    return args.size;
}
