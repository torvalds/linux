// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2014 Carlo Caione <carlo@caione.org>
 */

#include <asm/mach/arch.h>

static const char * const meson_common_board_compat[] = {
	"amlogic,meson6",
	"amlogic,meson8",
	"amlogic,meson8b",
	"amlogic,meson8m2",
	NULL,
};

DT_MACHINE_START(MESON, "Amlogic Meson platform")
	.dt_compat	= meson_common_board_compat,
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
MACHINE_END
