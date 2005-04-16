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
enter(void)
{
    struct prom_args {
	char *service;
    } args;

    args.service = "enter";
    (*of_prom_entry)(&args);
}
