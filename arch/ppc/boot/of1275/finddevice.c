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

phandle
finddevice(const char *name)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	const char *devspec;
	phandle device;
    } args;

    args.service = "finddevice";
    args.nargs = 1;
    args.nret = 1;
    args.devspec = name;
    args.device = OF_INVALID_HANDLE;
    (*of_prom_entry)(&args);
    return args.device;
}
