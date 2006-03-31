/*
 * "Indirect" DCR access
 *
 * Copyright (c) 2004 Eugene Surovegin <ebs@ebshome.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of  the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <asm/ppc_asm.h>
#include <asm/processor.h>

#define DCR_ACCESS_PROLOG(table) \
	rlwinm  r3,r3,4,18,27;   \
	lis     r5,table@h;      \
	ori     r5,r5,table@l;   \
	add     r3,r3,r5;        \
	mtctr   r3;              \
	bctr

_GLOBAL(__mfdcr)
	DCR_ACCESS_PROLOG(__mfdcr_table)

_GLOBAL(__mtdcr)
	DCR_ACCESS_PROLOG(__mtdcr_table)

__mfdcr_table:
	mfdcr  r3,0; blr
__mtdcr_table:
	mtdcr  0,r4; blr

dcr     = 1
        .rept   1023
	mfdcr   r3,dcr; blr
	mtdcr   dcr,r4; blr
	dcr     = dcr + 1
	.endr
