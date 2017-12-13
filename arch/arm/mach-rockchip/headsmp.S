/*
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/linkage.h>
#include <linux/init.h>

	.arm

ENTRY(rockchip_secondary_trampoline)
	ldr	pc, 1f
ENDPROC(rockchip_secondary_trampoline)
	.globl	rockchip_boot_fn
rockchip_boot_fn:
1:	.space	4

ENTRY(rockchip_secondary_trampoline_end)
