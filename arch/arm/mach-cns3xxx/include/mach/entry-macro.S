/*
 * Low-level IRQ helper macros for Cavium Networks platforms
 *
 * Copyright 2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#include <mach/hardware.h>
#include <asm/hardware/entry-macro-gic.S>

		.macro	disable_fiq
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm
