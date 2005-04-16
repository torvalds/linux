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

prom_entry of_prom_entry;
ihandle of_prom_mmu;

void
ofinit(prom_entry prom_ptr)
{
    phandle chosen;

    of_prom_entry = prom_ptr;

    if ((chosen = finddevice("/chosen")) == OF_INVALID_HANDLE)
	return;
    if (getprop(chosen, "mmu", &of_prom_mmu, sizeof(ihandle)) != 4)
	return;
}
