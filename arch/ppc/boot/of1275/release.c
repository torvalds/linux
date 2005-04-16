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

void
release(void *virt, unsigned int size)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	void *virt;
	unsigned int size;
    } args;

    args.service = "release";
    args.nargs = 2;
    args.nret = 0;
    args.virt = virt;
    args.size = size;
    (*of_prom_entry)(&args);
}
