#ifdef FLEX_SCANNER
/*
dnl   tables_shared.c - tables serialization code
dnl 
dnl   Copyright (c) 1990 The Regents of the University of California.
dnl   All rights reserved.
dnl 
dnl   This code is derived from software contributed to Berkeley by
dnl   Vern Paxson.
dnl 
dnl   The United States Government has rights in this work pursuant
dnl   to contract no. DE-AC03-76SF00098 between the United States
dnl   Department of Energy and the University of California.
dnl 
dnl   This file is part of flex.
dnl 
dnl   Redistribution and use in source and binary forms, with or without
dnl   modification, are permitted provided that the following conditions
dnl   are met:
dnl 
dnl   1. Redistributions of source code must retain the above copyright
dnl      notice, this list of conditions and the following disclaimer.
dnl   2. Redistributions in binary form must reproduce the above copyright
dnl      notice, this list of conditions and the following disclaimer in the
dnl      documentation and/or other materials provided with the distribution.
dnl 
dnl   Neither the name of the University nor the names of its contributors
dnl   may be used to endorse or promote products derived from this software
dnl   without specific prior written permission.
dnl 
dnl   THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
dnl   IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
dnl   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
dnl   PURPOSE.
dnl 
*/

/* This file is meant to be included in both the skeleton and the actual
 * flex code (hence the name "_shared"). 
 */
#ifndef yyskel_static
#define yyskel_static static
#endif
#else
#include "flexdef.h"
#include "tables.h"
#ifndef yyskel_static
#define yyskel_static
#endif
#endif


/** Get the number of integers in this table. This is NOT the
 *  same thing as the number of elements.
 *  @param td the table 
 *  @return the number of integers in the table
 */
yyskel_static flex_int32_t yytbl_calc_total_len (const struct yytbl_data *tbl)
{
	flex_int32_t n;

	/* total number of ints */
	n = tbl->td_lolen;
	if (tbl->td_hilen > 0)
		n *= tbl->td_hilen;

	if (tbl->td_id == YYTD_ID_TRANSITION)
		n *= 2;
	return n;
}
