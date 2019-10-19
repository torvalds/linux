// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>

	.text
ENTRY(memzero)
	beqz	$r1, 1f
	push	$lp
	move    $r2, $r1
	move    $r1, #0
	push	$r0
	bal     memset
	pop	$r0
	pop	$lp
1:
        ret
ENDPROC(memzero)
