/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/page.h>

/*
 * clear_page
 * r12: P1 address (to)
 */
	.text
	.global clear_page
clear_page:
	sub	r9, r12, -PAGE_SIZE
	mov     r10, 0
	mov	r11, 0
0:      st.d    r12++, r10
	cp      r12, r9
	brne	0b
	mov     pc, lr
