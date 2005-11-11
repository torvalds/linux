/*
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "of1275.h"
#include <stdarg.h>

int call_prom(const char *service, int nargs, int nret, ...)
{
	int i;
	struct prom_args {
		const char *service;
		int nargs;
		int nret;
		unsigned int args[12];
	} args;
	va_list list;

	args.service = service;
	args.nargs = nargs;
	args.nret = nret;

	va_start(list, nret);
	for (i = 0; i < nargs; i++)
		args.args[i] = va_arg(list, unsigned int);
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (of_prom_entry(&args) < 0)
		return -1;

	return (nret > 0)? args.args[nargs]: 0;
}

int call_prom_ret(const char *service, int nargs, int nret,
		  unsigned int *rets, ...)
{
	int i;
	struct prom_args {
		const char *service;
		int nargs;
		int nret;
		unsigned int args[12];
	} args;
	va_list list;

	args.service = service;
	args.nargs = nargs;
	args.nret = nret;

	va_start(list, rets);
	for (i = 0; i < nargs; i++)
		args.args[i] = va_arg(list, unsigned int);
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (of_prom_entry(&args) < 0)
		return -1;

	if (rets != (void *) 0)
		for (i = 1; i < nret; ++i)
			rets[i-1] = args.args[nargs+i];

	return (nret > 0)? args.args[nargs]: 0;
}
