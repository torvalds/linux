/* SPDX-License-Identifier: GPL-2.0 */
        ! Copyright 2007,2008 Imagination Technologies Ltd.

#include <asm/page.h>

        .text
        .global	_copy_page
        .type   _copy_page,function
	!! D1Ar1 - to
	!! D0Ar2 - from
_copy_page:
	MOV  D0FrT,#PAGE_SIZE
$Lcopy_page_loop:
	GETL D0Re0,D1Re0,[D0Ar2++]
	GETL D0Ar6,D1Ar5,[D0Ar2++]
	SETL [D1Ar1++],D0Re0,D1Re0
	SETL [D1Ar1++],D0Ar6,D1Ar5
	SUBS D0FrT,D0FrT,#16
	BNZ  $Lcopy_page_loop
	MOV  PC,D1RtP
        .size	_copy_page,.-_copy_page
