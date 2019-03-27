/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: freembt.c,v 1.3.2.2 2012/07/22 08:04:24 darren_r Exp $
 */

#include "ipf.h"

void freembt(m)
	mb_t *m;
{

	free(m);
}
