/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"

int bcopywrap(from, to, size)
	void *from, *to;
	size_t size;
{
	bcopy((caddr_t)from, (caddr_t)to, size);
	return 0;
}

