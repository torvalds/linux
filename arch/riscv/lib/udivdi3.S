/*
 * Copyright (C) 2016-2017 Free Software Foundation, Inc.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/linkage.h>

ENTRY(__udivdi3)
	mv	a2, a1
	mv	a1, a0
	li	a0, -1
	beqz	a2, .L5
	li	a3, 1
	bgeu	a2, a1, .L2
.L1:
	blez	a2, .L2
	slli	a2, a2, 1
	slli	a3, a3, 1
	bgtu	a1, a2, .L1
.L2:
	li	a0, 0
.L3:
	bltu	a1, a2, .L4
	sub	a1, a1, a2
	or	a0, a0, a3
.L4:
	srli	a3, a3, 1
	srli	a2, a2, 1
	bnez	a3, .L3
.L5:
	ret
ENDPROC(__udivdi3)
