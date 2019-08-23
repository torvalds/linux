/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2006 PathScale, Inc.  All Rights Reserved.
 */

#include <linux/linkage.h>

/*
 * override generic version in lib/iomap_copy.c
 */
ENTRY(__iowrite32_copy)
	movl %edx,%ecx
	rep movsd
	ret
ENDPROC(__iowrite32_copy)
