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
 * copy_page
 *
 * r12		to (P1 address)
 * r11		from (P1 address)
 * r8-r10	scratch
 */
	.text
	.global copy_page
copy_page:
	sub	r10, r11, -(1 << PAGE_SHIFT)
	/* pref	r11[0] */
1:	/* pref	r11[8] */
	ld.d	r8, r11++
	st.d	r12++, r8
	cp	r11, r10
	brlo	1b
	mov	pc, lr
