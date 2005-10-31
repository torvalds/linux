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

void *
claim(unsigned int virt, unsigned int size, unsigned int align)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	unsigned int virt;
	unsigned int size;
	unsigned int align;
	void *ret;
    } args;

    args.service = "claim";
    args.nargs = 3;
    args.nret = 1;
    args.virt = virt;
    args.size = size;
    args.align = align;
    args.ret = (void *) 0;
    (*of_prom_entry)(&args);
    return args.ret;
}
