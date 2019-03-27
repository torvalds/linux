/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: msgdsize.c,v 1.2.4.3 2012/07/22 08:04:24 darren_r Exp $
 */

#include "ipf.h"

size_t msgdsize(orig)
	mb_t *orig;
{
	size_t sz = 0;
	mb_t *m;

	for (m = orig; m != NULL; m = m->mb_next)
		sz += m->mb_len;
	return sz;
}
