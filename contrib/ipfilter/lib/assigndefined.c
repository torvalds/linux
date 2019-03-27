/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: assigndefined.c,v 1.4.2.2 2012/07/22 08:04:24 darren_r Exp $
 */

#include "ipf.h"

void assigndefined(env)
	char *env;
{
	char *s, *t;

	if (env == NULL)
		return;

	for (s = strtok(env, ";"); s != NULL; s = strtok(NULL, ";")) {
		t = strchr(s, '=');
		if (t == NULL)
			continue;
		*t++ = '\0';
		set_variable(s, t);
		*--t = '=';
	}
}
