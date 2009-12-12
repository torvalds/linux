/*
 * arch/powerpc/boot/wii.c
 *
 * Nintendo Wii bootwrapper support
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 */

#include <stddef.h>
#include "stdio.h"
#include "types.h"
#include "io.h"
#include "ops.h"

#include "ugecon.h"

BSS_STACK(8192);

#define HW_REG(x)	((void *)(x))

#define EXI_CTRL	HW_REG(0x0d800070)
#define EXI_CTRL_ENABLE	(1<<0)

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5)
{
	u32 heapsize = 24*1024*1024 - (u32)_end;

	simple_alloc_init(_end, heapsize, 32, 64);
	fdt_init(_dtb_start);

	/*
	 * 'mini' boots the Broadway processor with EXI disabled.
	 * We need it enabled before probing for the USB Gecko.
	 */
	out_be32(EXI_CTRL, in_be32(EXI_CTRL) | EXI_CTRL_ENABLE);

	if (ug_probe())
		console_ops.write = ug_console_write;
}

