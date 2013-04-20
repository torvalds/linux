/*
 * tbidspram.S
 *
 * Copyright (C) 2009, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Explicit state save and restore routines forming part of the thread binary
 * interface for META processors
 */

	.file	"tbidspram.S"

/* These aren't generally useful to a user so for now, they arent publically available */
#define _TBIECH_DSPRAM_DUA_S    8
#define _TBIECH_DSPRAM_DUA_BITS 0x7f00
#define _TBIECH_DSPRAM_DUB_S    0
#define _TBIECH_DSPRAM_DUB_BITS 0x007f

/*
 * void *__TBIDspramSaveA( short DspramSizes, void *pExt )
 */
	.text
	.balign	4
	.global	___TBIDspramSaveA
	.type	___TBIDspramSaveA,function
___TBIDspramSaveA:

	SETL	[A0StP++], D0.5, D1.5
	MOV	A0.3, D0Ar2

	/* D1Ar1 - Dspram Sizes
	 * A0.4  - Pointer to buffer
	 */

	/* Save the specified amount of dspram DUA */
DL	MOV	D0AR.0, #0
	LSR	D1Ar1, D1Ar1, #_TBIECH_DSPRAM_DUA_S
	AND	D1Ar1, D1Ar1, #(_TBIECH_DSPRAM_DUA_BITS >> _TBIECH_DSPRAM_DUA_S)
	SUB	TXRPT, D1Ar1, #1
$L1:
DL	MOV	D0Re0, [D0AR.0++]
DL	MOV	D0Ar6, [D0AR.0++]
DL	MOV	D0Ar4, [D0AR.0++]
DL	MOV	D0.5,  [D0AR.0++]
	MSETL	[A0.3++], D0Re0, D0Ar6, D0Ar4, D0.5

	BR	$L1

	GETL	D0.5, D1.5, [--A0StP]
	MOV	PC, D1RtP

	.size	___TBIDspramSaveA,.-___TBIDspramSaveA

/*
 * void *__TBIDspramSaveB( short DspramSizes, void *pExt )
 */
	.balign	4
	.global	___TBIDspramSaveB
	.type	___TBIDspramSaveB,function
___TBIDspramSaveB:

	SETL	[A0StP++], D0.5, D1.5
	MOV	A0.3, D0Ar2

	/* D1Ar1 - Dspram Sizes
	 * A0.3  - Pointer to buffer
	 */

	/* Save the specified amount of dspram DUA */
DL	MOV	D0BR.0, #0
	LSR	D1Ar1, D1Ar1, #_TBIECH_DSPRAM_DUB_S
	AND	D1Ar1, D1Ar1, #(_TBIECH_DSPRAM_DUB_BITS >> _TBIECH_DSPRAM_DUB_S)
	SUB	TXRPT, D1Ar1, #1
$L2:
DL	MOV	D0Re0, [D0BR.0++]
DL	MOV	D0Ar6, [D0BR.0++]
DL	MOV	D0Ar4, [D0BR.0++]
DL	MOV	D0.5,  [D0BR.0++]
	MSETL	[A0.3++], D0Re0, D0Ar6, D0Ar4, D0.5

	BR	$L2

	GETL	D0.5, D1.5, [--A0StP]
	MOV	PC, D1RtP

	.size	___TBIDspramSaveB,.-___TBIDspramSaveB

/*
 * void *__TBIDspramRestoreA( short DspramSizes, void *pExt )
 */
	.balign	4
	.global	___TBIDspramRestoreA
	.type	___TBIDspramRestoreA,function
___TBIDspramRestoreA:

	SETL	[A0StP++], D0.5, D1.5
	MOV	A0.3, D0Ar2

	/* D1Ar1 - Dspram Sizes
	 * A0.3 - Pointer to buffer
	 */

	/* Restore the specified amount of dspram DUA */
DL	MOV	D0AW.0, #0
	LSR	D1Ar1, D1Ar1, #_TBIECH_DSPRAM_DUA_S
	AND	D1Ar1, D1Ar1, #(_TBIECH_DSPRAM_DUA_BITS >> _TBIECH_DSPRAM_DUA_S)
	SUB	TXRPT, D1Ar1, #1
$L3:
	MGETL	D0Re0, D0Ar6, D0Ar4, D0.5, [A0.3++]
DL	MOV	[D0AW.0++], D0Re0
DL	MOV	[D0AW.0++], D0Ar6
DL	MOV	[D0AW.0++], D0Ar4
DL	MOV	[D0AW.0++], D0.5

	BR	$L3

	GETL	D0.5, D1.5, [--A0StP]
	MOV	PC, D1RtP

	.size	___TBIDspramRestoreA,.-___TBIDspramRestoreA

/*
 * void *__TBIDspramRestoreB( short DspramSizes, void *pExt )
 */
	.balign	4
	.global	___TBIDspramRestoreB
	.type	___TBIDspramRestoreB,function
___TBIDspramRestoreB:

	SETL	[A0StP++], D0.5, D1.5
	MOV	A0.3, D0Ar2

	/* D1Ar1 - Dspram Sizes
	 * A0.3 - Pointer to buffer
	 */

	/* Restore the specified amount of dspram DUA */
DL	MOV	D0BW.0, #0
	LSR	D1Ar1, D1Ar1, #_TBIECH_DSPRAM_DUB_S
	AND	D1Ar1, D1Ar1, #(_TBIECH_DSPRAM_DUB_BITS >> _TBIECH_DSPRAM_DUB_S)
	SUB	TXRPT, D1Ar1, #1
$L4:
	MGETL	D0Re0, D0Ar6, D0Ar4, D0.5, [A0.3++]
DL	MOV	[D0BW.0++], D0Re0
DL	MOV	[D0BW.0++], D0Ar6
DL	MOV	[D0BW.0++], D0Ar4
DL	MOV	[D0BW.0++], D0.5

	BR	$L4

	GETL	D0.5, D1.5, [--A0StP]
	MOV	PC, D1RtP

	.size	___TBIDspramRestoreB,.-___TBIDspramRestoreB

/*
 * End of tbidspram.S
 */
