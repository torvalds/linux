/*
 * arch/sh/kernel/cpu/ubc.S
 *
 * Set of management routines for the User Break Controller (UBC)
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/linkage.h>
#include <asm/ubc.h>

#define STBCR2		0xffc00010

ENTRY(ubc_sleep)
	mov	#0, r0

	mov.l	1f, r1		! Zero out UBC_BBRA ..
	mov.w	r0, @r1

	mov.l	2f, r1		! .. same for BBRB ..
	mov.w	r0, @r1

	mov.l	3f, r1		! .. and again for BRCR.
	mov.w	r0, @r1

	mov.w	@r1, r0		! Dummy read BRCR

	mov.l	4f, r1		! Set MSTP5 in STBCR2
	mov.b	@r1, r0
	or	#0x01, r0
	mov.b	r0, @r1

	mov.b	@r1, r0		! Two dummy reads ..
	mov.b	@r1, r0

	rts
	nop

ENTRY(ubc_wakeup)
	mov.l	4f, r1		! Clear MSTP5
	mov.b	@r1, r0
	and	#0xfe, r0
	mov.b	r0, @r1

	mov.b	@r1, r0		! Two more dummy reads ..
	mov.b	@r1, r0

	rts
	nop

1:	.long	UBC_BBRA
2:	.long	UBC_BBRB
3:	.long	UBC_BRCR
4:	.long	STBCR2

