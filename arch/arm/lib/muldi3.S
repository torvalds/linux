/*
 *  linux/arch/arm/lib/muldi3.S
 *
 *  Author:     Nicolas Pitre
 *  Created:    Oct 19, 2005
 *  Copyright:  Monta Vista Software, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>

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

ENTRY(__muldi3)
ENTRY(__aeabi_lmul)

	mul	xh, yl, xh
	mla	xh, xl, yh, xh
	mov	ip, xl, lsr #16
	mov	yh, yl, lsr #16
	bic	xl, xl, ip, lsl #16
	bic	yl, yl, yh, lsl #16
	mla	xh, yh, ip, xh
	mul	yh, xl, yh
	mul	xl, yl, xl
	mul	ip, yl, ip
	adds	xl, xl, yh, lsl #16
	adc	xh, xh, yh, lsr #16
	adds	xl, xl, ip, lsl #16
	adc	xh, xh, ip, lsr #16
	mov	pc, lr

