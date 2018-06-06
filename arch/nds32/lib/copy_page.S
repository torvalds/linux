// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>
#include <asm/export.h>
#include <asm/page.h>

	.text
ENTRY(copy_page)
	pushm	$r2, $r10
	movi	$r2, PAGE_SIZE >> 5
.Lcopy_loop:
	lmw.bim	$r3, [$r1], $r10
	smw.bim	$r3, [$r0], $r10
	subi45	$r2, #1
	bnez38	$r2, .Lcopy_loop
	popm	$r2, $r10
	ret
ENDPROC(copy_page)
EXPORT_SYMBOL(copy_page)

ENTRY(clear_page)
	pushm	$r1, $r9
	movi	$r1, PAGE_SIZE >> 5
	movi55	$r2, #0
	movi55	$r3, #0
	movi55	$r4, #0
	movi55	$r5, #0
	movi55	$r6, #0
	movi55	$r7, #0
	movi55	$r8, #0
	movi55	$r9, #0
.Lclear_loop:
	smw.bim	$r2, [$r0], $r9
	subi45	$r1, #1
	bnez38	$r1, .Lclear_loop
	popm	$r1, $r9
        ret
ENDPROC(clear_page)
EXPORT_SYMBOL(clear_page)
