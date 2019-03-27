/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: prependmbt.c,v 1.3.2.3 2012/07/22 08:04:24 darren_r Exp $
 */

#include "ipf.h"

int prependmbt(fin, m)
	fr_info_t *fin;
	mb_t *m;
{
	m->mb_next = *fin->fin_mp;
	*fin->fin_mp = m;
	return 0;
}
