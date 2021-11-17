/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */

#include <asm/memory.h>

.data
.global sp_tmp
sp_tmp:
.long

.text
.globl suspend2ram
.globl cpu_resume

suspend2ram:
	pushm   $r0, $r31
#if defined(CONFIG_HWZOL)
	mfusr   $r0, $lc
	mfusr   $r1, $le
	mfusr   $r2, $lb
#endif
	mfsr	$r3, $mr0
	mfsr    $r4, $mr1
	mfsr    $r5, $mr4
	mfsr    $r6, $mr6
	mfsr    $r7, $mr7
	mfsr    $r8, $mr8
	mfsr    $r9, $ir0
	mfsr    $r10, $ir1
	mfsr    $r11, $ir2
	mfsr    $r12, $ir3
	mfsr    $r13, $ir9
	mfsr    $r14, $ir10
	mfsr    $r15, $ir12
	mfsr    $r16, $ir13
	mfsr    $r17, $ir14
	mfsr    $r18, $ir15
	pushm   $r0, $r19
#if defined(CONFIG_FPU)
	jal	store_fpu_for_suspend
#endif
	tlbop	FlushAll
	isb

	// transfer $sp from va to pa
	sethi	$r0, hi20(PAGE_OFFSET)
	ori	$r0, $r0, lo12(PAGE_OFFSET)
	movi	$r2, PHYS_OFFSET
	sub	$r1, $sp, $r0
	add	$r2, $r1, $r2

	// store pa($sp) to sp_tmp
	sethi 	$r1, hi20(sp_tmp)
	swi	$r2, [$r1 + lo12(sp_tmp)]

	pushm	$r16, $r25
	pushm	$r29, $r30
#ifdef	CONFIG_CACHE_L2
	jal	dcache_wb_all_level
#else
	jal	cpu_dcache_wb_all
#endif
	popm	$r29, $r30
	popm	$r16, $r25

	// get wake_mask and loop in standby
	la	$r1, wake_mask
	lwi	$r1, [$r1]
self_loop:
	standby wake_grant
	mfsr	$r2, $ir15
	and	$r2, $r1, $r2
	beqz	$r2, self_loop

	// set ipc to resume address
	la	$r1, resume_addr
	lwi	$r1, [$r1]
	mtsr	$r1, $ipc
	isb

	// reset psw, turn off the address translation
	li      $r2, 0x7000a
	mtsr    $r2, $ipsw
	isb

	iret
cpu_resume:
	// translate the address of sp_tmp variable to pa
	la	$r1, sp_tmp
	sethi   $r0, hi20(PAGE_OFFSET)
	ori     $r0, $r0, lo12(PAGE_OFFSET)
	movi    $r2, PHYS_OFFSET
	sub     $r1, $r1, $r0
	add     $r1, $r1, $r2

	// access the sp_tmp to get stack pointer
	lwi	$sp, [$r1]

	popm	$r0, $r19
#if defined(CONFIG_HWZOL)
	mtusr   $r0, $lb
	mtusr   $r1, $lc
	mtusr   $r2, $le
#endif
	mtsr	$r3, $mr0
	mtsr    $r4, $mr1
	mtsr    $r5, $mr4
	mtsr    $r6, $mr6
	mtsr    $r7, $mr7
	mtsr    $r8, $mr8
	// set original psw to ipsw
	mtsr    $r9, $ir1

	mtsr    $r11, $ir2
	mtsr    $r12, $ir3

	// set ipc to RR
	la	$r13, RR
	mtsr	$r13, $ir9

	mtsr    $r14, $ir10
	mtsr    $r15, $ir12
	mtsr    $r16, $ir13
	mtsr    $r17, $ir14
	mtsr    $r18, $ir15
	popm    $r0, $r31

	isb
	iret
RR:
	ret
