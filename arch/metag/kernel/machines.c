// SPDX-License-Identifier: GPL-2.0
/*
 *  arch/metag/kernel/machines.c
 *
 *  Copyright (C) 2012 Imagination Technologies Ltd.
 *
 *  Generic Meta Boards.
 */

#include <linux/init.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>

static const char *meta_boards_compat[] __initdata = {
	"img,meta",
	NULL,
};

MACHINE_START(META, "Generic Meta")
	.dt_compat	= meta_boards_compat,
MACHINE_END
