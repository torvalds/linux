/*
 * Copyright 2014 Linaro Ltd.
 * Copyright (C) 2014 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

	.align 3
	.arm

/* It runs from physical address */
ENTRY(zx_resume_jump)
	adr	r1, zx_secondary_startup_pa
	ldr	r0, [r1]
	bx	r0
ENDPROC(zx_resume_jump)

ENTRY(zx_secondary_startup_pa)
	.word	zx_secondary_startup_pa

ENTRY(zx_suspend_iram_sz)
        .word	. - zx_resume_jump
ENDPROC(zx_secondary_startup_pa)


ENTRY(zx_secondary_startup)
	bl	v7_invalidate_l1
	b	secondary_startup
ENDPROC(zx_secondary_startup)
