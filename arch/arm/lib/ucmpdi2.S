/*
 *  linux/arch/arm/lib/ucmpdi2.S
 *
 *  Author:	Nicolas Pitre
 *  Created:	Oct 19, 2005
 *  Copyright:	Monta Vista Software, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

#ifdef __ARMEB__
#define xh r0
#define xl r1
#define yh r2
#define yl r3
#else
#define xl r0
#define xh r1
#define yl r2
#define yh r3
#endif

ENTRY(__ucmpdi2)

	cmp	xh, yh
	cmpeq	xl, yl
	movlo	r0, #0
	moveq	r0, #1
	movhi	r0, #2
	ret	lr

ENDPROC(__ucmpdi2)

#ifdef CONFIG_AEABI

ENTRY(__aeabi_ulcmp)

	cmp	xh, yh
	cmpeq	xl, yl
	movlo	r0, #-1
	moveq	r0, #0
	movhi	r0, #1
	ret	lr

ENDPROC(__aeabi_ulcmp)

#endif

