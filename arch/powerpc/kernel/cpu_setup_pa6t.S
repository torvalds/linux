/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 */

#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>

/* Right now, restore and setup are the same thing */
_GLOBAL(__restore_cpu_pa6t)
_GLOBAL(__setup_cpu_pa6t)
	/* Do nothing if not running in HV mode */
	mfmsr	r0
	rldicl.	r0,r0,4,63
	beqlr

	mfspr	r0,SPRN_HID5
	ori	r0,r0,0x38
	mtspr	SPRN_HID5,r0

	mfspr	r0,SPRN_LPCR
	ori	r0,r0,0x7000
	mtspr	SPRN_LPCR,r0

	blr
