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
#include "nonstdio.h"

extern ihandle of_prom_mmu;

int
map(unsigned int phys, unsigned int virt, unsigned int size)
{
    struct prom_args {
	char *service;
	int nargs;
	int nret;
	char *method;
	ihandle mmu_ihandle;
	int misc;
	unsigned int size;
	unsigned int virt;
	unsigned int phys;
	int ret0;
    } args;

    if (of_prom_mmu == 0) {
    	printf("map() called, no MMU found\n");
    	return -1;
    }
    args.service = "call-method";
    args.nargs = 6;
    args.nret = 1;
    args.method = "map";
    args.mmu_ihandle = of_prom_mmu;
    args.misc = 0;
    args.phys = phys;
    args.virt = virt;
    args.size = size;
    (*of_prom_entry)(&args);

    return (int)args.ret0;
}
