// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>
#include <asm/page.h>

ENTRY(__get_timerpage)
	sethi	$r0, hi20(. + PAGE_SIZE + 8)
	ori	$r0, $r0, lo12(. + PAGE_SIZE + 4)
	mfusr	$r1, $pc
	sub	$r0, $r1, $r0
	ret
ENDPROC(__get_timerpage)

ENTRY(__get_datapage)
	sethi	$r0, hi20(. + 2*PAGE_SIZE + 8)
	ori	$r0, $r0, lo12(. + 2*PAGE_SIZE + 4)
	mfusr	$r1, $pc
	sub	$r0, $r1, $r0
	ret
ENDPROC(__get_datapage)
