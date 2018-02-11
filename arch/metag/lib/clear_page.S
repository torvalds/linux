/* SPDX-License-Identifier: GPL-2.0 */
        ! Copyright 2007,2008,2009 Imagination Technologies Ltd.

#include <asm/page.h>

        .text
        .global	_clear_page
        .type   _clear_page,function
	!! D1Ar1 - page
_clear_page:
	MOV  TXRPT,#((PAGE_SIZE / 8) - 1)
	MOV  D0Re0,#0
	MOV  D1Re0,#0
$Lclear_page_loop:
	SETL [D1Ar1++],D0Re0,D1Re0
	BR   $Lclear_page_loop
	MOV  PC,D1RtP
        .size	_clear_page,.-_clear_page
