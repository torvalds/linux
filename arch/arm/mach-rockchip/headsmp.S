/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 */
#include <linux/linkage.h>
#include <linux/init.h>

ENTRY(rockchip_secondary_trampoline)
	ldr	pc, 1f
ENDPROC(rockchip_secondary_trampoline)
	.globl	rockchip_boot_fn
rockchip_boot_fn:
1:	.space	4

ENTRY(rockchip_secondary_trampoline_end)
