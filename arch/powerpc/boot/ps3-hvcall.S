/*
 *  PS3 bootwrapper hvcalls.
 *
 *  Copyright (C) 2007 Sony Computer Entertainment Inc.
 *  Copyright 2007 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ppc_asm.h"

	.machine "ppc64"

/*
 * The PS3 hypervisor uses a 64 bit "C" language calling convention.
 * The routines here marshal arguments between the 32 bit wrapper
 * program and the 64 bit hvcalls.
 *
 *  wrapper           lv1
 *  32-bit (h,l)      64-bit
 *
 *  1: r3,r4          <-> r3
 *  2: r5,r6          <-> r4
 *  3: r7,r8          <-> r5
 *  4: r9,r10         <-> r6
 *  5: 8(r1),12(r1)   <-> r7
 *  6: 16(r1),20(r1)  <-> r8
 *  7: 24(r1),28(r1)  <-> r9
 *  8: 32(r1),36(r1)  <-> r10
 *
 */

.macro GLOBAL name
	.section ".text"
	.balign 4
	.globl \name
\name:
.endm

.macro NO_SUPPORT name
	GLOBAL \name
	b ps3_no_support
.endm

.macro HVCALL num
	li r11, \num
	.long 0x44000022
	extsw r3, r3
.endm

.macro SAVE_LR offset=4
	mflr r0
	stw r0, \offset(r1)
.endm

.macro LOAD_LR offset=4
	lwz r0, \offset(r1)
	mtlr r0
.endm

.macro LOAD_64_REG target,high,low
	sldi r11, \high, 32
	or \target, r11, \low
.endm

.macro LOAD_64_STACK target,offset
	ld \target, \offset(r1)
.endm

.macro LOAD_R3
	LOAD_64_REG r3,r3,r4
.endm

.macro LOAD_R4
	LOAD_64_REG r4,r5,r6
.endm

.macro LOAD_R5
	LOAD_64_REG r5,r7,r8
.endm

.macro LOAD_R6
	LOAD_64_REG r6,r9,r10
.endm

.macro LOAD_R7
	LOAD_64_STACK r7,8
.endm

.macro LOAD_R8
	LOAD_64_STACK r8,16
.endm

.macro LOAD_R9
	LOAD_64_STACK r9,24
.endm

.macro LOAD_R10
	LOAD_64_STACK r10,32
.endm

.macro LOAD_REGS_0
	stwu 1,-16(1)
	stw 3, 8(1)
.endm

.macro LOAD_REGS_5
	LOAD_R3
	LOAD_R4
	LOAD_R5
	LOAD_R6
	LOAD_R7
.endm

.macro LOAD_REGS_6
	LOAD_REGS_5
	LOAD_R8
.endm

.macro LOAD_REGS_8
	LOAD_REGS_6
	LOAD_R9
	LOAD_R10
.endm

.macro STORE_REGS_0_1
	lwz r11, 8(r1)
	std r4, 0(r11)
	mr r4, r3
	li r3, 0
	addi r1,r1,16
.endm

.macro STORE_REGS_5_2
	lwz r11, 16(r1)
	std r4, 0(r11)
	lwz r11, 24(r1)
	std r5, 0(r11)
.endm

.macro STORE_REGS_6_1
	lwz r11, 24(r1)
	std r4, 0(r11)
.endm

GLOBAL lv1_get_logical_ppe_id
	SAVE_LR
	LOAD_REGS_0
	HVCALL 69
	STORE_REGS_0_1
	LOAD_LR
	blr

GLOBAL lv1_get_logical_partition_id
	SAVE_LR
	LOAD_REGS_0
	HVCALL 74
	STORE_REGS_0_1
	LOAD_LR
	blr

GLOBAL lv1_get_repository_node_value
	SAVE_LR
	LOAD_REGS_5
	HVCALL 91
	STORE_REGS_5_2
	LOAD_LR
	blr

GLOBAL lv1_panic
	SAVE_LR
	LOAD_REGS_8
	HVCALL 255
	LOAD_LR
	blr
