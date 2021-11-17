// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>


.macro 	lbi1 dst, addr, adj
lbi.bi	\dst, [\addr], \adj
.endm

.macro 	sbi1 src, addr, adj
sbi.bi	\src, [\addr], \adj
.endm

.macro	lmw1 start_reg, addr, end_reg
lmw.bim \start_reg, [\addr], \end_reg
.endm

.macro	smw1 start_reg, addr, end_reg
smw.bim \start_reg, [\addr], \end_reg
.endm

.text
ENTRY(memcpy)
	move	$r5, $r0
#include "copy_template.S"
	move	$r0, $r5
	ret

ENDPROC(memcpy)
