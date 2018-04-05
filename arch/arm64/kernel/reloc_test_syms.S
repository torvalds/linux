/*
 * Copyright (C) 2017 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/linkage.h>

ENTRY(absolute_data64)
	ldr	x0, 0f
	ret
0:	.quad	sym64_abs
ENDPROC(absolute_data64)

ENTRY(absolute_data32)
	ldr	w0, 0f
	ret
0:	.long	sym32_abs
ENDPROC(absolute_data32)

ENTRY(absolute_data16)
	adr	x0, 0f
	ldrh	w0, [x0]
	ret
0:	.short	sym16_abs, 0
ENDPROC(absolute_data16)

ENTRY(signed_movw)
	movz	x0, #:abs_g2_s:sym64_abs
	movk	x0, #:abs_g1_nc:sym64_abs
	movk	x0, #:abs_g0_nc:sym64_abs
	ret
ENDPROC(signed_movw)

ENTRY(unsigned_movw)
	movz	x0, #:abs_g3:sym64_abs
	movk	x0, #:abs_g2_nc:sym64_abs
	movk	x0, #:abs_g1_nc:sym64_abs
	movk	x0, #:abs_g0_nc:sym64_abs
	ret
ENDPROC(unsigned_movw)

	.align	12
	.space	0xff8
ENTRY(relative_adrp)
	adrp	x0, sym64_rel
	add	x0, x0, #:lo12:sym64_rel
	ret
ENDPROC(relative_adrp)

	.align	12
	.space	0xffc
ENTRY(relative_adrp_far)
	adrp	x0, memstart_addr
	add	x0, x0, #:lo12:memstart_addr
	ret
ENDPROC(relative_adrp_far)

ENTRY(relative_adr)
	adr	x0, sym64_rel
	ret
ENDPROC(relative_adr)

ENTRY(relative_data64)
	adr	x1, 0f
	ldr	x0, [x1]
	add	x0, x0, x1
	ret
0:	.quad	sym64_rel - .
ENDPROC(relative_data64)

ENTRY(relative_data32)
	adr	x1, 0f
	ldr	w0, [x1]
	add	x0, x0, x1
	ret
0:	.long	sym64_rel - .
ENDPROC(relative_data32)

ENTRY(relative_data16)
	adr	x1, 0f
	ldrsh	w0, [x1]
	add	x0, x0, x1
	ret
0:	.short	sym64_rel - ., 0
ENDPROC(relative_data16)
