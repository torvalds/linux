/*
 * Enter and leave sleep state on chips with 6xx-style HID0
 * power management bits, which don't leave sleep state via reset.
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright (c) 2006-2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <asm/ppc_asm.h>
#include <asm/reg.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>

_GLOBAL(mpc6xx_enter_standby)
	mflr	r4

	mfspr	r5, SPRN_HID0
	rlwinm	r5, r5, 0, ~(HID0_DOZE | HID0_NAP)
	oris	r5, r5, HID0_SLEEP@h
	mtspr	SPRN_HID0, r5
	isync

	lis	r5, ret_from_standby@h
	ori	r5, r5, ret_from_standby@l
	mtlr	r5

	CURRENT_THREAD_INFO(r5, r1)
	lwz	r6, TI_LOCAL_FLAGS(r5)
	ori	r6, r6, _TLF_SLEEPING
	stw	r6, TI_LOCAL_FLAGS(r5)

	mfmsr	r5
	ori	r5, r5, MSR_EE
	oris	r5, r5, MSR_POW@h
	sync
	mtmsr	r5
	isync

1:	b	1b

ret_from_standby:
	mfspr	r5, SPRN_HID0
	rlwinm	r5, r5, 0, ~HID0_SLEEP
	mtspr	SPRN_HID0, r5

	mtlr	r4
	blr
