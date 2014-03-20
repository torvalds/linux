/*
 * Non-board-specific low-level startup code
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>

#include <asm/page.h>

	.section .init.text,"ax"
	.global kernel_entry
kernel_entry:
	/* Start the show */
	lddpc   pc, kernel_start_addr

	.align  2
kernel_start_addr:
	.long   start_kernel
